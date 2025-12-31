#include "naw/desktop_pet/service/CodeTools.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace naw::desktop_pet::service {

// ========== 辅助函数 ==========

namespace {
    // 最大文件大小限制（10MB）
    constexpr size_t MAX_FILE_SIZE = 10 * 1024 * 1024;

    /**
     * @brief 检查文件大小是否超过限制
     */
    bool isFileTooLarge(const fs::path& path) {
        try {
            if (fs::exists(path) && fs::is_regular_file(path)) {
                auto size = fs::file_size(path);
                return size > MAX_FILE_SIZE;
            }
        } catch (...) {
            // 忽略错误，让后续处理
        }
        return false;
    }

    /**
     * @brief 将通配符模式转换为正则表达式
     */
    std::string wildcardToRegex(const std::string& pattern) {
        std::string regex;
        regex.reserve(pattern.size() * 2);
        for (char c : pattern) {
            if (c == '*') {
                regex += ".*";
            } else if (c == '?') {
                regex += ".";
            } else if (c == '.' || c == '+' || c == '(' || c == ')' || c == '[' || c == ']' || 
                     c == '{' || c == '}' || c == '^' || c == '$' || c == '|' || c == '\\') {
                regex += '\\';
                regex += c;
            } else {
                regex += c;
            }
        }
        return regex;
    }

    /**
     * @brief 检查文件名是否匹配模式
     */
    bool matchesPattern(const std::string& filename, const std::string& pattern) {
        if (pattern.empty()) {
            return true;
        }
        try {
            std::string regexPattern = wildcardToRegex(pattern);
            std::regex re(regexPattern, std::regex::icase);
            return std::regex_match(filename, re);
        } catch (...) {
            return false;
        }
    }

    /**
     * @brief 读取文件的指定行范围
     */
    std::vector<std::string> readFileLines(const fs::path& path, int startLine = 0, int endLine = -1) {
        std::vector<std::string> lines;
        std::ifstream file(path, std::ios::in);
        
        if (!file.is_open()) {
            throw std::runtime_error("无法打开文件: " + path.string());
        }

        std::string line;
        int currentLine = 0;
        
        while (std::getline(file, line)) {
            currentLine++;
            if (startLine > 0 && currentLine < startLine) {
                continue;
            }
            if (endLine > 0 && currentLine > endLine) {
                break;
            }
            lines.push_back(line);
        }
        
        return lines;
    }

    /**
     * @brief 统计文件总行数
     */
    int countFileLines(const fs::path& path) {
        std::ifstream file(path, std::ios::in);
        if (!file.is_open()) {
            return 0;
        }
        
        int count = 0;
        std::string line;
        while (std::getline(file, line)) {
            count++;
        }
        return count;
    }
}

// ========== read_file 工具实现 ==========

static nlohmann::json handleReadFile(const nlohmann::json& arguments) {
    try {
        // 提取参数
        if (!arguments.contains("path") || !arguments["path"].is_string()) {
            return nlohmann::json{{"error", "缺少必需参数: path"}};
        }
        
        std::string pathStr = arguments["path"].get<std::string>();
        fs::path filePath(pathStr);
        
        // 检查文件是否存在
        if (!fs::exists(filePath)) {
            return nlohmann::json{{"error", "文件不存在: " + pathStr}};
        }
        
        if (!fs::is_regular_file(filePath)) {
            return nlohmann::json{{"error", "路径不是文件: " + pathStr}};
        }
        
        // 检查文件大小
        if (isFileTooLarge(filePath)) {
            return nlohmann::json{{"error", "文件过大（超过10MB）: " + pathStr}};
        }
        
        // 提取行范围参数
        int startLine = 0;
        int endLine = -1;
        
        if (arguments.contains("start_line") && arguments["start_line"].is_number_integer()) {
            startLine = arguments["start_line"].get<int>();
        }
        if (arguments.contains("end_line") && arguments["end_line"].is_number_integer()) {
            endLine = arguments["end_line"].get<int>();
        }
        
        // 验证行范围
        int totalLines = countFileLines(filePath);
        if (startLine > 0 && startLine > totalLines) {
            return nlohmann::json{{"error", "起始行号超出文件范围"}};
        }
        if (endLine > 0 && endLine < startLine) {
            return nlohmann::json{{"error", "结束行号小于起始行号"}};
        }
        
        // 读取文件
        std::vector<std::string> lines;
        if (startLine > 0 || endLine > 0) {
            lines = readFileLines(filePath, startLine, endLine);
        } else {
            lines = readFileLines(filePath);
        }
        
        // 构建结果
        std::ostringstream content;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i > 0) content << "\n";
            content << lines[i];
        }
        
        nlohmann::json result;
        result["content"] = content.str();
        result["path"] = pathStr;
        result["line_count"] = totalLines;
        
        if (startLine > 0) {
            result["start_line"] = startLine;
        }
        if (endLine > 0) {
            result["end_line"] = endLine;
        }
        
        return result;
        
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", std::string("读取文件失败: ") + e.what()}};
    } catch (...) {
        return nlohmann::json{{"error", "读取文件时发生未知错误"}};
    }
}

