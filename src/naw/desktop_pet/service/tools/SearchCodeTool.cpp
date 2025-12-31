#include "naw/desktop_pet/service/CodeTools.h"
#include "naw/desktop_pet/service/tools/CodeToolsUtils.h"
#include "naw/desktop_pet/service/ToolManager.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;
using namespace naw::desktop_pet::service;
using namespace naw::desktop_pet::service::tools;

// 优化1: 快速字符串搜索 (Boyer-Moore-Horspool算法的简化版)
class FastStringMatcher {
private:
    std::string pattern;
    std::vector<size_t> badCharShift;
    bool caseSensitive;
    
    void buildBadCharTable() {
        size_t patLen = pattern.length();
        badCharShift.assign(256, patLen);
        
        for (size_t i = 0; i < patLen - 1; ++i) {
            unsigned char c = static_cast<unsigned char>(
                caseSensitive ? pattern[i] : std::tolower(pattern[i])
            );
            badCharShift[c] = patLen - 1 - i;
        }
    }
    
public:
    FastStringMatcher(const std::string& pat, bool cs) 
        : pattern(pat), caseSensitive(cs) {
        if (!caseSensitive) {
            std::transform(pattern.begin(), pattern.end(), pattern.begin(), ::tolower);
        }
        buildBadCharTable();
    }
    
    // 快速字符串搜索
    size_t search(const char* text, size_t textLen) const {
        if (pattern.empty() || textLen < pattern.length()) {
            return std::string::npos;
        }
        
        size_t patLen = pattern.length();
        size_t i = patLen - 1;
        
        while (i < textLen) {
            size_t j = patLen - 1;
            size_t k = i;
            
            while (j < patLen) {
                char textChar = caseSensitive ? text[k] : std::tolower(text[k]);
                if (textChar != pattern[j]) {
                    break;
                }
                if (j == 0) {
                    return k;
                }
                --j;
                --k;
            }
            
            unsigned char bc = static_cast<unsigned char>(
                caseSensitive ? text[i] : std::tolower(text[i])
            );
            i += badCharShift[bc];
        }
        
        return std::string::npos;
    }
};

// 优化2: 批量读取文件内容
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
    std::string content(size, '\0');
    if (!file.read(&content[0], size)) {
        return "";
    }
    
    return content;
}

// 优化3: 在内存中快速搜索所有匹配
static void searchInContent(
    const std::string& content,
    const std::string& query,
    bool useRegex,
    bool caseSensitive,
    const fs::path& filePath,
    std::vector<nlohmann::json>& matches,
    std::mutex& matchesMutex
) {
    std::vector<nlohmann::json> localMatches;
    
    if (useRegex) {
        // 正则表达式搜索
        std::regex searchRegex;
        try {
            searchRegex = caseSensitive ? 
                std::regex(query) : 
                std::regex(query, std::regex::icase);
        } catch (...) {
            return;
        }
        
        size_t lineStart = 0;
        int lineNumber = 1;
        
        for (size_t i = 0; i <= content.length(); ++i) {
            if (i == content.length() || content[i] == '\n') {
                std::string line = content.substr(lineStart, i - lineStart);
                
                try {
                    std::smatch match;
                    if (std::regex_search(line, match, searchRegex)) {
                        nlohmann::json matchResult;
                        matchResult["file"] = pathToUtf8String(filePath);
                        matchResult["line"] = lineNumber;
                        matchResult["column"] = static_cast<int>(match.position()) + 1;
                        matchResult["context"] = line;
                        localMatches.push_back(matchResult);
                    }
                } catch (...) {
                    // 跳过匹配错误
                }
                
                lineStart = i + 1;
                lineNumber++;
            }
        }
    } else {
        // 优化的字符串搜索
        FastStringMatcher matcher(query, caseSensitive);
        
        size_t lineStart = 0;
        int lineNumber = 1;
        
        for (size_t i = 0; i <= content.length(); ++i) {
            if (i == content.length() || content[i] == '\n') {
                size_t lineLen = i - lineStart;
                
                if (lineLen > 0) {
                    size_t pos = matcher.search(content.c_str() + lineStart, lineLen);
                    
                    if (pos != std::string::npos) {
                        std::string line = content.substr(lineStart, lineLen);
                        
                        nlohmann::json matchResult;
                        matchResult["file"] = pathToUtf8String(filePath);
                        matchResult["line"] = lineNumber;
                        matchResult["column"] = static_cast<int>(pos) + 1;
                        matchResult["context"] = line;
                        localMatches.push_back(matchResult);
                    }
                }
                
                lineStart = i + 1;
                lineNumber++;
            }
        }
    }
    
    if (!localMatches.empty()) {
        std::lock_guard<std::mutex> lock(matchesMutex);
        matches.insert(matches.end(), localMatches.begin(), localMatches.end());
    }
}

// 优化4: 收集所有待搜索文件
static std::vector<fs::path> collectFiles(
    const fs::path& dirPath,
    const std::string& filePattern
) {
    std::vector<fs::path> files;
    
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
                
                std::string filename = entry.path().filename().string();
                
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

// 优化5: 并行处理文件
static void processFilesParallel(
    const std::vector<fs::path>& files,
    const std::string& query,
    bool useRegex,
    bool caseSensitive,
    std::vector<nlohmann::json>& matches,
    std::atomic<int>& filesSearched
) {
    std::mutex matchesMutex;
    unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency());
    
    // 对于少量文件，不使用多线程
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
            for (size_t i = startIdx; i < endIdx; ++i) {
                try {
                    std::string content = readFileContent(files[i]);
                    if (content.empty()) {
                        continue;
                    }
                    
                    filesSearched++;
                    searchInContent(content, query, useRegex, caseSensitive, 
                                  files[i], matches, matchesMutex);
                                  
                } catch (...) {
                    // 跳过处理错误的文件
                    continue;
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

static nlohmann::json handleSearchCode(const nlohmann::json& arguments) {
    try {
        // 提取参数
        if (!arguments.contains("query") || !arguments["query"].is_string()) {
            return nlohmann::json{{"error", "缺少必需参数: query"}};
        }
        
        std::string query = arguments["query"].get<std::string>();
        
        std::string directory = ".";
        if (arguments.contains("directory") && arguments["directory"].is_string()) {
            directory = arguments["directory"].get<std::string>();
        }
        
        std::string filePattern;
        if (arguments.contains("file_pattern") && arguments["file_pattern"].is_string()) {
            filePattern = arguments["file_pattern"].get<std::string>();
        }
        
        bool caseSensitive = false;
        if (arguments.contains("case_sensitive") && arguments["case_sensitive"].is_boolean()) {
            caseSensitive = arguments["case_sensitive"].get<bool>();
        }
        
        fs::path dirPath(directory);
        
        // 检查目录是否存在
        if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
            return nlohmann::json{{"error", "目录不存在或不是目录: " + directory}};
        }
        
        // 判断是否使用正则表达式
        bool useRegex = false;
        try {
            std::regex testRegex(query);
            // 如果包含正则特殊字符，使用正则模式
            if (query.find_first_of(".*+?[]{}()^$|\\") != std::string::npos) {
                useRegex = true;
            }
        } catch (...) {
            return nlohmann::json{{"error", "无效的正则表达式"}};
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
        std::atomic<int> filesSearched(0);
        
        processFilesParallel(files, query, useRegex, caseSensitive, 
                           matches, filesSearched);
        
        nlohmann::json result;
        result["matches"] = matches;
        result["total_matches"] = matches.size();
        result["files_searched"] = filesSearched.load();
        
        return result;
        
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", std::string("搜索代码失败: ") + e.what()}};
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