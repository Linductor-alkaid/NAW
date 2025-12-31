#include "naw/desktop_pet/service/CodeTools.h"

#include "naw/desktop_pet/service/ToolManager.h"

namespace naw::desktop_pet::service {

// ========== 统一工具注册接口 ==========

void CodeTools::registerAllTools(ToolManager& toolManager) {
    registerReadFileTool(toolManager);
    registerWriteFileTool(toolManager);
    registerListFilesTool(toolManager);
    registerSearchCodeTool(toolManager);
    registerGetProjectStructureTool(toolManager);
    registerAnalyzeCodeTool(toolManager);
}

} // namespace naw::desktop_pet::service
