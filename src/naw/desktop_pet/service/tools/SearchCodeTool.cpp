#include "naw/desktop_pet/service/CodeTools.h"
#include "naw/desktop_pet/service/tools/CodeToolsUtils.h"
#include "naw/desktop_pet/service/ToolManager.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <atomic>
#include <optional>

namespace fs = std::filesystem;
using namespace naw::desktop_pet::service;
using namespace naw::desktop_pet::service::tools;

// 优化1: 快速UTF-8字符边界检查(内联优化)
inline bool isUtf8CharStart(unsigned char c) {
    return (c & 0xC0) != 0x80;  // 简化检查: 非继续字节
}

// 优化2: 使用string_view进行零拷贝行分割
struct LineView {
    std::string_view line;
    size_t lineNumber;
    size_t byteOffset;
};

static std::vector<LineView> splitLinesView(std::string_view content) {
    std::vector<LineView> lines;
    lines.reserve(content.size() / 80);  // 预估平均行长
    
    size_t lineStart = 0;
    size_t lineNumber = 1;
    
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n') {
            size_t lineEnd = i;
            if (lineEnd > lineStart && content[lineEnd - 1] == '\r') {
                lineEnd--;
            }
            
            if (lineEnd > lineStart) {
                lines.push_back({
                    content.substr(lineStart, lineEnd - lineStart),
                    lineNumber,
                    lineStart
                });
            }
            
            lineStart = i + 1;
            lineNumber++;
        }
    }
    
    // 处理最后一行
    if (lineStart < content.size()) {
        lines.push_back({
            content.substr(lineStart),
            lineNumber,
            lineStart
        });
    }
    
    return lines;
}

// 优化3: 简化的字符串匹配器(针对普通字符串搜索)
class OptimizedStringMatcher {
private:
    std::string pattern;
    std::string lowerPattern;
    bool caseSensitive;
    size_t patternLen;
    
public:
    OptimizedStringMatcher(const std::string& pat, bool cs) 
        : pattern(pat), caseSensitive(cs), patternLen(pat.length()) {
        if (!caseSensitive) {
            lowerPattern = pattern;
            std::transform(lowerPattern.begin(), lowerPattern.end(), 
                          lowerPattern.begin(), ::tolower);
        }
    }
    
    // 快速字符串搜索(使用标准库优化)
    size_t search(std::string_view text) const {
        if (patternLen == 0 || text.size() < patternLen) {
            return std::string::npos;
        }
        
        if (caseSensitive) {
            auto pos = text.find(pattern);
            return pos;
        } else {
            // 大小写不敏感搜索
            for (size_t i = 0; i <= text.size() - patternLen; ++i) {
                bool match = true;
                for (size_t j = 0; j < patternLen; ++j) {
                    if (std::tolower(static_cast<unsigned char>(text[i + j])) != 
                        static_cast<unsigned char>(lowerPattern[j])) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    return i;
                }
            }
            return std::string::npos;
        }
    }
};

// 辅助函数: 移除字符串中的空字节和其他控制字符
static std::string removeNullBytes(std::string_view input) {
    std::string result;
    result.reserve(input.size());
    
    for (char c : input) {
        // 跳过空字节和其他危险的控制字符
        if (c == '\0') {
            continue;
        }
        // 保留常见的控制字符(换行、制表符等)
        if (c == '\n' || c == '\r' || c == '\t' || 
            (c >= 32 && c <= 126) || static_cast<unsigned char>(c) >= 128) {
            result += c;
        } else {
            // 其他控制字符替换为空格
            result += ' ';
        }
    }
    
    return result;
}

// 辅助函数: 检查内容是否可能是二进制文件
static bool isBinaryContent(std::string_view content, size_t sampleSize = 512) {
    size_t checkSize = std::min(content.size(), sampleSize);
    int nullCount = 0;
    int controlCount = 0;
    
    for (size_t i = 0; i < checkSize; ++i) {
        unsigned char c = static_cast<unsigned char>(content[i]);
        if (c == 0) {
            nullCount++;
        } else if (c < 32 && c != '\n' && c != '\r' && c != '\t') {
            controlCount++;
        }
    }
    
    // 如果空字节或控制字符超过5%，认为是二进制文件
    return (nullCount + controlCount) > static_cast<int>(checkSize * 0.05);
}

