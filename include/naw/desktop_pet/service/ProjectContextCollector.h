#pragma once

#include "naw/desktop_pet/service/ErrorTypes.h"
#include "naw/desktop_pet/service/ContextManager.h"

#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"

namespace fs = std::filesystem;

namespace naw::desktop_pet::service {

/**
 * @brief 项目信息结构体
 */
struct ProjectInfo {
    std::string name;                              // 项目名称
    std::string rootPath;                           // 项目根路径
    std::vector<std::string> sourceFiles;            // 源文件列表
    std::vector<std::string> headerFiles;           // 头文件列表
    nlohmann::json cmakeConfig;                      // CMake配置（JSON格式）
    std::vector<std::string> dependencies;           // 依赖列表
    std::optional<std::string> directoryStructure;  // 目录结构树（可选，字符串格式）
    std::unordered_map<std::string, std::string> fileContents;  // 文件内容缓存
};

/**
 * @brief 项目上下文收集器
 *
 * 分析项目结构，收集项目信息（目录结构、CMake配置、依赖关系等），
 * 为LLM提供项目上下文。这个模块为 ContextManager 提供项目上下文数据，
 * 帮助LLM理解项目结构和代码关系。
 */
class ProjectContextCollector {
public:
    ProjectContextCollector() = default;
    ~ProjectContextCollector() = default;

    // 禁止拷贝/移动（因为包含mutex）
    ProjectContextCollector(const ProjectContextCollector&) = delete;
    ProjectContextCollector& operator=(const ProjectContextCollector&) = delete;
    ProjectContextCollector(ProjectContextCollector&&) = delete;
    ProjectContextCollector& operator=(ProjectContextCollector&&) = delete;

    // ========== 项目结构分析 ==========

    /**
     * @brief 检测项目根目录
     * @param startPath 起始路径（可以是文件或目录）
     * @return 项目根路径
     */
    static std::string detectProjectRoot(const std::string& startPath);

    /**
     * @brief 分析项目结构
     * @param projectRoot 项目根路径
     * @param error 如果分析失败，输出错误信息（可选）
     * @return 项目信息对象
     */
    ProjectInfo analyzeProject(const std::string& projectRoot, ErrorInfo* error = nullptr);

    /**
     * @brief 解析 CMakeLists.txt 文件
     * @param cmakePath CMakeLists.txt 文件路径
     * @return CMake配置（JSON格式）
     */
    static nlohmann::json parseCMakeLists(const std::string& cmakePath);

    /**
     * @brief 识别文件类型
     * @param filePath 文件路径
     * @return 文件类型字符串（"cpp", "header", "python", "cmake", "config", "other"）
     */
    static std::string identifyFileType(const std::string& filePath);

    // ========== 依赖关系提取 ==========

    /**
     * @brief 从CMake配置中提取依赖
     * @param cmakeConfig CMake配置（JSON格式）
     * @return 依赖列表
     */
    static std::vector<std::string> extractDependenciesFromCMake(const nlohmann::json& cmakeConfig);

    /**
     * @brief 从源码文件中提取 include 依赖
     * @param filePath 源文件路径
     * @param projectInfo 项目信息（用于判断文件是否在项目内）
     * @return 包含文件列表（项目内的文件）
     */
    static std::vector<std::string> extractIncludesFromSource(
        const std::string& filePath,
        const ProjectInfo& projectInfo
    );

    // ========== 文件上下文收集 ==========

    /**
     * @brief 查找相关文件
     * @param filePath 文件路径
     * @param projectInfo 项目信息
     * @return 相关文件列表（包含该文件的文件和被该文件包含的文件）
     */
    static std::vector<std::string> findRelatedFiles(
        const std::string& filePath,
        const ProjectInfo& projectInfo
    );

    /**
     * @brief 获取文件上下文
     * @param filePath 文件路径
     * @param projectInfo 项目信息
     * @param maxDepth 最大依赖深度（默认1，只包含直接依赖）
     * @param maxFiles 最大文件数（默认10）
     * @param maxTokens 最大Token数（默认0，表示不限制）
     * @return 文件上下文字符串
     */
    std::string getFileContext(
        const std::string& filePath,
        const ProjectInfo& projectInfo,
        int maxDepth = 1,
        size_t maxFiles = 10,
        size_t maxTokens = 0
    );

    // ========== 项目摘要生成 ==========

    /**
     * @brief 获取项目摘要
     * @param projectInfo 项目信息
     * @param maxLength 最大长度（字符数，默认2000）
     * @return 项目摘要字符串
     */
    std::string getProjectSummary(const ProjectInfo& projectInfo, size_t maxLength = 2000);

    // ========== 与 ContextManager 集成 ==========

    /**
     * @brief 收集项目上下文（适配 ContextManager::ProjectContext）
     * @param projectRoot 项目根路径
     * @param error 如果收集失败，输出错误信息（可选）
     * @return 项目上下文对象
     */
    ProjectContext collectProjectContext(const std::string& projectRoot, ErrorInfo* error = nullptr);

    // ========== 缓存管理 ==========

    /**
     * @brief 清除文件内容缓存
     */
    void clearFileCache();

    /**
     * @brief 清除摘要缓存
     */
    void clearSummaryCache();

    /**
     * @brief 清除所有缓存
     */
    void clearAllCaches();

private:
    // 文件内容缓存：文件路径 -> 文件内容
    std::unordered_map<std::string, std::string> m_fileCache;
    // 文件修改时间缓存：文件路径 -> 修改时间
    std::unordered_map<std::string, std::filesystem::file_time_type> m_fileModifyTime;
    // 摘要缓存：项目根路径 -> 摘要内容
    std::unordered_map<std::string, std::string> m_summaryCache;
    // 摘要修改时间缓存：项目根路径 -> 最后修改时间
    std::unordered_map<std::string, std::filesystem::file_time_type> m_summaryModifyTime;

    // 线程安全保护
    mutable std::mutex m_mutex;

    // 内部辅助方法

    /**
     * @brief 读取文件内容（带缓存）
     * @param filePath 文件路径
     * @return 文件内容（如果读取失败返回空字符串）
     */
    std::string readFileWithCache(const std::string& filePath);

    /**
     * @brief 检查文件是否需要更新缓存
     * @param filePath 文件路径
     * @return 如果需要更新返回 true
     */
    bool needsCacheUpdate(const std::string& filePath);

    /**
     * @brief 构建目录结构树（字符串格式）
     * @param projectRoot 项目根路径
     * @param maxDepth 最大深度
     * @return 目录结构字符串
     */
    static std::string buildDirectoryStructure(const std::string& projectRoot, int maxDepth = 5);

    /**
     * @brief 查找包含指定文件的文件（反向依赖）
     * @param targetFile 目标文件路径
     * @param projectInfo 项目信息
     * @return 包含该文件的文件列表
     */
    static std::vector<std::string> findFilesIncluding(
        const std::string& targetFile,
        const ProjectInfo& projectInfo
    );
};

} // namespace naw::desktop_pet::service