void CodeTools::registerReadFileTool(ToolManager& toolManager) {
    ToolDefinition tool;
    tool.name = "read_file";
    tool.description = "读取文本文件内容。支持读取完整文件或指定行范围。";
    tool.parametersSchema = nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "文件路径"}
            }},
            {"start_line", {
                {"type", "integer"},
                {"minimum", 1},
                {"description", "起始行号（从1开始）"}
            }},
            {"end_line", {
                {"type", "integer"},
                {"minimum", 1},
                {"description", "结束行号"}
            }}
        }},
        {"required", {"path"}}
    };
    tool.handler = handleReadFile;
    tool.permissionLevel = PermissionLevel::Public;
    
    toolManager.registerTool(tool, true);
}

// ========== write_file 工具实现 ==========

static nlohmann::json handleWriteFile(const nlohmann::json& arguments) {
    try {
        // 提取参数
        if (!arguments.contains("path") || !arguments["path"].is_string()) {
            return nlohmann::json{{"error", "缺少必需参数: path"}};
        }
        if (!arguments.contains("content") || !arguments["content"].is_string()) {
            return nlohmann::json{{"error", "缺少必需参数: content"}};
        }
        
        std::string pathStr = arguments["path"].get<std::string>();
        std::string content = arguments["content"].get<std::string>();
        fs::path filePath(pathStr);
        
        // 提取可选参数
        std::string mode = "overwrite";
        if (arguments.contains("mode") && arguments["mode"].is_string()) {
            mode = arguments["mode"].get<std::string>();
        }
        
        bool createDirs = false;
        if (arguments.contains("create_directories") && arguments["create_directories"].is_boolean()) {
            createDirs = arguments["create_directories"].get<bool>();
        }
        
        int startLine = 0;
        int endLine = -1;
        if (arguments.contains("start_line") && arguments["start_line"].is_number_integer()) {
            startLine = arguments["start_line"].get<int>();
        }
        if (arguments.contains("end_line") && arguments["end_line"].is_number_integer()) {
            endLine = arguments["end_line"].get<int>();
        }
        
        // 检查是否需要创建目录
        if (createDirs) {
            auto parentPath = filePath.parent_path();
            if (!parentPath.empty() && !fs::exists(parentPath)) {
                fs::create_directories(parentPath);
            }
        }
        
        // 处理行范围替换
        if (startLine > 0 && endLine > 0) {
            // 行范围替换模式
            if (!fs::exists(filePath)) {
                return nlohmann::json{{"error", "文件不存在，无法进行行范围替换"}};
            }
            
            // 读取原文件
            std::vector<std::string> lines = readFileLines(filePath);
            int totalLines = static_cast<int>(lines.size());
            
            // 验证行范围
            if (startLine > totalLines || endLine > totalLines) {
                return nlohmann::json{{"error", "行范围超出文件范围"}};
            }
            
            // 替换指定行
            std::vector<std::string> newContentLines;
            std::istringstream contentStream(content);
            std::string line;
            while (std::getline(contentStream, line)) {
                newContentLines.push_back(line);
            }
            
            // 构建新文件内容
            std::vector<std::string> newLines;
            for (int i = 0; i < totalLines; ++i) {
                int lineNum = i + 1; // 1-based
                if (lineNum >= startLine && lineNum <= endLine) {
                    // 替换范围内的行
                    int replaceIndex = lineNum - startLine;
                    if (replaceIndex < static_cast<int>(newContentLines.size())) {
                        newLines.push_back(newContentLines[replaceIndex]);
                    }
                } else {
                    newLines.push_back(lines[i]);
                }
            }
            
            // 写入文件
            std::ofstream file(filePath, std::ios::out | std::ios::trunc);
            if (!file.is_open()) {
                return nlohmann::json{{"error", "无法打开文件进行写入: " + pathStr}};
            }
            
            size_t bytesWritten = 0;
            for (size_t i = 0; i < newLines.size(); ++i) {
                if (i > 0) {
                    file << "\n";
                    bytesWritten += 1;
                }
                file << newLines[i];
                bytesWritten += newLines[i].size();
            }
            file.close();
            
            nlohmann::json result;
            result["success"] = true;
            result["path"] = pathStr;
            result["bytes_written"] = bytesWritten;
            result["mode"] = "line_replace";
            result["message"] = "成功替换行范围 " + std::to_string(startLine) + "-" + std::to_string(endLine);
            return result;
        }
        
        // 普通写入模式
        std::ios_base::openmode openMode = std::ios::out;
        if (mode == "append") {
            openMode |= std::ios::app;
        } else if (mode == "create_only") {
            if (fs::exists(filePath)) {
                return nlohmann::json{{"error", "文件已存在，无法使用 create_only 模式"}};
            }
            openMode |= std::ios::trunc;
        } else {
            // overwrite
            openMode |= std::ios::trunc;
        }
        
        std::ofstream file(filePath, openMode);
        if (!file.is_open()) {
            return nlohmann::json{{"error", "无法打开文件进行写入: " + pathStr}};
        }
        
        file << content;
        size_t bytesWritten = content.size();
        file.close();
        
        nlohmann::json result;
        result["success"] = true;
        result["path"] = pathStr;
        result["bytes_written"] = bytesWritten;
        result["mode"] = mode;
        result["message"] = "文件写入成功";
        return result;
        
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", std::string("写入文件失败: ") + e.what()}};
    } catch (...) {
        return nlohmann::json{{"error", "写入文件时发生未知错误"}};
    }
}

