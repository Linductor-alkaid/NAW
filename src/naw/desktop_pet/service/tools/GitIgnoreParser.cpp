#include "naw/desktop_pet/service/tools/GitIgnoreParser.h"
#include "naw/desktop_pet/service/tools/CodeToolsUtils.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;
using namespace naw::desktop_pet::service::tools;

namespace {
    // 计算字符串的简单哈希
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

GitIgnoreParser::GitIgnoreParser(const fs::path& projectRoot) : projectRoot_(projectRoot) {
}

void GitIgnoreParser::parseAll() {
    rules_.clear();
    parsedFiles_.clear();
    
    // 解析根目录的.gitignore
    fs::path rootGitIgnore = projectRoot_ / ".gitignore";
    if (fs::exists(rootGitIgnore)) {
        parseFile(rootGitIgnore);
    }
    
    // 递归查找所有.gitignore文件
    try {
        if (fs::exists(projectRoot_) && fs::is_directory(projectRoot_)) {
            for (const auto& entry : fs::recursive_directory_iterator(projectRoot_,
                    fs::directory_options::skip_permission_denied)) {
                if (entry.is_regular_file() && entry.path().filename() == ".gitignore") {
                    if (std::find(parsedFiles_.begin(), parsedFiles_.end(), entry.path()) == parsedFiles_.end()) {
                        parseFile(entry.path());
                    }
                }
            }
        }
    } catch (...) {
        // 忽略错误
    }
}

bool GitIgnoreParser::isIgnored(const fs::path& filePath, const fs::path& projectRoot) const {
    try {
        fs::path relPath = fs::relative(filePath, projectRoot);
        if (relPath.empty() || relPath == ".") {
            return false;
        }
        
        std::string pathStr = pathToUtf8String(relPath);
        std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
        
        // 检查所有规则（按顺序，后面的规则可能覆盖前面的）
        bool ignored = false;
        for (const auto& rule : rules_) {
            if (matchesRule(relPath, rule, projectRoot)) {
                if (rule.isNegation) {
                    ignored = false;  // 否定规则：不忽略
                } else {
                    ignored = true;   // 正常规则：忽略
                }
            }
        }
        
        return ignored;
    } catch (...) {
        return false;
    }
}

bool GitIgnoreParser::shouldScanDirectory(const fs::path& dirPath, const fs::path& projectRoot) const {
    // 目录不被忽略，且不是.git目录
    if (isIgnored(dirPath, projectRoot)) {
        return false;
    }
    
    std::string dirName = pathToUtf8String(dirPath.filename());
    if (dirName == ".git") {
        return false;
    }
    
    return true;
}

std::string GitIgnoreParser::computeHash() const {
    std::ostringstream oss;
    for (const auto& file : parsedFiles_) {
        std::string content = readFileContent(file);
        oss << pathToUtf8String(file) << ":" << computeStringHash(content) << ";";
    }
    return computeStringHash(oss.str());
}

void GitIgnoreParser::parseFile(const fs::path& gitignorePath) {
    if (!fs::exists(gitignorePath) || !fs::is_regular_file(gitignorePath)) {
        return;
    }
    
    try {
        std::ifstream file(gitignorePath);
        if (!file.is_open()) {
            return;
        }
        
        fs::path baseDir = gitignorePath.parent_path();
        std::string line;
        
        while (std::getline(file, line)) {
            parseLine(line, baseDir);
        }
        
        parsedFiles_.push_back(gitignorePath);
    } catch (...) {
        // 忽略错误
    }
}

void GitIgnoreParser::parseLine(const std::string& line, const fs::path& baseDir) {
    // 移除注释和前后空白
    std::string trimmed = line;
    size_t commentPos = trimmed.find('#');
    if (commentPos != std::string::npos) {
        trimmed = trimmed.substr(0, commentPos);
    }
    
    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
    trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
    
    if (trimmed.empty()) {
        return;
    }
    
    // 检查是否为否定规则
    bool isNegation = false;
    if (trimmed.front() == '!') {
        isNegation = true;
        trimmed = trimmed.substr(1);
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
    }
    
    // 检查是否为目录规则
    bool isDirectoryOnly = false;
    if (trimmed.back() == '/') {
        isDirectoryOnly = true;
        trimmed.pop_back();
    }
    
    // 检查是否为递归规则
    bool isRecursive = false;
    if (trimmed.find("**/") == 0) {
        isRecursive = true;
        trimmed = trimmed.substr(3);
    }
    
    // 规范化模式
    std::string pattern = normalizePattern(trimmed);
    if (pattern.empty()) {
        return;
    }
    
    // 如果是相对路径模式，需要相对于baseDir
    if (pattern.front() != '/' && !isRecursive) {
        fs::path patternPath = baseDir / pattern;
        fs::path relPath = fs::relative(patternPath, projectRoot_);
        pattern = pathToUtf8String(relPath);
        std::replace(pattern.begin(), pattern.end(), '\\', '/');
    }
    
    rules_.emplace_back(pattern, isNegation, isDirectoryOnly, isRecursive);
}

std::string GitIgnoreParser::normalizePattern(const std::string& pattern) {
    std::string result = pattern;
    
    // 移除前导斜杠
    while (result.front() == '/') {
        result = result.substr(1);
    }
    
    // 转义特殊字符（但保留*和?用于通配符）
    // 这里简化处理，主要依赖wildcardToRegex
    
    return result;
}

bool GitIgnoreParser::matchesRule(const fs::path& path, const GitIgnoreRule& rule, const fs::path& projectRoot) {
    try {
        std::string pathStr = pathToUtf8String(path);
        std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
        
        // 如果是目录规则，只匹配目录
        if (rule.isDirectoryOnly && !fs::is_directory(projectRoot / path)) {
            return false;
        }
        
        // 转换为正则表达式并匹配
        std::string regexPattern = patternToRegex(rule.pattern);
        std::regex re(regexPattern, std::regex::icase);
        
        // 检查完整路径或文件名
        if (std::regex_match(pathStr, re)) {
            return true;
        }
        
        // 检查路径的各个部分
        for (const auto& component : path) {
            std::string compStr = pathToUtf8String(component);
            if (std::regex_match(compStr, re)) {
                return true;
            }
        }
        
        // 如果是递归规则，检查路径是否包含模式
        if (rule.isRecursive) {
            if (pathStr.find(rule.pattern) != std::string::npos) {
                return true;
            }
        }
        
        return false;
    } catch (...) {
        return false;
    }
}

std::string GitIgnoreParser::patternToRegex(const std::string& pattern) {
    // 使用现有的wildcardToRegex函数
    return wildcardToRegex(pattern);
}

