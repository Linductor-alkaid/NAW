#include "naw/desktop_pet/service/tools/CodeToolsUtils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace fs = std::filesystem;

namespace naw::desktop_pet::service::tools {

// 最大文件大小限制（10MB）
constexpr size_t MAX_FILE_SIZE = 10 * 1024 * 1024;

/**
 * @brief 将路径转换为 UTF-8 编码的 std::string（C++20 实现）
 */
template<typename PathType>
std::string pathToUtf8StringImpl(const PathType& path, std::true_type) {
    // C++20: u8string() 返回 std::u8string
    std::u8string u8str = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8str.data()), u8str.size());
}

/**
 * @brief 将路径转换为 UTF-8 编码的 std::string（C++17 实现）
 */
template<typename PathType>
std::string pathToUtf8StringImpl(const PathType& path, std::false_type) {
    // C++17: u8string() 返回 std::string
    return path.u8string();
}

std::string pathToUtf8String(const fs::path& path) {
    try {
        // 检测 u8string() 的返回类型
        using u8str_type = decltype(path.u8string());
        constexpr bool is_u8string = std::is_same_v<u8str_type, std::u8string>;
        return pathToUtf8StringImpl(path, std::integral_constant<bool, is_u8string>{});
    } catch (...) {
        // 如果 u8string() 失败，回退到 string()（可能不是 UTF-8，但至少能工作）
        return path.string();
    }
}

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

std::vector<std::string> readFileLines(const fs::path& path, int startLine, int endLine) {
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

} // namespace naw::desktop_pet::service::tools