void CodeTools::registerWriteFileTool(ToolManager& toolManager) {
    ToolDefinition tool;
    tool.name = "write_file";
    tool.description = "写入文本文件。支持覆盖、追加、仅创建等模式，以及行范围替换。";
    tool.parametersSchema = nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "文件路径"}
            }},
            {"content", {
                {"type", "string"},
                {"description", "要写入的内容"}
            }},
            {"mode", {
                {"type", "string"},
                {"enum", {"overwrite", "append", "create_only"}},
                {"default", "overwrite"},
                {"description", "写入模式"}
            }},
            {"start_line", {
                {"type", "integer"},
                {"minimum", 1},
                {"description", "起始行号（用于行范围替换）"}
            }},
            {"end_line", {
                {"type", "integer"},
                {"minimum", 1},
                {"description", "结束行号（用于行范围替换）"}
            }},
            {"create_directories", {
                {"type", "boolean"},
                {"default", false},
                {"description", "是否自动创建目录"}
            }}
        }},
        {"required", {"path", "content"}}
    };
    tool.handler = handleWriteFile;
    tool.permissionLevel = PermissionLevel::Public;
    
    toolManager.registerTool(tool, true);
}

// ========== list_files 工具实现 ==========

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
        
        fs::path dirPath(directory);
        
        // 检查目录是否存在
        if (!fs::exists(dirPath)) {
            return nlohmann::json{{"error", "目录不存在: " + directory}};
        }
        
        if (!fs::is_directory(dirPath)) {
            return nlohmann::json{{"error", "路径不是目录: " + directory}};
        }
        
        // 收集文件和目录
        std::vector<std::string> files;
        std::vector<std::string> directories;
        uintmax_t totalSize = 0;
        
        // 辅助函数：获取路径表示
        // 非递归模式：只返回文件名
        // 递归模式：返回相对于 dirPath 的相对路径（清理 ".\" 前缀）
        auto getPathString = [&dirPath, recursive](const fs::path& entryPath) -> std::string {
            if (!recursive) {
                // 非递归模式：只返回文件名
                return entryPath.filename().string();
            } else {
                // 递归模式：返回相对路径
                try {
                    fs::path relative = fs::relative(entryPath, dirPath);
                    if (!relative.empty() && relative != entryPath) {
                        std::string relStr = relative.string();
                        // 移除开头的 ".\" 或 "./"
                        if (relStr.size() >= 2 && relStr[0] == '.' && (relStr[1] == '\\' || relStr[1] == '/')) {
                            return relStr.substr(2);
                        }
                        return relStr;
                    }
                } catch (...) {
                    // 如果获取相对路径失败，使用文件名
                }
                return entryPath.filename().string();
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
                            std::string filename = entry.path().filename().string();
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
                            std::string filename = entry.path().filename().string();
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
            return nlohmann::json{{"error", std::string("遍历目录失败: ") + e.what()}};
        }
        
        nlohmann::json result;
        result["files"] = files;
        result["directories"] = directories;
        result["count"] = files.size();
        result["total_size"] = totalSize;
        
        return result;
        
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", std::string("列出文件失败: ") + e.what()}};
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

// ========== search_code 工具实现 ==========

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
                                matchResult["file"] = entry.path().string();
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

// ========== get_project_structure 工具实现 ==========

namespace {
    /**
     * @brief 检测项目根目录
     */
    fs::path detectProjectRoot(const fs::path& startPath) {
        fs::path current = fs::absolute(startPath);
        
        // 如果路径是文件，获取其父目录
        if (fs::exists(current) && fs::is_regular_file(current)) {
            current = current.parent_path();
        }
        
        // 向上遍历查找项目标识（限制最大深度，避免无限循环）
        int maxDepth = 20; // 最多向上查找20层
        int depth = 0;
        while (!current.empty() && current != current.root_path() && depth < maxDepth) {
            try {
                // 检查常见的项目标识文件
                if (fs::exists(current / ".git") ||
                    fs::exists(current / "CMakeLists.txt") ||
                    fs::exists(current / ".project") ||
                    fs::exists(current / "package.json") ||
                    fs::exists(current / "pyproject.toml") ||
                    fs::exists(current / "setup.py")) {
                    return current;
                }
            } catch (...) {
                // 如果检查文件存在时出错，继续向上查找
            }
            
            fs::path parent = current.parent_path();
            if (parent == current) {
                // 已经到达根目录
                break;
            }
            current = parent;
            depth++;
        }
        
        // 如果没找到，返回起始路径的绝对路径
        return fs::absolute(startPath);
    }
    
    /**
     * @brief 解析 CMakeLists.txt
     */
    nlohmann::json parseCMakeLists(const fs::path& cmakePath) {
        nlohmann::json result;
        result["project_name"] = "";
        result["targets"] = nlohmann::json::array();
        result["dependencies"] = nlohmann::json::array();
        
        if (!fs::exists(cmakePath) || !fs::is_regular_file(cmakePath)) {
            return result;
        }
        
        try {
            std::ifstream file(cmakePath, std::ios::in);
            if (!file.is_open()) {
                return result;
            }
            
            std::string line;
            std::regex projectRegex(R"(project\s*\(\s*(\w+))");
            std::regex addExecutableRegex(R"(add_executable\s*\(\s*(\w+))");
            std::regex addLibraryRegex(R"(add_library\s*\(\s*(\w+))");
            std::regex targetLinkLibrariesRegex(R"(target_link_libraries\s*\(\s*(\w+)\s+(.+)\))");
            std::regex findPackageRegex(R"(find_package\s*\(\s*(\w+))");
            
            while (std::getline(file, line)) {
                // 移除注释
                size_t commentPos = line.find('#');
                if (commentPos != std::string::npos) {
                    line = line.substr(0, commentPos);
                }
                
                std::smatch match;
                
                // 解析 project()
                if (std::regex_search(line, match, projectRegex)) {
                    result["project_name"] = match[1].str();
                }
                
                // 解析 add_executable()
                if (std::regex_search(line, match, addExecutableRegex)) {
                    result["targets"].push_back(match[1].str());
                }
                
                // 解析 add_library()
                if (std::regex_search(line, match, addLibraryRegex)) {
                    result["targets"].push_back(match[1].str());
                }
                
                // 解析 target_link_libraries()
                if (std::regex_search(line, match, targetLinkLibrariesRegex)) {
                    std::string deps = match[2].str();
                    // 简单分割依赖（实际应该更复杂）
                    std::istringstream iss(deps);
                    std::string dep;
                    while (iss >> dep) {
                        if (!dep.empty()) {
                            result["dependencies"].push_back(dep);
                        }
                    }
                }
                
                // 解析 find_package()
                if (std::regex_search(line, match, findPackageRegex)) {
                    result["dependencies"].push_back(match[1].str());
                }
            }
            
            file.close();
        } catch (...) {
            // 解析失败，返回空结果
        }
        
        return result;
    }
}

static nlohmann::json handleGetProjectStructure(const nlohmann::json& arguments) {
    try {
        // 提取参数
        bool includeFiles = true;
        if (arguments.contains("include_files") && arguments["include_files"].is_boolean()) {
            includeFiles = arguments["include_files"].get<bool>();
        }
        
        bool includeDependencies = true;
        if (arguments.contains("include_dependencies") && arguments["include_dependencies"].is_boolean()) {
            includeDependencies = arguments["include_dependencies"].get<bool>();
        }
        
        fs::path projectRoot;
        if (arguments.contains("project_root") && arguments["project_root"].is_string()) {
            projectRoot = fs::path(arguments["project_root"].get<std::string>());
        } else {
            // 自动检测
            projectRoot = detectProjectRoot(fs::current_path());
        }
        
        projectRoot = fs::absolute(projectRoot);
        
        if (!fs::exists(projectRoot)) {
            return nlohmann::json{{"error", "项目根目录不存在: " + projectRoot.string()}};
        }
        
        // 构建结果
        nlohmann::json result;
        result["root_path"] = projectRoot.string();
        result["project_name"] = "";
        
        // 解析 CMakeLists.txt
        fs::path cmakePath = projectRoot / "CMakeLists.txt";
        nlohmann::json cmakeConfig = parseCMakeLists(cmakePath);
        if (includeDependencies) {
            result["cmake_config"] = cmakeConfig;
            if (!cmakeConfig["project_name"].empty()) {
                result["project_name"] = cmakeConfig["project_name"];
            }
        }
        
        // 收集文件列表
        std::vector<std::string> sourceFiles;
        std::vector<std::string> headerFiles;
        
        if (includeFiles) {
            try {
                fs::recursive_directory_iterator dirIter(projectRoot, 
                    fs::directory_options::skip_permission_denied);
                for (const auto& entry : dirIter) {
                    try {
                        // 跳过符号链接，避免无限循环
                        if (fs::is_symlink(entry)) {
                            dirIter.disable_recursion_pending();
                            continue;
                        }
                        
                        if (fs::is_regular_file(entry)) {
                            std::string ext = entry.path().extension().string();
                            std::string pathStr = entry.path().string();
                            
                            // C++ 源文件
                            if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c") {
                                sourceFiles.push_back(pathStr);
                            }
                            // C++ 头文件
                            else if (ext == ".h" || ext == ".hpp" || ext == ".hxx") {
                                headerFiles.push_back(pathStr);
                            }
                        }
                    } catch (const fs::filesystem_error&) {
                        // 跳过权限错误等文件系统错误
                        dirIter.disable_recursion_pending();
                        continue;
                    } catch (...) {
                        continue;
                    }
                }
            } catch (...) {
                // 忽略遍历错误
            }
        }
        
        result["source_files"] = sourceFiles;
        result["header_files"] = headerFiles;
        
        // 提取依赖
        if (includeDependencies && cmakeConfig.contains("dependencies")) {
            result["dependencies"] = cmakeConfig["dependencies"];
        } else {
            result["dependencies"] = nlohmann::json::array();
        }
        
        // 构建简单的目录结构树（字符串格式）
        // 简化实现，避免复杂的深度计算导致性能问题
        std::ostringstream structure;
        try {
            fs::recursive_directory_iterator dirIter(projectRoot, 
                fs::directory_options::skip_permission_denied);
            std::vector<fs::path> paths;
            
            // 先收集所有路径（限制数量，避免过大）
            const size_t MAX_PATHS = 1000;
            for (const auto& entry : dirIter) {
                try {
                    // 跳过符号链接，避免无限循环
                    if (fs::is_symlink(entry)) {
                        dirIter.disable_recursion_pending();
                        continue;
                    }
                    
                    paths.push_back(entry.path());
                    if (paths.size() >= MAX_PATHS) {
                        break; // 限制路径数量，避免卡死
                    }
                } catch (const fs::filesystem_error&) {
                    // 跳过权限错误等文件系统错误
                    dirIter.disable_recursion_pending();
                    continue;
                } catch (...) {
                    continue;
                }
            }
            
            // 构建简化的目录结构
            for (const auto& path : paths) {
                try {
                    // 计算相对路径
                    fs::path relPath = fs::relative(path, projectRoot);
                    if (relPath.empty() || relPath == ".") {
                        continue; // 跳过根目录本身
                    }
                    
                    structure << relPath.string();
                    if (fs::is_directory(path)) {
                        structure << "/";
                    }
                    structure << "\n";
                } catch (...) {
                    // 如果无法计算相对路径，使用文件名
                    structure << path.filename().string();
                    if (fs::is_directory(path)) {
                        structure << "/";
                    }
                    structure << "\n";
                }
            }
        } catch (...) {
            structure << "无法构建目录结构";
        }
        result["structure"] = structure.str();
        
        return result;
        
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", std::string("获取项目结构失败: ") + e.what()}};
    } catch (...) {
        return nlohmann::json{{"error", "获取项目结构时发生未知错误"}};
    }
}

