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

#if defined(_WIN32)
#include <windows.h>
#endif

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

std::string sanitizeUtf8String(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    
    for (size_t i = 0; i < str.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        
        // ASCII字符（0x00-0x7F）直接保留
        if (c < 0x80) {
            result += str[i];
        }
        // UTF-8多字节字符的起始字节
        else if ((c & 0xE0) == 0xC0) {
            // 2字节字符：110xxxxx 10xxxxxx
            // 注意：0xC0 和 0xC1 是无效的（它们会编码小于0x80的字符）
            // 有效范围：0xC2-0xDF
            if (c < 0xC2 || c > 0xDF) {
                result += '?';
                continue;
            }
            if (i + 1 < str.size() && (str[i + 1] & 0xC0) == 0x80) {
                result += str[i];
                result += str[i + 1];
                ++i;
            } else {
                // 无效序列，替换为问号
                result += '?';
            }
        }
        else if ((c & 0xF0) == 0xE0) {
            // 3字节字符：1110xxxx 10xxxxxx 10xxxxxx
            // 有效范围：0xE0-0xEF
            if (c < 0xE0 || c > 0xEF) {
                result += '?';
                continue;
            }
            if (i + 2 < str.size() && 
                (str[i + 1] & 0xC0) == 0x80 && 
                (str[i + 2] & 0xC0) == 0x80) {
                // 验证三字节字符的有效性（避免代理对和过长的序列）
                unsigned char c1 = static_cast<unsigned char>(str[i + 1]);
                if (c == 0xE0 && c1 < 0xA0) {
                    result += '?';
                    i += 2;
                    continue;
                }
                if (c == 0xED && c1 > 0x9F) {
                    result += '?';
                    i += 2;
                    continue;
                }
                result += str[i];
                result += str[i + 1];
                result += str[i + 2];
                i += 2;
            } else {
                result += '?';
            }
        }
        else if ((c & 0xF8) == 0xF0) {
            // 4字节字符：11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
            // 有效范围：0xF0-0xF4（0xF5-0xFF 是无效的起始字节）
            if (c > 0xF4) {
                result += '?';
                continue;
            }
            if (i + 3 < str.size() && 
                (str[i + 1] & 0xC0) == 0x80 && 
                (str[i + 2] & 0xC0) == 0x80 &&
                (str[i + 3] & 0xC0) == 0x80) {
                // 验证四字节字符的有效性
                unsigned char c1 = static_cast<unsigned char>(str[i + 1]);
                if (c == 0xF0 && c1 < 0x90) {
                    result += '?';
                    i += 3;
                    continue;
                }
                if (c == 0xF4 && c1 > 0x8F) {
                    result += '?';
                    i += 3;
                    continue;
                }
                result += str[i];
                result += str[i + 1];
                result += str[i + 2];
                result += str[i + 3];
                i += 3;
            } else {
                result += '?';
            }
        }
        // 继续字节（10xxxxxx）不应该单独出现
        else if ((c & 0xC0) == 0x80) {
            // 孤立的继续字节，替换为问号
            result += '?';
        }
        // 其他无效的UTF-8字节，替换为问号
        else {
            result += '?';
        }
    }
    
    return result;
}

FileEncoding detectFileEncoding(const std::vector<unsigned char>& content) {
    if (content.empty()) {
        return FileEncoding::UTF8;
    }
    
    // 检查 BOM
    if (content.size() >= 3 && 
        content[0] == 0xEF && content[1] == 0xBB && content[2] == 0xBF) {
        return FileEncoding::UTF8_BOM;
    }
    
    if (content.size() >= 2) {
        if (content[0] == 0xFF && content[1] == 0xFE) {
            return FileEncoding::UTF16LE;
        }
        if (content[0] == 0xFE && content[1] == 0xFF) {
            return FileEncoding::UTF16BE;
        }
    }
    
    // 尝试验证是否为有效的 UTF-8（无 BOM）
    if (isValidUtf8(std::string(content.begin(), content.end()))) {
        return FileEncoding::UTF8;
    }
    
#if defined(_WIN32)
    // Windows 系统：优先尝试 GBK
    return FileEncoding::GBK;
#else
    // 其他系统：尝试 Latin-1
    return FileEncoding::Latin1;
#endif
}

