# 工具与LLM集成说明

本文档说明如何将 ToolManager 的工具用于 LLM Function Calling，适配硅基流动等支持 OpenAI 兼容 Function Calling 的 LLM 服务提供商。

## 目录

1. [概述](#概述)
2. [架构设计](#架构设计)
3. [工具格式转换](#工具格式转换)
4. [工具选择策略](#工具选择策略)
5. [工具过滤机制](#工具过滤机制)
6. [集成方式](#集成方式)
7. [最佳实践](#最佳实践)
8. [常见问题](#常见问题)

## 概述

### 功能目标

将 ToolManager 中注册的工具转换为 OpenAI 兼容的 Function Calling 格式，并自动填充到 ChatRequest 中，使 LLM 能够识别和调用这些工具。

### 设计原则

1. **兼容性**：遵循 OpenAI Function Calling 规范，确保与硅基流动等 LLM 服务提供商兼容。
2. **灵活性**：支持工具过滤和工具选择策略，满足不同场景的需求。
3. **易用性**：提供便捷的 API，简化工具列表填充流程。
4. **向后兼容**：不影响现有功能，工具管理器可选。

## 架构设计

### 组件关系

```
┌─────────────────┐
│  ToolManager    │
│  - 工具注册      │
│  - 工具执行      │
│  - 格式转换      │
└────────┬────────┘
         │ getToolsForAPI()
         │ populateToolsToRequest()
         ▼
┌─────────────────┐
│  ChatRequest    │
│  - tools[]       │
│  - toolChoice   │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   APIClient     │
│   (硅基流动)     │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  ChatResponse   │
│  - toolCalls[]  │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│FunctionCalling  │
│   Handler       │
│  - 执行工具      │
│  - 构建后续请求  │
└─────────────────┘
```

### 数据流

1. **工具注册阶段**：工具注册到 ToolManager，包含名称、描述、参数 Schema 和处理器函数。
2. **请求构建阶段**：从 ToolManager 获取工具列表，转换为 OpenAI 格式，填充到 ChatRequest。
3. **LLM 处理阶段**：LLM 分析请求，决定是否调用工具，返回工具调用请求。
4. **工具执行阶段**：FunctionCallingHandler 执行工具，构建包含工具结果的后续请求。
5. **响应返回阶段**：LLM 处理工具结果，返回最终响应。

## 工具格式转换

### ToolDefinition 到 OpenAI 格式

ToolManager 中的 `ToolDefinition` 结构转换为 OpenAI Function Calling 格式：

```cpp
// ToolDefinition
{
    name: "read_file",
    description: "读取文件内容",
    parametersSchema: {
        type: "object",
        properties: {
            path: { type: "string" },
            start_line: { type: "integer" }
        },
        required: ["path"]
    }
}

// 转换为 OpenAI 格式
{
    "type": "function",
    "function": {
        "name": "read_file",
        "description": "读取文件内容",
        "parameters": {
            "type": "object",
            "properties": {
                "path": { "type": "string" },
                "start_line": { "type": "integer" }
            },
            "required": ["path"]
        }
    }
}
```

### 实现方法

```cpp
// 获取所有工具（OpenAI 格式）
std::vector<nlohmann::json> tools = toolManager.getToolsForAPI();

// 获取过滤后的工具
ToolFilter filter;
filter.permissionLevel = PermissionLevel::Public;
std::vector<nlohmann::json> filteredTools = toolManager.getToolsForAPI(filter);
```

## 工具选择策略

### 策略类型

工具选择策略通过 `toolChoice` 参数控制：

1. **"auto"**（默认）：让 LLM 自动决定是否调用工具
   ```cpp
   toolManager.populateToolsToRequest(request, {}, "auto");
   ```

2. **"none"**：不调用任何工具
   ```cpp
   toolManager.populateToolsToRequest(request, {}, "none");
   ```

3. **特定工具名**：强制调用指定工具
   ```cpp
   toolManager.populateToolsToRequest(request, {}, "read_file");
   ```

### 在 ChatRequest 中的表示

```cpp
// "auto" 或 "none"
request.toolChoice = "auto";  // 或 "none"

// 特定工具名（简化实现，直接使用工具名）
request.toolChoice = "read_file";
```

**注意**：根据 OpenAI 规范，特定工具名应该使用对象格式：
```json
{
  "type": "function",
  "function": {
    "name": "read_file"
  }
}
```

当前实现使用字符串格式以简化处理。如果需要完整的对象格式，可以在 `ChatRequest::toJson()` 方法中进行特殊处理。

## 工具过滤机制

### 过滤条件

工具过滤通过 `ToolFilter` 结构定义：

```cpp
struct ToolFilter {
    std::optional<std::string> namePrefix;        // 名称前缀过滤
    std::optional<PermissionLevel> permissionLevel; // 权限级别过滤
};
```

### 使用示例

```cpp
// 只暴露 Public 权限的工具
ToolFilter filter;
filter.permissionLevel = PermissionLevel::Public;
toolManager.populateToolsToRequest(request, filter, "auto");

// 只暴露名称以 "code_" 开头的工具
ToolFilter prefixFilter;
prefixFilter.namePrefix = "code_";
toolManager.populateToolsToRequest(request, prefixFilter, "auto");

// 组合过滤（同时满足多个条件）
ToolFilter combinedFilter;
combinedFilter.namePrefix = "code_";
combinedFilter.permissionLevel = PermissionLevel::Public;
toolManager.populateToolsToRequest(request, combinedFilter, "auto");
```

### 过滤逻辑

- **名称前缀过滤**：工具名称必须以指定前缀开头
- **权限级别过滤**：工具的权限级别必须匹配指定级别
- **组合过滤**：同时应用多个过滤条件（AND 逻辑）

## 集成方式

### 方式 1：直接使用 ToolManager

```cpp
ToolManager toolManager;
// ... 注册工具 ...

ChatRequest request;
request.model = "deepseek-chat";
request.messages.push_back(ChatMessage::user("Hello"));

// 直接填充工具列表
toolManager.populateToolsToRequest(request);
```

### 方式 2：通过 ContextManager

```cpp
ConfigManager configManager;
ContextManager contextManager(configManager);
contextManager.setToolManager(&toolManager);

ChatRequest request;
// ... 构建请求 ...

// 通过 ContextManager 填充工具列表
contextManager.populateToolsToRequest(request);
```

### 方式 3：在请求构建流程中自动填充

可以在 RequestManager 或上层服务中，在构建 ChatRequest 时自动从 ToolManager 获取工具列表并填充。

## 最佳实践

### 1. 工具定义

#### 工具名称

- 使用清晰、描述性的名称
- 使用下划线分隔单词（如 `read_file`、`search_code`）
- 避免使用保留字或特殊字符

#### 工具描述

- 清晰说明工具的用途和功能
- 包含使用场景和示例
- 帮助 LLM 理解何时使用该工具

```cpp
tool.description = "读取文件内容。支持读取完整文件或指定行范围。"
                  "适用于需要查看代码文件、配置文件等场景。";
```

#### 参数 Schema

- 使用完整的 JSON Schema 定义
- 明确指定参数类型、必需字段和约束
- 为每个参数提供描述

```cpp
tool.parametersSchema = nlohmann::json{
    {"type", "object"},
    {"properties", {
        {"path", {
            {"type", "string"},
            {"description", "要读取的文件路径（相对路径或绝对路径）"}
        }},
        {"start_line", {
            {"type", "integer"},
            {"description", "起始行号（从1开始，可选）"},
            {"minimum", 1}
        }},
        {"end_line", {
            {"type", "integer"},
            {"description", "结束行号（可选）"},
            {"minimum", 1}
        }}
    }},
    {"required", {"path"}}
};
```

### 2. 工具权限管理

根据工具的安全性和敏感性设置适当的权限级别：

- **Public**：公开工具，所有用户可用（如 `read_file`、`list_files`）
- **Restricted**：受限工具，需要特定权限（如 `write_file`、`execute_command`）
- **Admin**：管理员工具，仅管理员可用（如 `delete_file`、`system_config`）

```cpp
tool.permissionLevel = PermissionLevel::Restricted;
```

### 3. 工具过滤策略

- **最小权限原则**：只暴露必要的工具给 LLM
- **按场景过滤**：根据任务类型选择不同的工具集
- **按权限过滤**：只暴露用户有权限使用的工具

```cpp
// 代码讨论场景：只暴露代码相关工具
ToolFilter codeFilter;
codeFilter.namePrefix = "code_";
contextManager.populateToolsToRequest(request, codeFilter);
```

### 4. 错误处理

工具执行时应该：

- 捕获所有异常
- 返回清晰的错误信息
- 记录错误日志（可选）

```cpp
tool.handler = [](const nlohmann::json& args) -> nlohmann::json {
    try {
        // 工具执行逻辑
        return result;
    } catch (const std::exception& e) {
        nlohmann::json error;
        error["error"] = std::string("工具执行失败: ") + e.what();
        return error;
    }
};
```

### 5. 多轮工具调用

- 设置最大迭代次数，防止无限循环
- 监控工具调用链，检测循环调用
- 提供取消机制

```cpp
int maxIterations = 5;
int iteration = 0;
while (iteration < maxIterations && hasToolCalls) {
    // 处理工具调用
    iteration++;
}
```

## 常见问题

### Q1: 工具列表没有填充到请求中？

**A**: 检查以下几点：
1. 是否调用了 `populateToolsToRequest()` 方法
2. ToolManager 中是否注册了工具
3. 工具过滤条件是否过于严格

### Q2: LLM 没有调用工具？

**A**: 可能的原因：
1. 工具描述不够清晰，LLM 无法理解何时使用
2. 工具选择策略设置为 "none"
3. 用户请求不够明确，LLM 认为不需要调用工具

### Q3: 工具执行失败？

**A**: 检查：
1. 工具参数是否正确（类型、必需字段）
2. 工具处理器是否抛出异常
3. 工具是否有执行权限

### Q4: 如何调试工具调用？

**A**: 可以：
1. 打印工具列表：`std::cout << request.tools.dump(2) << std::endl;`
2. 打印工具调用：`std::cout << response.toolCalls.size() << std::endl;`
3. 打印工具执行结果：`std::cout << result.toJson().dump(2) << std::endl;`

### Q5: 工具选择策略的特定工具名格式？

**A**: 当前实现使用字符串格式（简化实现）。如果需要完整的 OpenAI 对象格式，可以在 `ChatRequest::toJson()` 方法中进行特殊处理。

## 参考

- [Function Calling 使用示例](../examples/FunctionCalling使用示例.md)
- [硅基流动 Function Calling 文档](https://docs.siliconflow.cn/cn/userguide/guides/function-calling)
- [OpenAI Function Calling 规范](https://platform.openai.com/docs/guides/function-calling)
- [ToolManager API 文档](../../include/naw/desktop_pet/service/ToolManager.h)
- [FunctionCallingHandler API 文档](../../include/naw/desktop_pet/service/FunctionCallingHandler.h)

