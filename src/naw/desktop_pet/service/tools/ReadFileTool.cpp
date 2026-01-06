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

static nlohmann::json handleReadFile(const nlohmann::json& arguments) {
    try {
        // 提取参数
        if (!arguments.contains("path") || !arguments["path"].is_string()) {
            return nlohmann::json{{"error", "缺少必需参数: path"}};
        }
        
        std::string pathStr = arguments["path"].get<std::string>();
        // 从 UTF-8 字符串构造路径（Windows 上正确处理编码）
        fs::path filePath = pathFromUtf8String(pathStr);
        
        // 检查文件是否存在
        if (!fs::exists(filePath)) {
            // 使用原始路径字符串（已经是UTF-8）
            return nlohmann::json{{"error", "文件不存在: " + sanitizeUtf8String(pathStr)}};
        }
        
        if (!fs::is_regular_file(filePath)) {
            // 使用原始路径字符串（已经是UTF-8）
            return nlohmann::json{{"error", "路径不是文件: " + sanitizeUtf8String(pathStr)}};
        }
        
        // 检查文件大小
        if (isFileTooLarge(filePath)) {
            return nlohmann::json{{"error", "文件过大（超过10MB）: " + pathStr}};
        }
        
        // 提取行范围参数
        int startLine = 0;
        int endLine = -1;
        
        if (arguments.contains("start_line") && arguments["start_line"].is_number_integer()) {
            startLine = arguments["start_line"].get<int>();
        }
        if (arguments.contains("end_line") && arguments["end_line"].is_number_integer()) {
            endLine = arguments["end_line"].get<int>();
        }
        
        // 验证行范围
        int totalLines = countFileLines(filePath);
        if (startLine > 0 && startLine > totalLines) {
            return nlohmann::json{{"error", "起始行号超出文件范围"}};
        }
        if (endLine > 0 && endLine < startLine) {
            return nlohmann::json{{"error", "结束行号小于起始行号"}};
        }
        
        // 读取文件
        std::vector<std::string> lines;
        if (startLine > 0 || endLine > 0) {
            lines = readFileLines(filePath, startLine, endLine);
        } else {
            lines = readFileLines(filePath);
        }
        
        // 构建结果
        std::ostringstream content;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i > 0) content << "\n";
            content << lines[i];
        }
        
        nlohmann::json result;
        result["content"] = content.str();
        // 使用原始路径字符串（已经是UTF-8），并清理无效UTF-8字符，确保JSON序列化成功
        result["path"] = sanitizeUtf8String(pathStr);
        result["line_count"] = totalLines;
        
        if (startLine > 0) {
            result["start_line"] = startLine;
        }
        if (endLine > 0) {
            result["end_line"] = endLine;
        }
        
        return result;
        
    } catch (const std::exception& e) {
        // 清理错误消息中的无效UTF-8字符
        std::string errorMsg = std::string("读取文件失败: ") + e.what();
        return nlohmann::json{{"error", sanitizeUtf8String(errorMsg)}};
    } catch (...) {
        return nlohmann::json{{"error", "读取文件时发生未知错误"}};
    }
}

void CodeTools::registerReadFileTool(ToolManager& toolManager) {
    ToolDefinition tool;
    tool.name = "read_file";
    tool.description = "读取文本文件内容。支持读取完整文件或指定行范围。";
    tool.parametersSchema = nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "文件路径"}
            }},
            {"start_line", {
                {"type", "integer"},
                {"minimum", 1},
                {"description", "起始行号（从1开始）"}
            }},
            {"end_line", {
                {"type", "integer"},
                {"minimum", 1},
                {"description", "结束行号"}
            }}
        }},
        {"required", {"path"}}
    };
    tool.handler = handleReadFile;
    tool.permissionLevel = PermissionLevel::Public;
    
    toolManager.registerTool(tool, true);
}