std::optional<std::string> convertToUtf8(const std::vector<unsigned char>& content, FileEncoding encoding) {
    if (content.empty()) {
        return std::string();
    }
    
    try {
        switch (encoding) {
            case FileEncoding::UTF8:
            case FileEncoding::UTF8_BOM: {
                // UTF-8 或 UTF-8 with BOM
                size_t offset = (encoding == FileEncoding::UTF8_BOM) ? 3 : 0;
                std::string result(content.begin() + offset, content.end());
                // 验证转换后的字符串
                if (isValidUtf8(result)) {
                    return result;
                }
                // 如果无效，尝试清理
                return sanitizeUtf8String(result);
            }
            
            case FileEncoding::UTF16LE: {
#if defined(_WIN32)
                // 跳过 BOM
                size_t offset = (content.size() >= 2 && content[0] == 0xFF && content[1] == 0xFE) ? 2 : 0;
                if (offset >= content.size() || (content.size() - offset) % 2 != 0) {
                    return std::nullopt;
                }
                
                const wchar_t* wstr = reinterpret_cast<const wchar_t*>(content.data() + offset);
                int wlen = static_cast<int>((content.size() - offset) / 2);
                
                int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr, wlen, nullptr, 0, nullptr, nullptr);
                if (u8len <= 0) {
                    return std::nullopt;
                }
                
                std::string result;
                result.resize(static_cast<size_t>(u8len));
                WideCharToMultiByte(CP_UTF8, 0, wstr, wlen, result.data(), u8len, nullptr, nullptr);
                return result;
#else
                // 非 Windows 平台：简单实现（可能需要使用 iconv）
                return std::nullopt;
#endif
            }
            
            case FileEncoding::UTF16BE: {
                // UTF-16BE 需要字节交换和转换（Windows API 不支持，需要手动处理）
                // 这里简化处理，返回空
                return std::nullopt;
            }
            
            case FileEncoding::GBK: {
#if defined(_WIN32)
                // GBK/CP936 转 UTF-8
                int wlen = MultiByteToWideChar(936, 0, 
                    reinterpret_cast<const char*>(content.data()), 
                    static_cast<int>(content.size()), 
                    nullptr, 0);
                if (wlen <= 0) {
                    return std::nullopt;
                }
                
                std::vector<wchar_t> wstr(static_cast<size_t>(wlen));
                MultiByteToWideChar(936, 0,
                    reinterpret_cast<const char*>(content.data()),
                    static_cast<int>(content.size()),
                    wstr.data(), wlen);
                
                int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wlen, nullptr, 0, nullptr, nullptr);
                if (u8len <= 0) {
                    return std::nullopt;
                }
                
                std::string result;
                result.resize(static_cast<size_t>(u8len));
                WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wlen, result.data(), u8len, nullptr, nullptr);
                return result;
#else
                // 非 Windows 平台：不支持 GBK
                return std::nullopt;
#endif
            }
            
            case FileEncoding::Latin1: {
                // Latin-1 直接映射到 Unicode，转换为 UTF-8
                std::string result;
                result.reserve(content.size() * 2); // UTF-8 最多可能占用 2 字节
                
                for (unsigned char c : content) {
                    if (c < 0x80) {
                        result += static_cast<char>(c);
                    } else {
                        // Latin-1 字符直接映射到 Unicode，UTF-8 编码为 2 字节
                        result += static_cast<char>(0xC0 | (c >> 6));
                        result += static_cast<char>(0x80 | (c & 0x3F));
                    }
                }
                return result;
            }
            
            case FileEncoding::Unknown:
            default:
                return std::nullopt;
        }
    } catch (...) {
        return std::nullopt;
    }
}

bool isValidUtf8(const std::string& str) {
    for (size_t i = 0; i < str.size(); ) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        
        if (c < 0x80) {
            // ASCII
            ++i;
        } else if ((c & 0xE0) == 0xC0) {
            // 2字节字符
            if (c < 0xC2 || c > 0xDF) {
                return false;
            }
            if (i + 1 >= str.size() || (str[i + 1] & 0xC0) != 0x80) {
                return false;
            }
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            // 3字节字符
            if (i + 2 >= str.size() || 
                (str[i + 1] & 0xC0) != 0x80 || 
                (str[i + 2] & 0xC0) != 0x80) {
                return false;
            }
            unsigned char c1 = static_cast<unsigned char>(str[i + 1]);
            if (c == 0xE0 && c1 < 0xA0) {
                return false;
            }
            if (c == 0xED && c1 > 0x9F) {
                return false;
            }
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            // 4字节字符
            if (c > 0xF4) {
                return false;
            }
            if (i + 3 >= str.size() || 
                (str[i + 1] & 0xC0) != 0x80 || 
                (str[i + 2] & 0xC0) != 0x80 ||
                (str[i + 3] & 0xC0) != 0x80) {
                return false;
            }
            unsigned char c1 = static_cast<unsigned char>(str[i + 1]);
            if (c == 0xF0 && c1 < 0x90) {
                return false;
            }
            if (c == 0xF4 && c1 > 0x8F) {
                return false;
            }
            i += 4;
        } else {
            // 无效的起始字节
            return false;
        }
    }
    return true;
}

std::pair<std::string, bool> validateAndFixUtf8(const std::string& str) {
    if (isValidUtf8(str)) {
        return {str, true};
    }
    return {sanitizeUtf8String(str), false};
}

size_t countUtf8Chars(const std::string& str) {
    size_t count = 0;
    for (size_t i = 0; i < str.size(); ) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        
        if (c < 0x80) {
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            i += 4;
        } else {
            // 无效字节，跳过
            i += 1;
        }
        count++;
    }
    return count;
}

size_t utf8CharAt(const std::string& str, size_t charIndex) {
    size_t byteOffset = 0;
    size_t charCount = 0;
    
    while (byteOffset < str.size() && charCount < charIndex) {
        unsigned char c = static_cast<unsigned char>(str[byteOffset]);
        
        if (c < 0x80) {
            byteOffset += 1;
        } else if ((c & 0xE0) == 0xC0) {
            byteOffset += 2;
        } else if ((c & 0xF0) == 0xE0) {
            byteOffset += 3;
        } else if ((c & 0xF8) == 0xF0) {
            byteOffset += 4;
        } else {
            // 无效字节，跳过
            byteOffset += 1;
        }
        charCount++;
    }
    
    return byteOffset;
}

size_t utf8ColumnToByteOffset(const std::string& str, int column) {
    if (column <= 0) {
        return 0;
    }
    // column 从1开始，转换为从0开始的索引
    return utf8CharAt(str, static_cast<size_t>(column - 1));
}

std::pair<size_t, size_t> getUtf8CharRange(const std::string& str, size_t startChar, size_t endChar) {
    size_t startByte = utf8CharAt(str, startChar);
    size_t endByte = utf8CharAt(str, endChar);
    return {startByte, endByte};
}

} // namespace naw::desktop_pet::service::tools

