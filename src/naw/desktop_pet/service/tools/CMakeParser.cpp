#include "naw/desktop_pet/service/tools/CMakeParser.h"
#include "naw/desktop_pet/service/tools/CodeToolsUtils.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using namespace naw::desktop_pet::service::tools;

namespace {
    // 计算字符串的简单哈希（用于缓存失效检测）
    std::string computeStringHash(const std::string& str) {
        std::hash<std::string> hasher;
        size_t hash = hasher(str);
        std::ostringstream oss;
        oss << std::hex << hash;
        return oss.str();
    }
    
    // 读取文件内容
    std::string readFileContent(const fs::path& filePath) {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            return "";
        }
        
        std::ostringstream oss;
        oss << file.rdbuf();
        return oss.str();
    }
}

CMakeProjectInfo CMakeParser::parseCMakeLists(const fs::path& cmakePath, const fs::path& projectRoot) {
    CMakeProjectInfo info;
    
    if (!fs::exists(cmakePath) || !fs::is_regular_file(cmakePath)) {
        return info;
    }
    
    try {
        std::ifstream file(cmakePath, std::ios::in);
        if (!file.is_open()) {
            return info;
        }
        
        std::string line;
        std::regex projectRegex(R"(project\s*\(\s*(\w+))");
        std::regex addExecutableRegex(R"(add_executable\s*\(\s*(\w+))");
        std::regex addLibraryRegex(R"(add_library\s*\(\s*(\w+))");
        std::regex findPackageRegex(R"(find_package\s*\(\s*(\w+))");
        std::regex addSubdirectoryRegex(R"(add_subdirectory\s*\(\s*([^)]+))");
        std::regex includeDirsRegex(R"(include_directories\s*\()");
        std::regex targetIncludeDirsRegex(R"(target_include_directories\s*\(\s*(\w+))");
        std::regex targetSourcesRegex(R"(target_sources\s*\(\s*(\w+))");
        
        while (std::getline(file, line)) {
            // 移除注释
            size_t commentPos = line.find('#');
            if (commentPos != std::string::npos) {
                line = line.substr(0, commentPos);
            }
            
            // 移除前后空白
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t") + 1);
            
            if (line.empty()) continue;
            
            std::smatch match;
            
            // 解析project()
            if (std::regex_search(line, match, projectRegex)) {
                info.projectName = match[1].str();
            }
            
            // 解析add_executable()
            if (std::regex_search(line, match, addExecutableRegex)) {
                std::string targetName = match[1].str();
                info.targets.push_back(targetName);
                
                // 提取源文件参数（可能跨多行）
                std::vector<std::string> args = extractArguments(file, line);
                for (const auto& arg : args) {
                    std::string normalized = normalizePath(arg, projectRoot);
                    if (!normalized.empty() && fs::exists(projectRoot / normalized)) {
                        info.sourceFiles.push_back(normalized);
                    }
                }
            }
            
            // 解析add_library()
            if (std::regex_search(line, match, addLibraryRegex)) {
                std::string targetName = match[1].str();
                info.targets.push_back(targetName);
                
                // 提取源文件参数
                std::vector<std::string> args = extractArguments(file, line);
                for (const auto& arg : args) {
                    std::string normalized = normalizePath(arg, projectRoot);
                    if (!normalized.empty() && fs::exists(projectRoot / normalized)) {
                        info.sourceFiles.push_back(normalized);
                    }
                }
            }
            
            // 解析find_package()
            if (std::regex_search(line, match, findPackageRegex)) {
                std::string dep = match[1].str();
                if (std::find(info.dependencies.begin(), info.dependencies.end(), dep) == info.dependencies.end()) {
                    info.dependencies.push_back(dep);
                }
            }
            
            // 解析add_subdirectory()
            if (std::regex_search(line, match, addSubdirectoryRegex)) {
                std::string subdir = match[1].str();
                subdir = removeVariableRefs(subdir);
                if (!subdir.empty()) {
                    info.subdirectories.push_back(subdir);
                }
            }
            
            // 解析include_directories()
            if (std::regex_search(line, match, includeDirsRegex)) {
                std::vector<std::string> args = extractArguments(file, line);
                for (const auto& arg : args) {
                    std::string normalized = normalizePath(arg, projectRoot);
                    if (!normalized.empty()) {
                        info.includeDirs.push_back(normalized);
                    }
                }
            }
            
            // 解析target_include_directories()
            if (std::regex_search(line, match, targetIncludeDirsRegex)) {
                std::vector<std::string> args = extractArguments(file, line);
                // 第一个参数是target名称，跳过，后面的才是目录
                for (size_t i = 1; i < args.size(); ++i) {
                    std::string normalized = normalizePath(args[i], projectRoot);
                    if (!normalized.empty()) {
                        info.includeDirs.push_back(normalized);
                    }
                }
            }
            
            // 解析target_sources()
            if (std::regex_search(line, match, targetSourcesRegex)) {
                std::vector<std::string> args = extractArguments(file, line);
                // 第一个参数是target名称，跳过，后面的才是源文件
                for (size_t i = 1; i < args.size(); ++i) {
                    std::string normalized = normalizePath(args[i], projectRoot);
                    if (!normalized.empty() && fs::exists(projectRoot / normalized)) {
                        info.sourceFiles.push_back(normalized);
                    }
                }
            }
        }
        
        file.close();
        
        // 计算配置文件哈希
        std::string content = readFileContent(cmakePath);
        info.configHash = computeStringHash(content);
        
    } catch (...) {
        // 解析失败，返回空结果
    }
    
    return info;
}

