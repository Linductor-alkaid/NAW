#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace naw::desktop_pet::service::tools {

/**
 * @brief CMake项目信息结构
 */
struct CMakeProjectInfo {
    std::string projectName;
    std::vector<std::string> sourceFiles;      // 从add_executable/add_library提取
    std::vector<std::string> includeDirs;     // 从include_directories提取
    std::vector<std::string> subdirectories;  // 从add_subdirectory提取
    std::vector<std::string> targets;
    std::vector<std::string> dependencies;
    
    // 配置文件哈希（用于缓存失效检测）
    std::string configHash;
};

/**
 * @brief CMakeLists.txt解析器
 * 
 * 增强的CMake解析器，能够提取：
 * - 项目名称
 * - 源文件列表（从add_executable/add_library）
 * - 包含目录（从include_directories）
 * - 子目录（从add_subdirectory）
 * - 目标名称
 * - 依赖关系
 */
class CMakeParser {
public:
    /**
     * @brief 解析CMakeLists.txt文件
     * @param cmakePath CMakeLists.txt文件路径
     * @param projectRoot 项目根目录（用于解析相对路径）
     * @return CMakeProjectInfo 解析结果
     */
    static CMakeProjectInfo parseCMakeLists(const fs::path& cmakePath, const fs::path& projectRoot);
    
    /**
     * @brief 递归解析所有CMakeLists.txt文件
     * @param projectRoot 项目根目录
     * @return 所有CMakeLists.txt的解析结果（按路径索引）
     */
    static std::unordered_map<std::string, CMakeProjectInfo> parseAllCMakeLists(const fs::path& projectRoot);
    
    /**
     * @brief 计算文件哈希（用于缓存失效检测）
     * @param filePath 文件路径
     * @return 文件内容的哈希值（十六进制字符串）
     */
    static std::string computeFileHash(const fs::path& filePath);

private:
    /**
     * @brief 解析单行CMake命令
     * @param line 命令行
     * @param info 解析结果（输出）
     * @param projectRoot 项目根目录
     */
    static void parseLine(const std::string& line, CMakeProjectInfo& info, const fs::path& projectRoot);
    
    /**
     * @brief 提取函数参数（支持多行）
     * @param file 文件流
     * @param line 当前行（输入输出）
     * @return 参数列表
     */
    static std::vector<std::string> extractArguments(std::ifstream& file, std::string& line);
    
    /**
     * @brief 规范化路径（处理相对路径和变量引用）
     * @param path 原始路径
     * @param projectRoot 项目根目录
     * @return 规范化后的路径
     */
    static std::string normalizePath(const std::string& path, const fs::path& projectRoot);
    
    /**
     * @brief 移除CMake变量引用（简单处理，不展开变量）
     * @param str 包含变量的字符串
     * @return 移除变量后的字符串（保留路径部分）
     */
    static std::string removeVariableRefs(const std::string& str);
};

} // namespace naw::desktop_pet::service::tools