void CodeTools::registerGetProjectStructureTool(ToolManager& toolManager) {
    ToolDefinition tool;
    tool.name = "get_project_structure";
    tool.description = "分析项目结构，包括目录结构、源文件列表、CMake配置和依赖关系。";
    tool.parametersSchema = nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"include_files", {
                {"type", "boolean"},
                {"default", true},
                {"description", "是否包含文件列表"}
            }},
            {"include_dependencies", {
                {"type", "boolean"},
                {"default", true},
                {"description", "是否包含依赖关系"}
            }},
            {"project_root", {
                {"type", "string"},
                {"description", "项目根路径，默认自动检测"}
            }}
        }}
    };
    tool.handler = handleGetProjectStructure;
    tool.permissionLevel = PermissionLevel::Public;
    
    toolManager.registerTool(tool, true);
}

// ========== analyze_code 工具实现 ==========

namespace {
    /**
     * @brief 分析 C++ 代码
     */
    nlohmann::json analyzeCppCode(const fs::path& filePath, const std::string& analysisType) {
        nlohmann::json result;
        result["path"] = filePath.string();
        result["language"] = "cpp";
        result["functions"] = nlohmann::json::array();
        result["classes"] = nlohmann::json::array();
        result["includes"] = nlohmann::json::array();
        
        try {
            std::ifstream file(filePath, std::ios::in);
            if (!file.is_open()) {
                return result;
            }
            
            std::string line;
            int lineNumber = 0;
            
            // 正则表达式
            std::regex includeRegex(R"(#include\s*[<"]([^>"]+)[>"])");
            std::regex functionRegex(R"((?:(?:inline|static|constexpr|virtual|explicit)\s+)*\w+\s+\w+\s*\([^)]*\)\s*(?:const\s*)?\{)");
            std::regex classRegex(R"(class\s+(\w+)(?:\s*:\s*(?:public|private|protected)\s+\w+)?\s*\{)");
            std::regex methodRegex(R"((?:(?:inline|static|constexpr|virtual|explicit)\s+)*\w+\s+(\w+)\s*\([^)]*\)\s*(?:const\s*)?(?:;|\{))");
            
            std::string currentClass;
            int currentClassLine = 0;
            std::vector<std::string> currentClassMethods;
            
            while (std::getline(file, line)) {
                lineNumber++;
                
                // 移除注释
                size_t commentPos = line.find("//");
                if (commentPos != std::string::npos) {
                    line = line.substr(0, commentPos);
                }
                
                // 提取 #include
                if (analysisType == "dependencies" || analysisType == "all") {
                    std::smatch match;
                    if (std::regex_search(line, match, includeRegex)) {
                        result["includes"].push_back(match[1].str());
                    }
                }
                
                // 提取类定义
                if (analysisType == "classes" || analysisType == "all") {
                    std::smatch match;
                    if (std::regex_search(line, match, classRegex)) {
                        // 保存之前的类
                        if (!currentClass.empty()) {
                            nlohmann::json classInfo;
                            classInfo["name"] = currentClass;
                            classInfo["line"] = currentClassLine;
                            classInfo["methods"] = currentClassMethods;
                            result["classes"].push_back(classInfo);
                        }
                        
                        currentClass = match[1].str();
                        currentClassLine = lineNumber;
                        currentClassMethods.clear();
                    }
                    
                    // 提取类方法
                    if (!currentClass.empty() && std::regex_search(line, match, methodRegex)) {
                        currentClassMethods.push_back(match[1].str());
                    }
                }
                
                // 提取函数定义（简单版本，不考虑命名空间等复杂情况）
                if (analysisType == "functions" || analysisType == "all") {
                    std::smatch match;
                    if (std::regex_search(line, match, functionRegex)) {
                        nlohmann::json funcInfo;
                        funcInfo["name"] = "function_" + std::to_string(lineNumber); // 简化处理
                        funcInfo["signature"] = line;
                        funcInfo["line"] = lineNumber;
                        result["functions"].push_back(funcInfo);
                    }
                }
            }
            
            // 保存最后一个类
            if (!currentClass.empty() && (analysisType == "classes" || analysisType == "all")) {
                nlohmann::json classInfo;
                classInfo["name"] = currentClass;
                classInfo["line"] = currentClassLine;
                classInfo["methods"] = currentClassMethods;
                result["classes"].push_back(classInfo);
            }
            
            file.close();
        } catch (...) {
            // 分析失败，返回已有结果
        }
        
        return result;
    }
    
