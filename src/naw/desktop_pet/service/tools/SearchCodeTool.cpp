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

namespace fs = std::filesystem;
using namespace naw::desktop_pet::service;
using namespace naw::desktop_pet::service::tools;

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
        
        // 准备正则表达式
        std::regex searchRegex;
        try {
            if (caseSensitive) {
                searchRegex = std::regex(query);
            } else {
                searchRegex = std::regex(query, std::regex::icase);
            }
        } catch (const std::regex_error& e) {
            return nlohmann::json{{"error", std::string("无效的正则表达式: ") + e.what()}};
        }
        
        // 搜索文件
        std::vector<nlohmann::json> matches;
        int filesSearched = 0;
        
        try {
            // 使用选项跳过符号链接，避免无限循环
            fs::recursive_directory_iterator dirIter(dirPath, 
                fs::directory_options::skip_permission_denied);
            
            for (const auto& entry : dirIter) {
                try {
                    // 跳过符号链接，避免无限循环
                    if (fs::is_symlink(entry)) {
                        dirIter.disable_recursion_pending();
                        continue;
                    }
                    
                    if (!fs::is_regular_file(entry)) {
                        continue;
                    }
                    
                    std::string filename = entry.path().filename().string();
                    
                    // 文件类型过滤
                    if (!filePattern.empty() && !matchesPattern(filename, filePattern)) {
                        continue;
                    }
                    
                    filesSearched++;
                    
                    // 检查文件大小
                    if (isFileTooLarge(entry.path())) {
                        continue;
                    }
                    
                    // 逐行搜索
                    std::ifstream file(entry.path(), std::ios::in);
                    if (!file.is_open()) {
                        continue;
                    }
                    
                    std::string line;
                    int lineNumber = 0;
                    while (std::getline(file, line)) {
                        lineNumber++;
                        try {
                            if (std::regex_search(line, searchRegex)) {
                                // 找到匹配
                                size_t pos = line.find(query);
                                if (pos == std::string::npos) {
                                    // 如果是正则表达式，尝试找到第一个匹配位置
                                    std::smatch match;
                                    if (std::regex_search(line, match, searchRegex)) {
                                        pos = match.position();
                                    } else {
                                        pos = 0;
                                    }
                                }
                                
                                nlohmann::json matchResult;
                                // 使用 pathToUtf8String() 确保返回 UTF-8 编码的字符串
                                matchResult["file"] = pathToUtf8String(entry.path());
                                matchResult["line"] = lineNumber;
                                matchResult["column"] = static_cast<int>(pos) + 1; // 1-based
                                matchResult["context"] = line;
                                matches.push_back(matchResult);
                            }
                        } catch (...) {
                            // 跳过正则表达式匹配错误
                            continue;
                        }
                    }
                    file.close();
                } catch (const fs::filesystem_error&) {
                    // 跳过权限错误等文件系统错误
                    dirIter.disable_recursion_pending();
                    continue;
                } catch (...) {
                    // 跳过无法访问的文件
                    continue;
                }
            }
        } catch (const fs::filesystem_error& e) {
            return nlohmann::json{{"error", std::string("搜索文件失败: ") + e.what()}};
        }
        
        nlohmann::json result;
        result["matches"] = matches;
        result["total_matches"] = matches.size();
        result["files_searched"] = filesSearched;
        
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
                {"description", "搜索目录，默认为当前目录"}
            }},
            {"file_pattern", {
                {"type", "string"},
                {"description", "文件类型过滤，如 *.cpp"}
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