// 优化4: 计算UTF-8字符位置(优化版)
static int calculateUtf8Column(std::string_view line, size_t bytePos) {
    int column = 1;
    for (size_t i = 0; i < bytePos && i < line.size(); ++i) {
        if (isUtf8CharStart(static_cast<unsigned char>(line[i]))) {
            column++;
        }
    }
    return column;
}

// 优化5: 批量读取文件(保持原有编码处理 + 二进制过滤)
static std::string readFileContent(const fs::path& filePath, size_t maxSize = 10 * 1024 * 1024) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return "";
    }
    
    std::streamsize size = file.tellg();
    if (size > static_cast<std::streamsize>(maxSize)) {
        return "";
    }
    
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> rawContent(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(rawContent.data()), size)) {
        return "";
    }
    
    // 快速检查是否为二进制文件
    std::string_view contentView(reinterpret_cast<char*>(rawContent.data()), rawContent.size());
    if (isBinaryContent(contentView)) {
        return "";  // 跳过二进制文件
    }
    
    // 检测文件编码
    FileEncoding encoding = detectFileEncoding(rawContent);
    
    // 转换为UTF-8
    auto utf8Content = convertToUtf8(rawContent, encoding);
    if (!utf8Content.has_value()) {
        std::string content(rawContent.begin(), rawContent.end());
        content = removeNullBytes(content);  // 移除空字节
        return sanitizeUtf8String(content);
    }
    
    auto [result, isValid] = validateAndFixUtf8(utf8Content.value());
    result = removeNullBytes(result);  // 确保移除所有空字节
    return result;
}

// 优化6: 在内存中快速搜索(使用string_view和线程本地缓冲)
static void searchInContent(
    std::string_view content,
    const std::string& query,
    bool useRegex,
    bool caseSensitive,
    const fs::path& filePath,
    const fs::path& searchDir,
    std::vector<nlohmann::json>& localMatches  // 使用线程本地缓冲
) {
    // 准备文件路径字符串
    std::string filePathStr;
    try {
        fs::path relPath = fs::relative(filePath, searchDir);
        filePathStr = sanitizeUtf8String(pathToUtf8String(relPath));
    } catch (...) {
        filePathStr = sanitizeUtf8String(pathToUtf8String(filePath.filename()));
    }
    
    // 分割行(零拷贝)
    auto lines = splitLinesView(content);
    
    if (useRegex) {
        // 正则表达式搜索
        std::regex searchRegex;
        try {
            searchRegex = caseSensitive ? 
                std::regex(query, std::regex::optimize) :  // 添加optimize标志
                std::regex(query, std::regex::icase | std::regex::optimize);
        } catch (...) {
            return;
        }
        
        for (const auto& [line, lineNumber, byteOffset] : lines) {
            try {
                // 使用cmatch避免临时string分配
                std::match_results<std::string_view::const_iterator> match;
                if (std::regex_search(line.begin(), line.end(), match, searchRegex)) {
                    nlohmann::json matchResult;
                    matchResult["file"] = filePathStr;
                    matchResult["line"] = lineNumber;
                    
                    size_t matchBytePos = match.position();
                    matchResult["column"] = calculateUtf8Column(line, matchBytePos);
                    
                    // 清理上下文，移除空字节
                    std::string context = removeNullBytes(line);
                    // 限制上下文长度，避免过长
                    if (context.length() > 500) {
                        context = context.substr(0, 500) + "...";
                    }
                    matchResult["context"] = sanitizeUtf8String(context);
                    
                    localMatches.push_back(std::move(matchResult));
                }
            } catch (...) {
                // 跳过匹配错误
            }
        }
    } else {
        // 优化的字符串搜索
        OptimizedStringMatcher matcher(query, caseSensitive);
        
        for (const auto& [line, lineNumber, byteOffset] : lines) {
            if (!line.empty()) {
                size_t pos = matcher.search(line);
                
                if (pos != std::string::npos) {
                    nlohmann::json matchResult;
                    matchResult["file"] = filePathStr;
                    matchResult["line"] = lineNumber;
                    matchResult["column"] = calculateUtf8Column(line, pos);
                    
                    // 清理上下文，移除空字节
                    std::string context = removeNullBytes(line);
                    // 限制上下文长度
                    if (context.length() > 500) {
                        context = context.substr(0, 500) + "...";
                    }
                    matchResult["context"] = sanitizeUtf8String(context);
                    
                    localMatches.push_back(std::move(matchResult));
                }
            }
        }
    }
}

