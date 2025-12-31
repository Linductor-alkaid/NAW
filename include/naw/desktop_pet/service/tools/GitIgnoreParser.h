#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace naw::desktop_pet::service::tools {

/**
 * @brief GitIgnore规则结构
 */
struct GitIgnoreRule {
    std::string pattern;      // 模式字符串
    bool isNegation;          // 是否为否定规则（!pattern）
    bool isDirectoryOnly;     // 是否仅匹配目录（pattern/）
    bool isRecursive;         // 是否递归匹配（**/pattern）
    
    GitIgnoreRule(const std::string& p, bool neg = false, bool dirOnly = false, bool rec = false)
        : pattern(p), isNegation(neg), isDirectoryOnly(dirOnly), isRecursive(rec) {}
};

/**
 * @brief .gitignore解析器
 * 
 * 支持标准gitignore语法，包括模式匹配、否定规则、目录匹配和递归匹配
 */
class GitIgnoreParser {
public:
    /**
     * @brief 构造函数
     * @param projectRoot 项目根目录
     */
    explicit GitIgnoreParser(const fs::path& projectRoot);
    
    /**
     * @brief 解析所有.gitignore文件（递归）
     */
    void parseAll();
    
    /**
     * @brief 检查路径是否被忽略
     * @param filePath 文件路径（相对于projectRoot）
     * @param projectRoot 项目根目录
     * @return true如果路径被忽略
     */
    bool isIgnored(const fs::path& filePath, const fs::path& projectRoot) const;
    
    /**
     * @brief 检查目录是否应该被扫描（不被忽略）
     * @param dirPath 目录路径（相对于projectRoot）
     * @param projectRoot 项目根目录
     * @return true如果目录应该被扫描
     */
    bool shouldScanDirectory(const fs::path& dirPath, const fs::path& projectRoot) const;
    
    /**
     * @brief 获取所有规则
     */
    const std::vector<GitIgnoreRule>& getRules() const { return rules_; }
    
    /**
     * @brief 计算所有.gitignore文件的哈希（用于缓存失效检测）
     */
    std::string computeHash() const;

private:
    /**
     * @brief 解析单个.gitignore文件
     * @param gitignorePath .gitignore文件路径
     */
    void parseFile(const fs::path& gitignorePath);
    
    /**
     * @brief 解析单行规则
     * @param line 规则行
     * @param baseDir .gitignore文件所在目录（用于相对路径规则）
     */
    void parseLine(const std::string& line, const fs::path& baseDir);
    
    /**
     * @brief 规范化模式（移除前导/，处理特殊字符）
     * @param pattern 原始模式
     * @return 规范化后的模式
     */
    static std::string normalizePattern(const std::string& pattern);
    
    /**
     * @brief 检查路径是否匹配规则
     * @param path 路径（相对于projectRoot）
     * @param rule 规则
     * @param projectRoot 项目根目录
     * @return true如果匹配
     */
    static bool matchesRule(const fs::path& path, const GitIgnoreRule& rule, const fs::path& projectRoot);
    
    /**
     * @brief 将gitignore模式转换为正则表达式
     * @param pattern gitignore模式
     * @return 正则表达式字符串
     */
    static std::string patternToRegex(const std::string& pattern);

private:
    fs::path projectRoot_;
    std::vector<GitIgnoreRule> rules_;
    std::vector<fs::path> parsedFiles_;  // 已解析的.gitignore文件路径
};

} // namespace naw::desktop_pet::service::tools