    /**
     * @brief 分析 Python 代码
     */
    nlohmann::json analyzePythonCode(const fs::path& filePath, const std::string& analysisType) {
        nlohmann::json result;
        result["path"] = filePath.string();
        result["language"] = "python";
        result["functions"] = nlohmann::json::array();
        result["classes"] = nlohmann::json::array();
        result["includes"] = nlohmann::json::array();
        
        try {
            std::ifstream file(filePath, std::ios::in);
            if (!file.is_open()) {
                return result;
            }
            
            std::string line;
            int lineNumber = 0;
            
            std::regex importRegex(R"(import\s+(\w+)|from\s+(\w+)\s+import)");
            std::regex functionRegex(R"(def\s+(\w+)\s*\()");
            std::regex classRegex(R"(class\s+(\w+)(?:\([^)]+\))?\s*:)");
            
            std::string currentClass;
            int currentClassLine = 0;
            std::vector<std::string> currentClassMethods;
            
            while (std::getline(file, line)) {
                lineNumber++;
                
                // 移除注释
                size_t commentPos = line.find('#');
                if (commentPos != std::string::npos) {
                    line = line.substr(0, commentPos);
                }
                
                // 提取 import
                if (analysisType == "dependencies" || analysisType == "all") {
                    std::smatch match;
                    if (std::regex_search(line, match, importRegex)) {
                        std::string module = match[1].str();
                        if (module.empty()) {
                            module = match[2].str();
                        }
                        if (!module.empty()) {
                            result["includes"].push_back(module);
                        }
                    }
                }
                
                // 提取类定义
                if (analysisType == "classes" || analysisType == "all") {
                    std::smatch match;
                    if (std::regex_search(line, match, classRegex)) {
                        // 保存之前的类
                        if (!currentClass.empty()) {
                            nlohmann::json classInfo;
                            classInfo["name"] = currentClass;
                            classInfo["line"] = currentClassLine;
                            classInfo["methods"] = currentClassMethods;
                            result["classes"].push_back(classInfo);
                        }
                        
                        currentClass = match[1].str();
                        currentClassLine = lineNumber;
                        currentClassMethods.clear();
                    }
                    
                    // 提取类方法（在类内部定义的函数）
                    if (!currentClass.empty()) {
                        std::smatch match;
                        if (std::regex_search(line, match, functionRegex)) {
                            currentClassMethods.push_back(match[1].str());
                        }
                    }
                }
                
                // 提取函数定义（不在类内部的）
                if (analysisType == "functions" || analysisType == "all") {
                    if (currentClass.empty()) {
                        std::smatch match;
                        if (std::regex_search(line, match, functionRegex)) {
                            nlohmann::json funcInfo;
                            funcInfo["name"] = match[1].str();
                            funcInfo["signature"] = line;
                            funcInfo["line"] = lineNumber;
                            result["functions"].push_back(funcInfo);
                        }
                    }
                }
            }
            
            // 保存最后一个类
            if (!currentClass.empty() && (analysisType == "classes" || analysisType == "all")) {
                nlohmann::json classInfo;
                classInfo["name"] = currentClass;
                classInfo["line"] = currentClassLine;
                classInfo["methods"] = currentClassMethods;
                result["classes"].push_back(classInfo);
            }
            
            file.close();
        } catch (...) {
            // 分析失败，返回已有结果
        }
        
        return result;
    }
}

