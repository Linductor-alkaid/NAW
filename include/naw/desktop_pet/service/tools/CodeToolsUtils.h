#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace naw::desktop_pet::service::tools {

/**
 * @brief 将路径转换为 UTF-8 编码的 std::string
 * 兼容 C++17 和 C++20（C++20 中 u8string() 返回 std::u8string）
 */
std::string pathToUtf8String(const fs::path& path);

/**
 * @brief 检查文件大小是否超过限制
 */
bool isFileTooLarge(const fs::path& path);

/**
 * @brief 将通配符模式转换为正则表达式
 */
std::string wildcardToRegex(const std::string& pattern);

/**
 * @brief 检查文件名是否匹配模式
 */
bool matchesPattern(const std::string& filename, const std::string& pattern);

/**
 * @brief 读取文件的指定行范围
 */
std::vector<std::string> readFileLines(const fs::path& path, int startLine = 0, int endLine = -1);

/**
 * @brief 统计文件总行数
 */
int countFileLines(const fs::path& path);

} // namespace naw::desktop_pet::service::tools

