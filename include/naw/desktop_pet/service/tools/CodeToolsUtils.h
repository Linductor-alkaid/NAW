#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <optional>

namespace fs = std::filesystem;

namespace naw::desktop_pet::service::tools {

/**
 * @brief 文件编码类型枚举
 */
enum class FileEncoding {
    UTF8,
    UTF8_BOM,
    UTF16LE,
    UTF16BE,
    GBK,      // Windows 中文编码
    Latin1,   // ISO-8859-1
    Unknown   // 无法识别的编码
};

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

/**
 * @brief 清理字符串中的无效UTF-8字节，确保可以安全地放入JSON
 * @param str 输入字符串
 * @return 清理后的UTF-8字符串
 */
std::string sanitizeUtf8String(const std::string& str);

/**
 * @brief 检测文件编码类型
 * @param content 文件内容的字节数据
 * @return 检测到的编码类型
 */
FileEncoding detectFileEncoding(const std::vector<unsigned char>& content);

/**
 * @brief 将不同编码的字符串转换为 UTF-8
 * @param content 原始文件内容
 * @param encoding 原始编码类型
 * @return 转换后的 UTF-8 字符串，失败时返回 std::nullopt
 */
std::optional<std::string> convertToUtf8(const std::vector<unsigned char>& content, FileEncoding encoding);

/**
 * @brief 验证字符串是否为有效的 UTF-8
 * @param str 要验证的字符串
 * @return 是否为有效的 UTF-8
 */
bool isValidUtf8(const std::string& str);

/**
 * @brief 验证并修复 UTF-8 字符串
 * @param str 输入字符串
 * @return 修复后的 UTF-8 字符串和是否有效的标志
 */
std::pair<std::string, bool> validateAndFixUtf8(const std::string& str);

/**
 * @brief 计算 UTF-8 字符串中的字符数（而非字节数）
 * @param str UTF-8 字符串
 * @return 字符数
 */
size_t countUtf8Chars(const std::string& str);

/**
 * @brief 获取 UTF-8 字符串中指定字符位置的字节偏移
 * @param str UTF-8 字符串
 * @param charIndex 字符索引（从0开始）
 * @return 字节偏移，如果超出范围则返回 str.size()
 */
size_t utf8CharAt(const std::string& str, size_t charIndex);

/**
 * @brief 将字符列位置转换为字节偏移
 * @param str UTF-8 字符串
 * @param column 列位置（从1开始，字符位置）
 * @return 字节偏移，如果超出范围则返回 str.size()
 */
size_t utf8ColumnToByteOffset(const std::string& str, int column);

/**
 * @brief 获取 UTF-8 字符串中指定字符范围的字节范围
 * @param str UTF-8 字符串
 * @param startChar 起始字符索引（从0开始）
 * @param endChar 结束字符索引（从0开始，不包含）
 * @return (起始字节偏移, 结束字节偏移)
 */
std::pair<size_t, size_t> getUtf8CharRange(const std::string& str, size_t startChar, size_t endChar);

} // namespace naw::desktop_pet::service::tools

