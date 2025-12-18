# Phase 2：API客户端层（API Client Layer）详细任务清单

本文档是阶段二 API 客户端层的详细开发任务清单，基于《服务层设计方案》制定，并以 Phase1 已完成的基础设施层（HttpClient/Config/Error/Types 等）为依赖。

> **参考信息**：
> - 设计方案：`docs/design/服务层设计方案.md`
> - Phase1 详细清单：`docs/todolists/service_layer/TODO_Phase1_基础设施层.md`
> - 总体任务清单：`docs/todolists/TODO_服务层开发任务清单.md`
> - SiliconFlow ChatCompletions API：`https://docs.siliconflow.cn/cn/api-reference/chat-completions/chat-completions`

## 概述

### 目标
- 提供 **SiliconFlow/OpenAI 兼容** 的 Chat Completions API 客户端封装。
- 覆盖三种调用方式：**同步 / 异步 / 流式（SSE）**。
- 覆盖 Function Calling：请求侧 `tools` / `tool_choice` 构建、响应侧 `tool_calls` 解析（含流式增量合并）。
- 与 Phase1 的 `ConfigManager`、`ErrorHandler`、`HttpClient`、`types` 无缝集成。

### 非目标（明确留到后续Phase）
- 模型管理/路由/上下文/队列/缓存等（Phase3/4/7）。
- STT/TTS/VLM 多端点封装（Phase6）。
- MCP Server、ToolManager、项目上下文采集（Phase5）。

### 依赖与关键缺口（Phase2 前置）
- [x] Phase1 已提供：
  - `utils::HttpClient` 支持同步/异步请求、基础重试；并提供 `StreamHandler`（按块回调）用于流式读取。
  - `service::ErrorHandler` 提供从 `HttpResponse` 生成 `ErrorInfo`，并支持基于 `Retry-After` 的退避策略。
  - `types::ChatMessage/ChatRequest/ChatResponse/Tool...` 等 OpenAI 兼容 JSON 结构与序列化/反序列化基础。
- [!] **必须补齐的缺口（Phase2 需要推动实现）**：
  - 当前 `HttpClient` 的流式读取仅对 **GET** 生效（`Get(..., recv)`），而 SiliconFlow 的流式 chat 是 **POST**。
  - Phase2 必须新增：`postStream(...)` 或通用 `executeStream(HttpRequest)`，让 **POST 也能边读边回调**。

---

## 2.1 SiliconFlow API客户端核心（SiliconFlowAPIClient）

### 2.1.1 任务概述
实现一个面向 SiliconFlow 的 API 客户端：读取配置、统一鉴权、发起 HTTP 请求并解析返回，向上提供同步/异步/流式三套接口。

### 2.1.2 文件结构（建议）
> 若你希望严格对齐设计方案命名：`APIClient.h/.cpp`；若你希望更明确：`SiliconFlowAPIClient.h/.cpp`。

建议新增：
```
include/naw/desktop_pet/service/
└── SiliconFlowAPIClient.h

src/naw/desktop_pet/service/
└── SiliconFlowAPIClient.cpp
```

### 2.1.3 详细任务清单

#### 2.1.3.1 客户端构造与配置绑定
- [ ] **读取配置**（通过 `ConfigManager`）
  - [ ] 读取 `api.base_url`（默认 `https://api.siliconflow.cn/v1`）
  - [ ] 读取 `api.api_key`（支持 `${SILICONFLOW_API_KEY}` 形式）
  - [ ] 读取 `api.default_timeout_ms`
- [ ] **初始化底层 HttpClient**
  - [ ] 统一设置默认请求头：
    - [ ] `Authorization: Bearer <api_key>`
    - [ ] `Content-Type: application/json`
    - [ ] `Accept: application/json`
  - [ ] 超时策略：默认用配置覆盖 `HttpClient::setTimeout()`
- [ ] **敏感信息保护**
  - [ ] 禁止在日志中打印 api_key（复用 ConfigManager 的 redact 或 ErrorHandler context 控制）

**验收标准**：在不发请求的情况下，可通过单元测试验证读取/拼装后的 base_url、headers、timeout 结果正确。

#### 2.1.3.2 同步 Chat Completions
- [ ] **接口定义**（复用 `types::ChatRequest/ChatResponse`）
  - [ ] `ChatResponse chat(const ChatRequest& request)`
- [ ] **请求序列化**
  - [ ] `ChatRequest::toJson()` → JSON string（使用 Phase1 `utils::toJsonBody`）
- [ ] **HTTP调用**
  - [ ] `POST /chat/completions`
- [ ] **响应解析**
  - [ ] 200：`ChatResponse::fromJson()`
  - [ ] 非200：ErrorHandler 解析 error body + status code，抛出/返回错误（统一策略见 2.4）

**验收标准**：
- 使用 mock HttpClient（或注入可替换 transport）输入典型响应 JSON，能得到正确 `ChatResponse`（content/tool_calls/usage）。

#### 2.1.3.3 异步 Chat Completions
- [ ] **接口定义**
  - [ ] `std::future<ChatResponse> chatAsync(const ChatRequest& request)`
- [ ] **取消策略**
  - [ ] 若需要：暴露 `HttpClient::CancelToken` 并定义取消后的错误语义（例如 statusCode=0 + error="Cancelled" → ErrorType::Unknown/Network 的映射规则）
  - [ ] 若短期不做：在文档中明确“异步不提供中途取消”，仅支持超时

**验收标准**：单测验证 future 返回、异常传播/错误返回符合约定。