// 优化7: 收集文件(保持原有逻辑)
static std::vector<fs::path> collectFiles(
    const fs::path& dirPath,
    const std::string& filePattern
) {
    std::vector<fs::path> files;
    files.reserve(1000);  // 预分配空间
    
    try {
        fs::recursive_directory_iterator dirIter(dirPath, 
            fs::directory_options::skip_permission_denied);
        
        for (const auto& entry : dirIter) {
            try {
                if (fs::is_symlink(entry)) {
                    dirIter.disable_recursion_pending();
                    continue;
                }
                
                if (!fs::is_regular_file(entry)) {
                    continue;
                }
                
                // 使用UTF-8编码的文件名进行模式匹配
                std::string filename = pathToUtf8String(entry.path().filename());
                
                if (!filePattern.empty() && !matchesPattern(filename, filePattern)) {
                    continue;
                }
                
                if (isFileTooLarge(entry.path())) {
                    continue;
                }
                
                files.push_back(entry.path());
                
            } catch (const fs::filesystem_error&) {
                dirIter.disable_recursion_pending();
                continue;
            } catch (...) {
                continue;
            }
        }
    } catch (...) {
        // 处理文件系统错误
    }
    
    return files;
}

// 优化8: 并行处理文件(减少锁竞争)
static void processFilesParallel(
    const std::vector<fs::path>& files,
    const std::string& query,
    bool useRegex,
    bool caseSensitive,
    const fs::path& searchDir,
    std::vector<nlohmann::json>& matches,
    std::atomic<int>& filesSearched
) {
    std::mutex matchesMutex;
    unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency());
    
    // 对于少量文件,不使用多线程
    if (files.size() < 10) {
        numThreads = 1;
    }
    
    std::vector<std::thread> threads;
    size_t filesPerThread = (files.size() + numThreads - 1) / numThreads;
    
    for (unsigned int t = 0; t < numThreads; ++t) {
        size_t startIdx = t * filesPerThread;
        size_t endIdx = std::min(startIdx + filesPerThread, files.size());
        
        if (startIdx >= files.size()) {
            break;
        }
        
        threads.emplace_back([&, startIdx, endIdx]() {
            // 线程本地缓冲,减少锁竞争
            std::vector<nlohmann::json> threadLocalMatches;
            threadLocalMatches.reserve(100);
            
            for (size_t i = startIdx; i < endIdx; ++i) {
                try {
                    std::string content = readFileContent(files[i]);
                    if (content.empty()) {
                        continue;
                    }
                    
                    filesSearched++;
                    searchInContent(content, query, useRegex, caseSensitive, 
                                  files[i], searchDir, threadLocalMatches);
                                  
                } catch (...) {
                    // 跳过处理错误的文件
                    continue;
                }
            }
            
            // 批量合并结果,减少锁竞争
            if (!threadLocalMatches.empty()) {
                std::lock_guard<std::mutex> lock(matchesMutex);
                matches.insert(matches.end(), 
                             std::make_move_iterator(threadLocalMatches.begin()),
                             std::make_move_iterator(threadLocalMatches.end()));
            }
        });
    }
    
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

// 辅助函数:深度清理JSON对象(保持原有逻辑)
static nlohmann::json deepCleanJson(const nlohmann::json& j) {
    if (j.is_string()) {
        return sanitizeUtf8String(j.get<std::string>());
    } else if (j.is_array()) {
        nlohmann::json cleanedArray = nlohmann::json::array();
        for (const auto& item : j) {
            cleanedArray.push_back(deepCleanJson(item));
        }
        return cleanedArray;
    } else if (j.is_object()) {
        nlohmann::json cleanedObj = nlohmann::json::object();
        for (auto it = j.begin(); it != j.end(); ++it) {
            std::string key = sanitizeUtf8String(it.key());
            cleanedObj[key] = deepCleanJson(it.value());
        }
        return cleanedObj;
    } else {
        return j;
    }
}