std::unordered_map<std::string, CMakeProjectInfo> CMakeParser::parseAllCMakeLists(const fs::path& projectRoot) {
    std::unordered_map<std::string, CMakeProjectInfo> results;
    
    try {
        // 解析根目录的CMakeLists.txt
        fs::path rootCMake = projectRoot / "CMakeLists.txt";
        if (fs::exists(rootCMake)) {
            auto info = CMakeParser::parseCMakeLists(rootCMake, projectRoot);
            results[pathToUtf8String(rootCMake)] = info;
            
            // 递归解析子目录
            for (const auto& subdir : info.subdirectories) {
                fs::path subdirPath = projectRoot / subdir;
                if (fs::exists(subdirPath) && fs::is_directory(subdirPath)) {
                    fs::path subCMake = subdirPath / "CMakeLists.txt";
                    if (fs::exists(subCMake)) {
                        auto subInfo = CMakeParser::parseCMakeLists(subCMake, projectRoot);
                        results[pathToUtf8String(subCMake)] = subInfo;
                    }
                }
            }
        }
    } catch (...) {
        // 忽略错误
    }
    
    return results;
}

std::string CMakeParser::computeFileHash(const fs::path& filePath) {
    try {
        std::string content = readFileContent(filePath);
        return computeStringHash(content);
    } catch (...) {
        return "";
    }
}

std::vector<std::string> CMakeParser::extractArguments(std::ifstream& file, std::string& line) {
    std::vector<std::string> args;
    
    // 查找开括号后的内容
    size_t openParen = line.find('(');
    if (openParen == std::string::npos) {
        return args;
    }
    
    std::string currentArg;
    int parenDepth = 1;
    size_t pos = openParen + 1;
    
    // 处理当前行的剩余部分
    while (pos < line.length() && parenDepth > 0) {
        char c = line[pos];
        if (c == '(') {
            parenDepth++;
            currentArg += c;
        } else if (c == ')') {
            parenDepth--;
            if (parenDepth > 0) {
                currentArg += c;
            }
        } else if (c == ' ' || c == '\t') {
            if (!currentArg.empty()) {
                // 移除引号
                if (currentArg.front() == '"' && currentArg.back() == '"') {
                    currentArg = currentArg.substr(1, currentArg.length() - 2);
                }
                args.push_back(currentArg);
                currentArg.clear();
            }
        } else {
            currentArg += c;
        }
        pos++;
    }
    
    // 如果括号未闭合，继续读取下一行
    while (parenDepth > 0 && std::getline(file, line)) {
        // 移除注释
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }
        
        pos = 0;
        while (pos < line.length() && parenDepth > 0) {
            char c = line[pos];
            if (c == '(') {
                parenDepth++;
                currentArg += c;
            } else if (c == ')') {
                parenDepth--;
                if (parenDepth > 0) {
                    currentArg += c;
                }
            } else if (c == ' ' || c == '\t') {
                if (!currentArg.empty()) {
                    if (currentArg.front() == '"' && currentArg.back() == '"') {
                        currentArg = currentArg.substr(1, currentArg.length() - 2);
                    }
                    args.push_back(currentArg);
                    currentArg.clear();
                }
            } else {
                currentArg += c;
            }
            pos++;
        }
    }
    
    // 添加最后一个参数
    if (!currentArg.empty()) {
        if (currentArg.front() == '"' && currentArg.back() == '"') {
            currentArg = currentArg.substr(1, currentArg.length() - 2);
        }
        args.push_back(currentArg);
    }
    
    return args;
}

std::string CMakeParser::normalizePath(const std::string& path, const fs::path& projectRoot) {
    std::string normalized = removeVariableRefs(path);
    
    // 移除引号
    if (normalized.front() == '"' && normalized.back() == '"') {
        normalized = normalized.substr(1, normalized.length() - 2);
    }
    
    // 处理相对路径
    if (!normalized.empty() && normalized[0] != '/') {
        // 已经是相对路径，直接返回
        return normalized;
    }
    
    // 如果是绝对路径，转换为相对路径
    try {
        fs::path absPath(normalized);
        if (absPath.is_absolute()) {
            fs::path relPath = fs::relative(absPath, projectRoot);
            return pathToUtf8String(relPath);
        }
    } catch (...) {
        // 转换失败，返回原路径
    }
    
    return normalized;
}

std::string CMakeParser::removeVariableRefs(const std::string& str) {
    std::string result = str;
    
    // 简单处理：移除 ${VAR} 形式的变量引用，保留其他部分
    std::regex varRegex(R"(\$\{[^}]+\})");
    result = std::regex_replace(result, varRegex, "");
    
    // 移除多余的空白
    result.erase(0, result.find_first_not_of(" \t"));
    result.erase(result.find_last_not_of(" \t") + 1);
    
    return result;
}