static nlohmann::json handleAnalyzeCode(const nlohmann::json& arguments) {
    try {
        // 提取参数
        if (!arguments.contains("path") || !arguments["path"].is_string()) {
            return nlohmann::json{{"error", "缺少必需参数: path"}};
        }
        
        std::string pathStr = arguments["path"].get<std::string>();
        fs::path filePath(pathStr);
        
        std::string analysisType = "all";
        if (arguments.contains("analysis_type") && arguments["analysis_type"].is_string()) {
            analysisType = arguments["analysis_type"].get<std::string>();
        }
        
        // 验证分析类型
        if (analysisType != "functions" && analysisType != "classes" && 
            analysisType != "dependencies" && analysisType != "all") {
            return nlohmann::json{{"error", "无效的分析类型: " + analysisType}};
        }
        
        // 检查文件是否存在
        if (!fs::exists(filePath)) {
            return nlohmann::json{{"error", "文件不存在: " + pathStr}};
        }
        
        if (!fs::is_regular_file(filePath)) {
            return nlohmann::json{{"error", "路径不是文件: " + pathStr}};
        }
        
        // 根据文件扩展名选择分析器
        std::string ext = filePath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" || 
            ext == ".h" || ext == ".hpp" || ext == ".hxx") {
            return analyzeCppCode(filePath, analysisType);
        } else if (ext == ".py") {
            return analyzePythonCode(filePath, analysisType);
        } else {
            return nlohmann::json{{"error", "不支持的文件类型: " + ext}};
        }
        
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", std::string("分析代码失败: ") + e.what()}};
    } catch (...) {
        return nlohmann::json{{"error", "分析代码时发生未知错误"}};
    }
}

void CodeTools::registerAnalyzeCodeTool(ToolManager& toolManager) {
    ToolDefinition tool;
    tool.name = "analyze_code";
    tool.description = "分析代码文件，提取函数定义、类定义和依赖关系。支持 C++ 和 Python。";
    tool.parametersSchema = nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "代码文件路径"}
            }},
            {"analysis_type", {
                {"type", "string"},
                {"enum", {"functions", "classes", "dependencies", "all"}},
                {"default", "all"},
                {"description", "分析类型"}
            }}
        }},
        {"required", {"path"}}
    };
    tool.handler = handleAnalyzeCode;
    tool.permissionLevel = PermissionLevel::Public;
    
    toolManager.registerTool(tool, true);
}

// ========== 统一工具注册接口 ==========

void CodeTools::registerAllTools(ToolManager& toolManager) {
    registerReadFileTool(toolManager);
    registerWriteFileTool(toolManager);
    registerListFilesTool(toolManager);
    registerSearchCodeTool(toolManager);
    registerGetProjectStructureTool(toolManager);
    registerAnalyzeCodeTool(toolManager);
}

} // namespace naw::desktop_pet::service