static nlohmann::json handleSearchCode(const nlohmann::json& arguments) {
    try {
        // 提取参数
        if (!arguments.contains("query") || !arguments["query"].is_string()) {
            return nlohmann::json{{"error", "缺少必需参数: query"}};
        }
        
        // 验证并清理查询字符串
        std::string rawQuery = arguments["query"].get<std::string>();
        auto [query, queryValid] = validateAndFixUtf8(rawQuery);
        if (query.empty()) {
            return nlohmann::json{{"error", "查询字符串包含无效的UTF-8编码"}};
        }
        
        std::string directory = ".";
        if (arguments.contains("directory") && arguments["directory"].is_string()) {
            std::string rawDir = arguments["directory"].get<std::string>();
            auto [cleanedDir, dirValid] = validateAndFixUtf8(rawDir);
            directory = cleanedDir.empty() ? "." : cleanedDir;
        }
        
        std::string filePattern;
        if (arguments.contains("file_pattern") && arguments["file_pattern"].is_string()) {
            std::string rawPattern = arguments["file_pattern"].get<std::string>();
            auto [cleanedPattern, patternValid] = validateAndFixUtf8(rawPattern);
            filePattern = cleanedPattern;
        }
        
        bool caseSensitive = false;
        if (arguments.contains("case_sensitive") && arguments["case_sensitive"].is_boolean()) {
            caseSensitive = arguments["case_sensitive"].get<bool>();
        }
        
        // 从 UTF-8 字符串构造路径（Windows 上正确处理编码）
        fs::path dirPath = pathFromUtf8String(directory);
        
        // 检查目录是否存在
        if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
            std::string errorMsg = "目录不存在或不是目录: " + sanitizeUtf8String(directory);
            return nlohmann::json{{"error", errorMsg}};
        }
        
        // 判断是否使用正则表达式
        bool useRegex = false;
        if (query.find_first_of(".*+?[]{}()^$|\\") != std::string::npos) {
            try {
                if (!isValidUtf8(query)) {
                    return nlohmann::json{{"error", "正则表达式模式包含无效的UTF-8编码"}};
                }
                std::regex testRegex(query, std::regex::optimize);
                useRegex = true;
            } catch (...) {
                return nlohmann::json{{"error", "无效的正则表达式"}};
            }
        }
        
        // 步骤1: 收集所有文件
        std::vector<fs::path> files = collectFiles(dirPath, filePattern);
        
        if (files.empty()) {
            return nlohmann::json{
                {"matches", nlohmann::json::array()},
                {"total_matches", 0},
                {"files_searched", 0}
            };
        }
        
        // 步骤2: 并行搜索
        std::vector<nlohmann::json> matches;
        matches.reserve(files.size() * 2);  // 预分配空间
        std::atomic<int> filesSearched(0);
        
        processFilesParallel(files, query, useRegex, caseSensitive, 
                           dirPath, matches, filesSearched);
        
        // 步骤3: 深度清理所有匹配结果
        nlohmann::json cleanedMatches = nlohmann::json::array();
        for (auto& match : matches) {
            try {
                nlohmann::json cleanedMatch = deepCleanJson(match);
                cleanedMatches.push_back(std::move(cleanedMatch));
            } catch (...) {
                continue;
            }
        }
        
        // 构建结果
        nlohmann::json result;
        result["matches"] = std::move(cleanedMatches);
        result["total_matches"] = result["matches"].size();
        result["files_searched"] = filesSearched.load();
        
        return result;
        
    } catch (const std::exception& e) {
        std::string errorMsg = "搜索代码失败: " + sanitizeUtf8String(e.what());
        return nlohmann::json{{"error", errorMsg}};
    } catch (...) {
        return nlohmann::json{{"error", "搜索代码时发生未知错误"}};
    }
}

void CodeTools::registerSearchCodeTool(ToolManager& toolManager) {
    ToolDefinition tool;
    tool.name = "search_code";
    tool.description = "在代码文件中搜索文本或正则表达式。支持大小写敏感/不敏感搜索和文件类型过滤。";
    tool.parametersSchema = nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"query", {
                {"type", "string"},
                {"description", "搜索查询文本或正则表达式"}
            }},
            {"directory", {
                {"type", "string"},
                {"description", "搜索目录,默认为当前目录"}
            }},
            {"file_pattern", {
                {"type", "string"},
                {"description", "文件类型过滤,如 *.cpp"}
            }},
            {"case_sensitive", {
                {"type", "boolean"},
                {"default", false},
                {"description", "是否区分大小写"}
            }}
        }},
        {"required", {"query"}}
    };
    tool.handler = handleSearchCode;
    tool.permissionLevel = PermissionLevel::Public;
    
    toolManager.registerTool(tool, true);
}