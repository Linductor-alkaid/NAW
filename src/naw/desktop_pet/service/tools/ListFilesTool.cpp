#include "naw/desktop_pet/service/CodeTools.h"
#include "naw/desktop_pet/service/tools/CodeToolsUtils.h"
#include "naw/desktop_pet/service/ToolManager.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace naw::desktop_pet::service;
using namespace naw::desktop_pet::service::tools;

static nlohmann::json handleListFiles(const nlohmann::json& arguments) {
    try {
        // 提取参数
        std::string directory = ".";
        if (arguments.contains("directory") && arguments["directory"].is_string()) {
            directory = arguments["directory"].get<std::string>();
        }
        
        std::string pattern;
        if (arguments.contains("pattern") && arguments["pattern"].is_string()) {
            pattern = arguments["pattern"].get<std::string>();
        }
        
        bool recursive = false;
        if (arguments.contains("recursive") && arguments["recursive"].is_boolean()) {
            recursive = arguments["recursive"].get<bool>();
        }
        
        // 从 UTF-8 字符串构造路径（Windows 上正确处理编码）
        fs::path dirPath = pathFromUtf8String(directory);
        
        // 检查目录是否存在
        if (!fs::exists(dirPath)) {
            return nlohmann::json{{"error", "目录不存在: " + sanitizeUtf8String(directory)}};
        }
        
        if (!fs::is_directory(dirPath)) {
            return nlohmann::json{{"error", "路径不是目录: " + sanitizeUtf8String(directory)}};
        }
        
        // 收集文件和目录
        std::vector<std::string> files;
        std::vector<std::string> directories;
        uintmax_t totalSize = 0;
        
        // 辅助函数：获取路径表示（返回UTF-8编码的字符串）
        // 非递归模式：只返回文件名
        // 递归模式：返回相对于 dirPath 的相对路径（清理 ".\" 前缀）
        auto getPathString = [&dirPath, recursive](const fs::path& entryPath) -> std::string {
            if (!recursive) {
                // 非递归模式：只返回文件名（转换为UTF-8）
                std::string filename = pathToUtf8String(entryPath.filename());
                return sanitizeUtf8String(filename);
            } else {
                // 递归模式：返回相对路径（转换为UTF-8）
                try {
                    fs::path relative = fs::relative(entryPath, dirPath);
                    if (!relative.empty() && relative != entryPath) {
                        std::string relStr = pathToUtf8String(relative);
                        // 移除开头的 ".\" 或 "./"
                        if (relStr.size() >= 2 && relStr[0] == '.' && (relStr[1] == '\\' || relStr[1] == '/')) {
                            return sanitizeUtf8String(relStr.substr(2));
                        }
                        return sanitizeUtf8String(relStr);
                    }
                } catch (...) {
                    // 如果获取相对路径失败，使用文件名
                }
                std::string filename = pathToUtf8String(entryPath.filename());
                return sanitizeUtf8String(filename);
            }
        };
        
        try {
            if (recursive) {
                fs::recursive_directory_iterator dirIter(dirPath, 
                    fs::directory_options::skip_permission_denied);
                for (const auto& entry : dirIter) {
                    try {
                        // 跳过符号链接，避免无限循环
                        if (fs::is_symlink(entry)) {
                            dirIter.disable_recursion_pending();
                            continue;
                        }
                        
                        if (fs::is_regular_file(entry)) {
                            // 使用UTF-8编码的文件名进行模式匹配
                            std::string filename = pathToUtf8String(entry.path().filename());
                            if (pattern.empty() || matchesPattern(filename, pattern)) {
                                files.push_back(getPathString(entry.path()));
                                totalSize += fs::file_size(entry);
                            }
                        } else if (fs::is_directory(entry)) {
                            directories.push_back(getPathString(entry.path()));
                        }
                    } catch (const fs::filesystem_error&) {
                        // 跳过权限错误等文件系统错误
                        dirIter.disable_recursion_pending();
                        continue;
                    } catch (...) {
                        // 跳过无法访问的文件
                        continue;
                    }
                }
            } else {
                for (const auto& entry : fs::directory_iterator(dirPath)) {
                    try {
                        if (fs::is_regular_file(entry)) {
                            // 使用UTF-8编码的文件名进行模式匹配
                            std::string filename = pathToUtf8String(entry.path().filename());
                            if (pattern.empty() || matchesPattern(filename, pattern)) {
                                files.push_back(getPathString(entry.path()));
                                totalSize += fs::file_size(entry);
                            }
                        } else if (fs::is_directory(entry)) {
                            directories.push_back(getPathString(entry.path()));
                        }
                    } catch (...) {
                        // 跳过无法访问的文件
                        continue;
                    }
                }
            }
        } catch (const fs::filesystem_error& e) {
            std::string errorMsg = std::string("遍历目录失败: ") + e.what();
            return nlohmann::json{{"error", sanitizeUtf8String(errorMsg)}};
        }
        
        nlohmann::json result;
        result["files"] = files;  // 文件路径已经过 sanitizeUtf8String 处理
        result["directories"] = directories;  // 目录路径已经过 sanitizeUtf8String 处理
        result["count"] = files.size();
        result["total_size"] = totalSize;
        
        return result;
        
    } catch (const std::exception& e) {
        std::string errorMsg = std::string("列出文件失败: ") + e.what();
        return nlohmann::json{{"error", sanitizeUtf8String(errorMsg)}};
    } catch (...) {
        return nlohmann::json{{"error", "列出文件时发生未知错误"}};
    }
}

void CodeTools::registerListFilesTool(ToolManager& toolManager) {
    ToolDefinition tool;
    tool.name = "list_files";
    tool.description = "列出目录中的文件和子目录。支持递归遍历和文件模式过滤。";
    tool.parametersSchema = nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"directory", {
                {"type", "string"},
                {"description", "目录路径，默认为当前目录"}
            }},
            {"pattern", {
                {"type", "string"},
                {"description", "文件匹配模式，如 *.cpp"}
            }},
            {"recursive", {
                {"type", "boolean"},
                {"default", false},
                {"description", "是否递归遍历子目录"}
            }}
        }}
    };
    tool.handler = handleListFiles;
    tool.permissionLevel = PermissionLevel::Public;
    
    toolManager.registerTool(tool, true);
}