#### 2.1.3.4 流式 Chat Completions（SSE）
- [ ] **补齐 POST 流式能力（关键前置）**
  - [ ] 在 `utils::HttpClient` 中新增 `postStream(...)`（或 `executeStream(HttpRequest)`）
  - [ ] 允许 `HttpRequest::streamHandler` 在 POST 场景下被逐块调用
  - [ ] 明确：流式时 `HttpResponse.body` 可能为空（只走回调），或只保留尾部/聚合结果
- [ ] **SSE解析器（建议独立类）**
  - [ ] 解析规则：
    - [ ] 以 `\n\n` 分隔 event
    - [ ] 支持多行 `data:` 拼接
    - [ ] 处理 `data: [DONE]`
    - [ ] 处理断行/粘包（chunk 可能任意切割）
  - [ ] 输出：每个 event 的 JSON payload（string 或 json）
- [ ] **增量聚合与回调协议**
  - [ ] `onTextDelta(std::string_view)`
  - [ ] `onToolCallDelta(ToolCallDelta)`（可选，但建议，为Function Calling流式增量合并做准备）
  - [ ] `onComplete(ChatResponse)`
  - [ ] `onError(ErrorInfo)`
- [ ] **与 ChatResponse 形态对齐**
  - [ ] 依据 OpenAI 兼容 shape，把 `choices[].delta.content` 聚合到最终 content
  - [ ] 对 `tool_calls` 的增量合并：按 index/id 聚合 name/arguments

**验收标准**：
- 给定一组切碎的 SSE chunk（单测输入），最终聚合 content 与 tool_calls 与预期一致。

---

## 2.2 API请求构建（ChatRequestBuilder）

### 2.2.1 任务概述
将“上层想表达的请求”（模型、messages、采样参数、工具定义等）稳定地转成 SiliconFlow/OpenAI 兼容 JSON。

### 2.2.2 详细任务清单
- [ ] **参数规范化**
  - [ ] 字段名统一（snake_case 输出）：`max_tokens/top_p/top_k/tool_choice` 等
  - [ ] 合法性校验：范围、空值、默认值填充
- [ ] **messages构建**
  - [ ] 复用 `types::ChatMessage::toJson()`（支持 content string 与 content array）
- [ ] **Function Calling 构建**
  - [ ] `tools` 序列化：复用 `types::Tool::toJson()`（OpenAI兼容 schema）
  - [ ] `tool_choice`：支持 `auto/none/指定function` 三种

**验收标准**：单测校验生成 JSON 的字段形态与设计方案一致（含 tools/tool_choice）。

---

## 2.3 API响应解析（ChatResponseParser）

### 2.3.1 任务概述
统一解析三类响应：非流式标准响应、非流式工具调用响应、流式SSE增量响应。

### 2.3.2 详细任务清单
- [ ] **标准响应解析**
  - [ ] 提取 content
  - [ ] 解析 finish_reason
  - [ ] 解析 usage（prompt/completion/total tokens）
- [ ] **Function Calling 响应解析**
  - [ ] 提取 `tool_calls`（id/type/function{name,arguments}）
  - [ ] arguments 容错：
    - [ ] 若为字符串但不是合法JSON：保留原文 + 标记 parse_error
- [ ] **流式聚合器**
  - [ ] content delta 合并
  - [ ] tool_calls delta 合并
  - [ ] 完成时构建最终 `ChatResponse`

**验收标准**：覆盖至少：纯文本、纯工具调用、文本+工具混合、arguments 非法JSON 的容错。

---

## 2.4 错误处理与重试集成

### 2.4.1 任务概述
把 HTTP/JSON/业务错误统一映射到 `ErrorInfo/ErrorType`，并接入重试/退避策略。

### 2.4.2 详细任务清单
- [ ] **错误统一入口**
  - [ ] 复用 `ErrorHandler::fromHttpResponse()`
  - [ ] 对 JSON parse error 也生成 `ErrorInfo`（标记为 InvalidRequest/Unknown）
- [ ] **429 Retry-After**
  - [ ] header 优先：`Retry-After`（秒）
  - [ ] 无 header 时走 ErrorHandler 默认退避
- [ ] **上下文信息**
  - [ ] 在 ErrorInfo.context 写入：model、endpoint、attempt、requestId（若后续加）

**验收标准**：单测验证 401/403/429/5xx/transport_error 的分类、重试判定与延迟计算。

---

## 2.5 单元测试与示例

### 2.5.1 单元测试
- [ ] **请求序列化测试**：ChatRequest + tools → JSON
- [ ] **响应解析测试**：标准/工具/混合
- [ ] **SSE解析与聚合测试**：
  - [ ] 分片、断行、粘包
  - [ ] `[DONE]` 终止
  - [ ] tool_calls delta 合并
- [ ] **错误映射与重试测试**：429 Retry-After 优先

### 2.5.2 示例程序
- [ ] 新增示例：`src/naw/desktop_pet/service/utils/examples/api_client_example.cpp`
  - [ ] 非流式 chat
  - [ ] 流式 chat（打印增量）
  - [ ] Function Calling（构造一个简单tool并验证 tool_calls 解析）

**验收标准**：示例可编译运行；在配置正确时能够打通真实 SiliconFlow；在无网络/无key时输出可理解错误。

---

## 开发顺序建议

1. 先补齐 `HttpClient` 的 **POST流式** 支撑（否则 SSE 无法落地）。
2. 实现 `SiliconFlowAPIClient` 的非流式同步/异步。
3. 实现 SSE 解析器与流式聚合器。
4. 接入 Function Calling 的请求构建与响应解析（含流式 delta）。
5. 补齐单元测试与示例。

---

*最后更新: 2025年12月18日*
