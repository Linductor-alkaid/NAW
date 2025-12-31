#pragma once

#include "naw/desktop_pet/service/tools/CMakeParser.h"
#include "naw/desktop_pet/service/tools/GitIgnoreParser.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace naw::desktop_pet::service::tools {

/**
 * @brief 项目文件白名单
 * 
 * 基于CMake配置和.gitignore规则构建，确定需要扫描的文件和目录
 */
struct ProjectFileWhitelist {
    std::unordered_set<std::string> sourceFiles;      // 源文件路径集合（相对路径）
    std::unordered_set<std::string> includeDirs;      // 包含目录集合（相对路径）
    std::unordered_set<std::string> configFiles;     // 配置文件集合（相对路径）
    std::unordered_set<std::string> docFiles;         // 文档文件集合（相对路径）
    std::unordered_set<std::string> resourceDirs;     // 资源目录集合（相对路径）
    std::vector<fs::path> scanRoots;                  // 需要扫描的根目录列表（绝对路径）
    
    // 配置文件哈希（用于缓存失效检测）
    std::string cmakeHash;
    std::string gitignoreHash;
    std::string combinedHash;  // 组合哈希
    
    // CMake项目信息
    CMakeProjectInfo cmakeInfo;
    
    // GitIgnore解析器
    std::unique_ptr<GitIgnoreParser> gitIgnoreParser;
    
    // 默认构造函数
    ProjectFileWhitelist() = default;
    
    // 移动构造函数
    ProjectFileWhitelist(ProjectFileWhitelist&&) noexcept = default;
    
    // 移动赋值运算符
    ProjectFileWhitelist& operator=(ProjectFileWhitelist&&) noexcept = default;
    
    // 禁用复制（因为包含unique_ptr）
    ProjectFileWhitelist(const ProjectFileWhitelist&) = delete;
    ProjectFileWhitelist& operator=(const ProjectFileWhitelist&) = delete;
    
    /**
     * @brief 检查文件是否在白名单中
     * @param filePath 文件路径（绝对或相对）
     * @param projectRoot 项目根目录
     * @return true如果文件在白名单中
     */
    bool isWhitelisted(const fs::path& filePath, const fs::path& projectRoot) const;
    
    /**
     * @brief 检查目录是否应该被扫描
     * @param dirPath 目录路径（绝对或相对）
     * @param projectRoot 项目根目录
     * @return true如果目录应该被扫描
     */
    bool shouldScanDirectory(const fs::path& dirPath, const fs::path& projectRoot) const;
    
    /**
     * @brief 检查文件扩展名是否为源文件
     * @param ext 文件扩展名（包含点，如".cpp"）
     * @return true如果是源文件扩展名
     */
    static bool isSourceFileExtension(const std::string& ext);
    
    /**
     * @brief 检查文件扩展名是否为头文件
     * @param ext 文件扩展名（包含点，如".h"）
     * @return true如果是头文件扩展名
     */
    static bool isHeaderFileExtension(const std::string& ext);
    
    /**
     * @brief 检查文件扩展名是否为文档文件
     * @param ext 文件扩展名（包含点，如".md"）
     * @return true如果是文档文件扩展名
     */
    static bool isDocumentFileExtension(const std::string& ext);
    
    /**
     * @brief 检查文件扩展名是否为资源文件
     * @param ext 文件扩展名（包含点，如".png"）
     * @return true如果是资源文件扩展名
     */
    static bool isResourceFileExtension(const std::string& ext);
};

/**
 * @brief 构建项目文件白名单
 * 
 * 基于以下信息构建：
 * 1. CMakeLists.txt中定义的源文件和包含目录
 * 2. 项目结构推断（src/、include/、config/目录）
 * 3. .gitignore规则（排除不需要的文件）
 * 
 * @param projectRoot 项目根目录
 * @param useCMakeSources 是否使用CMake源文件列表
 * @param useGitIgnore 是否使用.gitignore规则
 * @param scanSrcDirs 默认扫描的源目录列表（如["src", "include", "config"]）
 * @param excludeDirs 默认排除的目录列表（如["third_party", "build"]）
 * @return ProjectFileWhitelist 白名单对象
 */
ProjectFileWhitelist buildProjectWhitelist(
    const fs::path& projectRoot,
    bool useCMakeSources = true,
    bool useGitIgnore = true,
    const std::vector<std::string>& scanSrcDirs = {"src", "include", "config", "docs", "doc", "resources", "assets"},
    const std::vector<std::string>& excludeDirs = {"third_party", "build", "cmake-build-*", ".git"}
);

} // namespace naw::desktop_pet::service::tools

