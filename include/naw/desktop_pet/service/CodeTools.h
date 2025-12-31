#pragma once

#include "naw/desktop_pet/service/ToolManager.h"

namespace naw::desktop_pet::service {

// 前向声明
class ToolManager;

/**
 * @brief 代码工具集
 *
 * 提供代码开发相关的标准工具，包括文件读取、文件列表、代码搜索、
 * 项目结构分析和代码分析等。这些工具为LLM提供代码操作能力。
 */
class CodeTools {
public:
    /**
     * @brief 注册所有代码工具到 ToolManager
     * @param toolManager 工具管理器实例
     */
    static void registerAllTools(ToolManager& toolManager);

private:
    /**
     * @brief 注册 read_file 工具
     */
    static void registerReadFileTool(ToolManager& toolManager);

    /**
     * @brief 注册 write_file 工具
     */
    static void registerWriteFileTool(ToolManager& toolManager);

    /**
     * @brief 注册 list_files 工具
     */
    static void registerListFilesTool(ToolManager& toolManager);

    /**
     * @brief 注册 search_code 工具
     */
    static void registerSearchCodeTool(ToolManager& toolManager);

    /**
     * @brief 注册 get_project_structure 工具
     */
    static void registerGetProjectStructureTool(ToolManager& toolManager);

    /**
     * @brief 注册 quick_project_scan 工具（可选）
     */
    static void registerQuickProjectScanTool(ToolManager& toolManager);

    /**
     * @brief 注册 analyze_code 工具
     */
    static void registerAnalyzeCodeTool(ToolManager& toolManager);
};

} // namespace naw::desktop_pet::service
