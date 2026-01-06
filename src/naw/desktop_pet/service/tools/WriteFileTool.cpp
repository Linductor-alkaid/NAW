#include "naw/desktop_pet/service/CodeTools.h"
#include "naw/desktop_pet/service/tools/CodeToolsUtils.h"
#include "naw/desktop_pet/service/ToolManager.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace naw::desktop_pet::service;
using namespace naw::desktop_pet::service::tools;

static nlohmann::json handleWriteFile(const nlohmann::json& arguments) {
    try {
        // 提取参数
        if (!arguments.contains("path") || !arguments["path"].is_string()) {
            return nlohmann::json{{"error", "缺少必需参数: path"}};
        }
        if (!arguments.contains("content") || !arguments["content"].is_string()) {
            return nlohmann::json{{"error", "缺少必需参数: content"}};
        }
        
        std::string pathStr = arguments["path"].get<std::string>();
        std::string content = arguments["content"].get<std::string>();
        // 从 UTF-8 字符串构造路径（Windows 上正确处理编码）
        fs::path filePath = pathFromUtf8String(pathStr);
        
        // 提取可选参数
        std::string mode = "overwrite";
        if (arguments.contains("mode") && arguments["mode"].is_string()) {
            mode = arguments["mode"].get<std::string>();
        }
        
        bool createDirs = false;
        if (arguments.contains("create_directories") && arguments["create_directories"].is_boolean()) {
            createDirs = arguments["create_directories"].get<bool>();
        }
        
        int startLine = 0;
        int endLine = -1;
        if (arguments.contains("start_line") && arguments["start_line"].is_number_integer()) {
            startLine = arguments["start_line"].get<int>();
        }
        if (arguments.contains("end_line") && arguments["end_line"].is_number_integer()) {
            endLine = arguments["end_line"].get<int>();
        }
        
        // 检查是否需要创建目录
        if (createDirs) {
            auto parentPath = filePath.parent_path();
            if (!parentPath.empty() && !fs::exists(parentPath)) {
                fs::create_directories(parentPath);
            }
        }
        
        // 处理行范围替换
        if (startLine > 0 && endLine > 0) {
            // 行范围替换模式
            if (!fs::exists(filePath)) {
                return nlohmann::json{{"error", "文件不存在，无法进行行范围替换: " + sanitizeUtf8String(pathStr)}};
            }
            
            // 读取原文件
            std::vector<std::string> lines = readFileLines(filePath);
            int totalLines = static_cast<int>(lines.size());
            
            // 验证行范围
            if (startLine > totalLines || endLine > totalLines) {
                return nlohmann::json{{"error", "行范围超出文件范围"}};
            }
            
            // 替换指定行
            std::vector<std::string> newContentLines;
            std::istringstream contentStream(content);
            std::string line;
            while (std::getline(contentStream, line)) {
                newContentLines.push_back(line);
            }
            
            // 构建新文件内容
            std::vector<std::string> newLines;
            for (int i = 0; i < totalLines; ++i) {
                int lineNum = i + 1; // 1-based
                if (lineNum >= startLine && lineNum <= endLine) {
                    // 替换范围内的行
                    int replaceIndex = lineNum - startLine;
                    if (replaceIndex < static_cast<int>(newContentLines.size())) {
                        newLines.push_back(newContentLines[replaceIndex]);
                    }
                } else {
                    newLines.push_back(lines[i]);
                }
            }
            
            // 写入文件（使用UTF-8模式）
            std::ofstream file(filePath, std::ios::out | std::ios::trunc | std::ios::binary);
            if (!file.is_open()) {
                return nlohmann::json{{"error", "无法打开文件进行写入: " + sanitizeUtf8String(pathStr)}};
            }
            
            size_t bytesWritten = 0;
            for (size_t i = 0; i < newLines.size(); ++i) {
                if (i > 0) {
                    file << "\n";
                    bytesWritten += 1;
                }
                file << newLines[i];
                bytesWritten += newLines[i].size();
            }
            file.close();
            
            nlohmann::json result;
            result["success"] = true;
            result["path"] = sanitizeUtf8String(pathStr);  // 使用原始路径字符串（已经是UTF-8）
            result["bytes_written"] = bytesWritten;
            result["mode"] = "line_replace";
            result["message"] = "成功替换行范围 " + std::to_string(startLine) + "-" + std::to_string(endLine);
            return result;
        }
        
        // 普通写入模式（使用二进制模式确保UTF-8内容正确写入）
        std::ios_base::openmode openMode = std::ios::out | std::ios::binary;
        if (mode == "append") {
            openMode |= std::ios::app;
        } else if (mode == "create_only") {
            if (fs::exists(filePath)) {
                return nlohmann::json{{"error", "文件已存在，无法使用 create_only 模式: " + sanitizeUtf8String(pathStr)}};
            }
            openMode |= std::ios::trunc;
        } else {
            // overwrite
            openMode |= std::ios::trunc;
        }
        
        std::ofstream file(filePath, openMode);
        if (!file.is_open()) {
            return nlohmann::json{{"error", "无法打开文件进行写入: " + sanitizeUtf8String(pathStr)}};
        }
        
        // 直接写入UTF-8内容（二进制模式）
        file.write(content.data(), content.size());
        size_t bytesWritten = content.size();
        file.close();
        
        nlohmann::json result;
        result["success"] = true;
        result["path"] = sanitizeUtf8String(pathStr);  // 使用原始路径字符串（已经是UTF-8）
        result["bytes_written"] = bytesWritten;
        result["mode"] = mode;
        result["message"] = "文件写入成功";
        return result;
        
    } catch (const std::exception& e) {
        // 清理错误消息中的无效UTF-8字符
        std::string errorMsg = std::string("写入文件失败: ") + e.what();
        return nlohmann::json{{"error", sanitizeUtf8String(errorMsg)}};
    } catch (...) {
        return nlohmann::json{{"error", "写入文件时发生未知错误"}};
    }
}

void CodeTools::registerWriteFileTool(ToolManager& toolManager) {
    ToolDefinition tool;
    tool.name = "write_file";
    tool.description = "写入文本文件。支持覆盖、追加、仅创建等模式，以及行范围替换。";
    tool.parametersSchema = nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "文件路径"}
            }},
            {"content", {
                {"type", "string"},
                {"description", "要写入的内容"}
            }},
            {"mode", {
                {"type", "string"},
                {"enum", {"overwrite", "append", "create_only"}},
                {"default", "overwrite"},
                {"description", "写入模式"}
            }},
            {"start_line", {
                {"type", "integer"},
                {"minimum", 1},
                {"description", "起始行号（用于行范围替换）"}
            }},
            {"end_line", {
                {"type", "integer"},
                {"minimum", 1},
                {"description", "结束行号（用于行范围替换）"}
            }},
            {"create_directories", {
                {"type", "boolean"},
                {"default", false},
                {"description", "是否自动创建目录"}
            }}
        }},
        {"required", {"path", "content"}}
    };
    tool.handler = handleWriteFile;
    tool.permissionLevel = PermissionLevel::Public;
    
    toolManager.registerTool(tool, true);
}

