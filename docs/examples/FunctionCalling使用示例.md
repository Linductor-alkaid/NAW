# Function Calling 使用示例

本文档提供完整的 Function Calling 使用示例，展示如何将 ToolManager 的工具用于 LLM Function Calling。

## 目录

1. [基本使用流程](#基本使用流程)
2. [注册工具](#注册工具)
3. [构建包含工具的请求](#构建包含工具的请求)
4. [处理工具调用](#处理工具调用)
5. [多轮工具调用](#多轮工具调用)
6. [工具过滤和选择策略](#工具过滤和选择策略)
7. [完整示例代码](#完整示例代码)

## 基本使用流程

Function Calling 的基本流程如下：

```
1. 注册工具到 ToolManager
2. 构建 ChatRequest，填充工具列表
3. 发送请求到 LLM（硅基流动）
4. LLM 返回工具调用请求
5. 执行工具并构建后续请求
6. 发送后续请求，获取最终响应
```

## 注册工具

### 示例 1：注册简单工具

```cpp
#include "naw/desktop_pet/service/ToolManager.h"
#include "naw/desktop_pet/service/CodeTools.h"

using namespace naw::desktop_pet::service;

// 创建工具管理器
ToolManager toolManager;

// 注册代码工具（CodeTools 提供标准工具集）
CodeTools::registerAllTools(toolManager);

// 或者手动注册单个工具
ToolDefinition tool;
tool.name = "get_weather";
tool.description = "获取指定城市的天气信息";
tool.parametersSchema = nlohmann::json{
    {"type", "object"},
    {"properties", {
        {"city", {{"type", "string"}, {"description", "城市名称"}}},
        {"unit", {{"type", "string"}, {"enum", {"celsius", "fahrenheit"}}, {"default", "celsius"}}}
    }},
    {"required", {"city"}}
};
tool.handler = [](const nlohmann::json& args) -> nlohmann::json {
    std::string city = args["city"].get<std::string>();
    // 实现天气查询逻辑
    nlohmann::json result;
    result["city"] = city;
    result["temperature"] = 25;
    result["condition"] = "sunny";
    return result;
};

toolManager.registerTool(tool);
```

## 构建包含工具的请求

### 方法 1：使用 ToolManager 直接填充

```cpp
#include "naw/desktop_pet/service/types/RequestResponse.h"

using namespace naw::desktop_pet::service;
using namespace naw::desktop_pet::service::types;

// 创建请求
ChatRequest request;
request.model = "deepseek-chat";
request.messages.push_back(ChatMessage::user("请帮我读取文件 config.json"));

// 填充工具列表（自动模式）
toolManager.populateToolsToRequest(request, {}, "auto");

// 或者指定特定工具
toolManager.populateToolsToRequest(request, {}, "read_file");

// 或者不使用工具
toolManager.populateToolsToRequest(request, {}, "none");
```

### 方法 2：使用 ContextManager 填充

```cpp
#include "naw/desktop_pet/service/ContextManager.h"
#include "naw/desktop_pet/service/ConfigManager.h"

ConfigManager configManager;
ContextManager contextManager(configManager);

// 设置工具管理器
contextManager.setToolManager(&toolManager);

// 构建上下文
ContextConfig config;
config.taskType = types::TaskType::CodeDiscussion;
auto messages = contextManager.buildContext(
    config,
    "请帮我读取文件 config.json",
    "deepseek-chat"
);

// 创建请求
ChatRequest request;
request.model = "deepseek-chat";
request.messages = messages;

// 通过 ContextManager 填充工具列表
contextManager.populateToolsToRequest(request);
```

### 工具过滤示例

```cpp
// 只暴露 Public 权限的工具
ToolFilter filter;
filter.permissionLevel = PermissionLevel::Public;
toolManager.populateToolsToRequest(request, filter, "auto");

// 只暴露名称以 "code_" 开头的工具
ToolFilter prefixFilter;
prefixFilter.namePrefix = "code_";
toolManager.populateToolsToRequest(request, prefixFilter, "auto");
```

## 处理工具调用

### 发送请求并处理工具调用

```cpp
#include "naw/desktop_pet/service/APIClient.h"
#include "naw/desktop_pet/service/FunctionCallingHandler.h"

APIClient apiClient(configManager);

// 1. 发送请求
ChatResponse response = apiClient.chat(request);

// 2. 检查是否有工具调用
if (FunctionCallingHandler::hasToolCalls(response)) {
    // 3. 处理工具调用（自动执行并构建后续请求）
    ErrorInfo error;
    auto followUpRequest = FunctionCallingHandler::processToolCalls(
        response,
        request,
        toolManager,
        &error
    );
    
    if (followUpRequest.has_value()) {
        // 4. 发送后续请求
        ChatResponse finalResponse = apiClient.chat(*followUpRequest);
        std::cout << "最终响应: " << finalResponse.content << std::endl;
    } else {
        std::cerr << "工具调用处理失败: " << error.message << std::endl;
    }
} else {
    // 没有工具调用，直接使用响应
    std::cout << "响应: " << response.content << std::endl;
}
```

### 手动处理工具调用

```cpp
// 1. 提取工具调用
auto toolCalls = FunctionCallingHandler::extractToolCalls(response);

// 2. 执行工具调用
auto results = FunctionCallingHandler::executeToolCalls(toolCalls, toolManager);

// 3. 构建工具结果消息
auto toolResultMessages = FunctionCallingHandler::buildToolResultMessages(results);

// 4. 构建后续请求
auto followUpRequest = FunctionCallingHandler::buildFollowUpRequest(
    request.messages,
    toolResultMessages,
    request
);

// 5. 发送后续请求
ChatResponse finalResponse = apiClient.chat(followUpRequest);
```

## 多轮工具调用

LLM 可能需要进行多轮工具调用才能完成任务。处理方式如下：

```cpp
ChatRequest currentRequest = request;
int maxIterations = 5; // 防止无限循环
int iteration = 0;

while (iteration < maxIterations) {
    // 发送请求
    ChatResponse response = apiClient.chat(currentRequest);
    
    // 检查是否有工具调用
    if (!FunctionCallingHandler::hasToolCalls(response)) {
        // 没有工具调用，任务完成
        std::cout << "最终响应: " << response.content << std::endl;
        break;
    }
    
    // 处理工具调用
    ErrorInfo error;
    auto followUpRequest = FunctionCallingHandler::processToolCalls(
        response,
        currentRequest,
        toolManager,
        &error
    );
    
    if (!followUpRequest.has_value()) {
        std::cerr << "工具调用处理失败: " << error.message << std::endl;
        break;
    }
    
    // 更新当前请求
    currentRequest = *followUpRequest;
    iteration++;
}

if (iteration >= maxIterations) {
    std::cerr << "达到最大迭代次数，可能陷入循环" << std::endl;
}
```

## 工具过滤和选择策略

### 工具选择策略

```cpp
// "auto"：让 LLM 自动决定是否调用工具（默认）
toolManager.populateToolsToRequest(request, {}, "auto");

// "none"：不调用任何工具
toolManager.populateToolsToRequest(request, {}, "none");

// 特定工具名：强制调用指定工具
toolManager.populateToolsToRequest(request, {}, "read_file");
```

### 工具过滤

```cpp
// 按权限级别过滤
ToolFilter filter;
filter.permissionLevel = PermissionLevel::Public;
toolManager.populateToolsToRequest(request, filter, "auto");

// 按名称前缀过滤
ToolFilter prefixFilter;
prefixFilter.namePrefix = "code_";
toolManager.populateToolsToRequest(request, prefixFilter, "auto");

// 组合过滤（同时应用多个条件）
ToolFilter combinedFilter;
combinedFilter.namePrefix = "code_";
combinedFilter.permissionLevel = PermissionLevel::Public;
toolManager.populateToolsToRequest(request, combinedFilter, "auto");
```

## 完整示例代码

以下是一个完整的示例，展示如何使用 Function Calling 完成代码文件读取任务：

```cpp
#include "naw/desktop_pet/service/ToolManager.h"
#include "naw/desktop_pet/service/CodeTools.h"
#include "naw/desktop_pet/service/APIClient.h"
#include "naw/desktop_pet/service/FunctionCallingHandler.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"

#include <iostream>

using namespace naw::desktop_pet::service;
using namespace naw::desktop_pet::service::types;

int main() {
    // 1. 初始化组件
    ConfigManager configManager;
    APIClient apiClient(configManager);
    ToolManager toolManager;
    
    // 2. 注册代码工具
    CodeTools::registerAllTools(toolManager);
    
    // 3. 构建请求
    ChatRequest request;
    request.model = "deepseek-chat";
    request.messages.push_back(ChatMessage::user("请帮我读取文件 README.md 的前 10 行"));
    
    // 4. 填充工具列表
    toolManager.populateToolsToRequest(request, {}, "auto");
    
    // 5. 发送请求
    ChatResponse response = apiClient.chat(request);
    
    // 6. 处理工具调用
    if (FunctionCallingHandler::hasToolCalls(response)) {
        std::cout << "检测到工具调用，正在处理..." << std::endl;
        
        ErrorInfo error;
        auto followUpRequest = FunctionCallingHandler::processToolCalls(
            response,
            request,
            toolManager,
            &error
        );
        
        if (followUpRequest.has_value()) {
            // 7. 发送后续请求
            ChatResponse finalResponse = apiClient.chat(*followUpRequest);
            std::cout << "最终响应:\n" << finalResponse.content << std::endl;
        } else {
            std::cerr << "错误: " << error.message << std::endl;
            return 1;
        }
    } else {
        std::cout << "响应:\n" << response.content << std::endl;
    }
    
    return 0;
}
```

## 错误处理

### 工具不存在

```cpp
ErrorInfo error;
bool success = toolManager.populateToolsToRequest(request, {}, "nonexistent_tool", &error);
if (!success) {
    std::cerr << "错误: " << error.message << std::endl;
    // 错误类型: InvalidRequest
    // 错误代码: 404
}
```

### 工具执行失败

```cpp
auto results = FunctionCallingHandler::executeToolCalls(toolCalls, toolManager);
for (const auto& result : results) {
    if (!result.success) {
        std::cerr << "工具 " << result.toolName << " 执行失败: " 
                  << result.error.value_or("未知错误") << std::endl;
    }
}
```

## 最佳实践

1. **工具描述要清晰**：工具的描述应该清楚地说明工具的用途和适用场景，帮助 LLM 正确选择工具。

2. **参数 Schema 要完整**：确保参数 Schema 包含完整的类型定义、必需字段和约束条件。

3. **错误处理要完善**：工具执行时应该捕获异常并返回清晰的错误信息。

4. **工具权限要合理**：根据工具的安全性和敏感性设置适当的权限级别。

5. **工具过滤要谨慎**：只暴露必要的工具给 LLM，避免暴露敏感或危险的工具。

6. **多轮调用要限制**：设置最大迭代次数，防止无限循环。

## 参考

- [工具与LLM集成说明](../design/工具与LLM集成说明.md)
- [硅基流动 Function Calling 文档](https://docs.siliconflow.cn/cn/userguide/guides/function-calling)
- [ToolManager API 文档](../../include/naw/desktop_pet/service/ToolManager.h)

