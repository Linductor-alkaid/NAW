# Phase 4：服务管理层（Service Management Layer）详细任务清单

本文档是阶段四服务管理层的详细开发任务清单，基于《服务层设计方案》制定，并以 Phase1、Phase2、Phase3 已完成的基础设施层、API 客户端层和核心管理层为依赖。

> **参考信息**：
> - 设计方案：`docs/design/服务层设计方案.md`
> - Phase1 详细清单：`docs/todolists/service_layer/TODO_Phase1_基础设施层.md`
> - Phase2 详细清单：`docs/todolists/service_layer/TODO_Phase2_API客户端层.md`
> - Phase3 详细清单：`docs/todolists/service_layer/TODO_Phase3_核心管理层.md`
> - 总体任务清单：`docs/todolists/TODO_服务层开发任务清单.md`

## 概述

### 目标
- 实现 **请求管理器（RequestManager）**：管理请求队列、并发控制和请求调度，确保API调用高效且不超出限流。
- 实现 **响应处理器（ResponseHandler）**：统一处理API响应，包括流式响应解析、响应验证和缓存集成。
- 实现 **缓存管理器（CacheManager）**：缓存常见请求的响应，减少API调用次数，提高响应速度。

### 非目标（明确留到后续Phase）
- 工具调用/MCP服务（Phase5）。
- 多模态服务（Phase6）。
- 服务层主接口整合（Phase7）。

### 依赖与关键组件（Phase4 前置）
- [x] Phase1 已提供：
  - `ConfigManager`：配置文件加载和环境变量支持。
  - `types::TaskType`、`types::TaskPriority`、`types::ChatRequest`、`types::ChatResponse` 等基础数据结构。
  - `ErrorHandler`：统一错误处理和重试策略。
  - `utils::TokenCounter`：Token计数工具。
- [x] Phase2 已提供：
  - `APIClient`：同步/异步/流式 API 调用能力。
- [x] Phase3 已提供：
  - `ModelManager`：模型配置和性能统计。
  - `TaskRouter`：任务路由和模型选择。
  - `ContextManager`：上下文构建和管理。

---

## 4.1 请求管理器（RequestManager）

### 4.1.1 任务概述
实现请求队列管理、并发控制、请求调度和超时管理，确保API调用高效且不超出限流。

### 4.1.2 文件结构（建议）
```
include/naw/desktop_pet/service/
└── RequestManager.h

src/naw/desktop_pet/service/
└── RequestManager.cpp
```

### 4.1.3 详细任务清单

#### 4.1.3.1 请求队列实现
- [x] **优先级队列数据结构设计**
  - [x] 定义 `RequestItem` 结构体
    - [x] `requestId`（请求ID，唯一标识）
    - [x] `request`（ChatRequest对象）
    - [x] `taskType`（任务类型）
    - [x] `priority`（任务优先级）
    - [x] `modelId`（选定的模型ID）
    - [x] `timestamp`（提交时间）
    - [x] `promise`（std::promise<ChatResponse>，用于异步返回结果）
    - [x] `cancelToken`（取消令牌，可选）
  - [x] 定义优先级比较器（`CompareRequestPriority`）
    - [x] Critical > High > Normal > Low
    - [x] 同优先级按时间戳排序（FIFO）
  - [x] 使用 `std::priority_queue` 或自定义优先级队列实现

- [x] **请求入队/出队**
  - [x] 实现 `enqueueRequest()` 方法
    - [x] 生成唯一请求ID（UUID或时间戳+随机数）
    - [x] 创建 `RequestItem` 对象
    - [x] 将请求添加到优先级队列
    - [x] 返回 `std::future<ChatResponse>`（通过 promise）
    - [x] 线程安全（使用 mutex 保护）
  - [x] 实现 `dequeueRequest()` 方法
    - [x] 从优先级队列取出最高优先级请求
    - [x] 检查请求是否已取消
    - [x] 返回 `RequestItem` 或 `std::nullopt`（队列为空时）
    - [x] 线程安全

- [x] **队列大小限制**
  - [x] 从 `ConfigManager` 读取 `request_manager.max_queue_size` 配置
  - [x] 实现队列大小检查（`isQueueFull()`）
  - [x] 在入队时检查队列大小
    - [x] 队列满时返回错误或等待（可选：阻塞或立即返回）
    - [x] 支持配置队列满时的策略（拒绝/等待/丢弃低优先级）
  - [x] 实现队列统计（当前队列大小、最大队列大小）

**验收标准**：
- 单测验证：请求按优先级正确入队和出队。
- 单测验证：队列大小限制生效（超过限制时拒绝或等待）。
- 单测验证：线程安全（并发入队/出队无竞态条件）。

#### 4.1.3.2 并发控制器实现
- [x] **按模型限制并发数**
  - [x] 定义并发控制结构（`ConcurrencyController`）
    - [x] 按模型ID跟踪当前并发数（`modelId -> currentConcurrency`）
    - [x] 从 `ModelConfig` 读取 `maxConcurrentRequests` 限制
    - [x] 使用 `std::atomic` 或 `std::mutex` 保护并发计数
  - [x] 实现 `checkConcurrencyLimit(const std::string& modelId)` 方法
    - [x] 获取模型的最大并发数（从 `ModelManager`）
    - [x] 获取当前并发数
    - [x] 返回是否可接受新请求（`currentConcurrency < maxConcurrentRequests`）
  - [x] 实现 `acquireConcurrencySlot(const std::string& modelId)` 方法
    - [x] 检查并发限制
    - [x] 递增当前并发数
    - [x] 返回是否成功获取槽位
  - [x] 实现 `releaseConcurrencySlot(const std::string& modelId)` 方法
    - [x] 递减当前并发数
    - [x] 确保并发数不为负

- [x] **并发请求计数**
  - [x] 实现全局并发请求计数（所有模型的总并发数）
  - [x] 实现按模型的并发请求计数
  - [x] 实现并发统计查询接口
    - [x] `getCurrentConcurrency(const std::string& modelId)` - 获取指定模型的当前并发数
    - [x] `getTotalConcurrency()` - 获取全局总并发数
    - [x] `getConcurrencyLimit(const std::string& modelId)` - 获取指定模型的并发限制

- [x] **并发限制检查**
  - [x] 在请求调度前检查并发限制
  - [x] 如果达到并发限制，将请求保留在队列中
  - [x] 实现并发限制等待机制（可选：定期检查或事件通知）
  - [x] 支持超时（等待时间过长时返回错误）

**验收标准**：
- 单测验证：并发限制生效（超过限制时请求被阻塞）。
- 单测验证：并发计数正确（请求开始/结束时计数更新）。
- 单测验证：多个模型独立限制并发（模型A的并发不影响模型B）。

#### 4.1.3.3 请求调度器实现
- [x] **队列处理循环**
  - [x] 定义工作线程（`m_workerThread`）
  - [x] 实现 `processQueue()` 方法
    - [x] 循环处理队列中的请求
    - [x] 检查停止标志（`m_running`）
    - [x] 从队列取出请求
    - [x] 检查并发限制
    - [x] 分发请求到API客户端
    - [x] 处理请求结果（成功/失败）
  - [x] 实现 `start()` 方法
    - [x] 启动工作线程
    - [x] 设置运行标志
  - [x] 实现 `stop()` 方法
    - [x] 设置停止标志
    - [x] 等待工作线程结束
    - [x] 清理资源

- [x] **请求分发**
  - [x] 实现 `dispatchRequest(const RequestItem& item)` 方法
    - [x] 获取模型配置（从 `ModelManager`）
    - [x] 检查并发限制（`acquireConcurrencySlot`）
    - [x] 调用 `APIClient::chatAsync()` 或 `APIClient::chatStream()`
    - [x] 处理异步结果
      - [x] 成功：设置 promise 值
      - [x] 失败：设置 promise 异常或错误响应
    - [x] 释放并发槽位（`releaseConcurrencySlot`）
    - [x] 更新统计信息
  - [x] 支持流式请求分发
    - [x] 处理流式回调
    - [x] 聚合流式响应
    - [x] 最终设置 promise 值

- [x] **超时管理**
  - [x] 从 `ConfigManager` 读取 `request_manager.default_timeout_ms` 配置
  - [x] 从 `ModelConfig` 读取模型特定的超时配置（可选）
  - [x] 实现请求超时检查
    - [x] 记录请求开始时间
    - [x] 定期检查请求是否超时
    - [x] 超时后取消请求（如果支持）
  - [x] 实现超时处理
    - [x] 设置 promise 异常（`std::future_error` 或自定义超时错误）
    - [x] 释放并发槽位
    - [x] 更新统计信息（失败计数）
    - [x] 记录超时日志

**验收标准**：
- 单测验证：请求按优先级正确调度。
- 单测验证：并发限制生效（达到限制时请求等待）。
- 单测验证：超时管理正确（超时后请求被取消并返回错误）。

#### 4.1.3.4 请求取消机制
- [x] **取消令牌实现**
  - [x] 定义 `CancelToken` 结构（或使用 `HttpClient::CancelToken`）
    - [x] `isCancelled()` - 检查是否已取消
    - [x] `cancel()` - 取消请求
  - [x] 在 `RequestItem` 中存储 `CancelToken`
  - [x] 实现请求取消接口（`cancelRequest(const std::string& requestId)`）
    - [x] 查找请求（在队列中或正在处理中）
    - [x] 设置取消标志
    - [x] 如果请求在队列中，从队列移除
    - [x] 如果请求正在处理，调用 `APIClient` 的取消方法（如果支持）
    - [x] 设置 promise 异常（取消错误）
    - [x] 释放并发槽位

- [x] **取消状态跟踪**
  - [x] 维护已取消请求的集合（可选，用于统计）
  - [x] 实现取消统计（取消请求数、取消原因等）

**验收标准**：
- 单测验证：队列中的请求可以被取消。
- 单测验证：正在处理的请求可以被取消（如果API客户端支持）。
- 单测验证：取消后 promise 正确设置异常。

#### 4.1.3.5 请求统计
- [x] **统计数据结构**
  - [x] 定义 `RequestStatistics` 结构体
    - [x] `totalRequests`（总请求数）
    - [x] `completedRequests`（完成请求数）
    - [x] `failedRequests`（失败请求数）
    - [x] `cancelledRequests`（取消请求数）
    - [x] `averageResponseTimeMs`（平均响应时间，毫秒）
    - [x] `minResponseTimeMs`（最小响应时间）
    - [x] `maxResponseTimeMs`（最大响应时间）
    - [x] `requestsPerModel`（按模型的请求数统计）
    - [x] `queueSize`（当前队列大小）
    - [x] `maxQueueSize`（最大队列大小）
  - [x] 使用线程安全的数据结构（`std::atomic` 或 `std::mutex`）

- [x] **统计更新**
  - [x] 在请求提交时更新统计（`totalRequests++`）
  - [x] 在请求完成时更新统计（`completedRequests++`，响应时间）
  - [x] 在请求失败时更新统计（`failedRequests++`）
  - [x] 在请求取消时更新统计（`cancelledRequests++`）
  - [x] 按模型统计请求数（`requestsPerModel[modelId]++`）

- [x] **统计查询接口**
  - [x] 实现 `getStatistics()` 方法
    - [x] 返回完整的 `RequestStatistics` 结构
    - [x] 计算平均响应时间（`totalResponseTime / completedRequests`）
  - [x] 实现 `getQueueStatistics()` 方法
    - [x] 返回队列相关统计（当前大小、最大大小、等待时间等）

**验收标准**：
- 单测验证：统计信息正确更新（请求提交/完成/失败时）。
- 单测验证：平均响应时间计算正确。
- 单测验证：按模型统计准确。

---

## 4.2 响应处理器（ResponseHandler）

### 4.2.1 任务概述
统一处理API响应，包括流式响应处理、响应验证和缓存集成。

### 4.2.2 文件结构（建议）
```
include/naw/desktop_pet/service/
└── ResponseHandler.h

src/naw/desktop_pet/service/
└── ResponseHandler.cpp
```

### 4.2.3 详细任务清单

#### 4.2.3.1 流式响应处理
- [x] **SSE数据流解析**
  - [x] 复用 Phase2 中 `APIClient` 的 SSE 解析逻辑（或提取为独立组件）
  - [x] 实现 `parseSSEChunk()` 方法（通过 `SseDecoder` 类实现）
    - [x] 处理 `\n\n` 分隔的事件
    - [x] 处理多行 `data:` 拼接
    - [x] 处理 `data: [DONE]` 终止标记
    - [x] 处理断行/粘包（chunk 可能任意切割）
  - [x] 输出：每个 event 的 JSON payload（string）

- [x] **增量内容提取**
  - [x] 实现 `extractTextDelta()` 方法（通过 `ChatStreamAggregator::onChunkJson()` 实现）
    - [x] 从 SSE event JSON 中提取 `choices[0].delta.content`
    - [x] 返回增量文本内容（`std::string_view` 或 `std::string`）
  - [x] 实现 `extractToolCallDelta()` 方法（通过 `ChatStreamAggregator::onChunkJson()` 实现）
    - [x] 从 SSE event JSON 中提取 `choices[0].delta.tool_calls`
    - [x] 返回增量工具调用数据（`ToolCallDelta` 结构）
  - [x] 实现 `extractFinishReason()` 方法（通过 `ChatStreamAggregator::onChunkJson()` 实现）
    - [x] 从 SSE event JSON 中提取 `choices[0].finish_reason`
    - [x] 返回完成原因（`stop`、`tool_calls`、`length` 等）

- [x] **流式回调管理**
  - [x] 定义流式回调接口（`StreamCallbacks`）
    - [x] `onTextDelta(std::string_view delta)` - 文本增量回调
    - [x] `onToolCallDelta(const ToolCallDelta& delta)` - 工具调用增量回调
    - [x] `onComplete(const ChatResponse& response)` - 完成回调
    - [x] `onError(const ErrorInfo& error)` - 错误回调
  - [x] 实现 `handleStreamResponse()` 方法
    - [x] 接收 SSE 数据流
    - [x] 解析每个 chunk
    - [x] 聚合增量内容
    - [x] 调用相应的回调函数
    - [x] 处理完成和错误情况
  - [x] 实现流式响应聚合器（`ChatStreamAggregator` 类）
    - [x] 累积文本内容（`m_accumulatedContent`）
    - [x] 累积工具调用（按 index/id 聚合）
    - [x] 最终构建完整的 `ChatResponse` 对象

**验收标准**：
- 单测验证：SSE 数据流解析正确（处理各种边界情况）。
- 单测验证：增量内容提取正确（文本和工具调用）。
- 单测验证：流式回调按顺序正确调用。

#### 4.2.3.2 响应验证
- [x] **JSON格式验证**
  - [x] 实现 `validateJsonFormat()` 方法
    - [x] 检查响应是否为有效 JSON
    - [x] 使用 `nlohmann::json::parse()` 解析
    - [x] 捕获 JSON 解析异常
    - [x] 返回验证结果（成功/失败 + 错误信息）
  - [x] 实现 `validateResponseStructure()` 方法
    - [x] 检查必需字段是否存在（`choices`、`usage` 等）
    - [x] 检查字段类型是否正确
    - [x] 返回验证结果

- [x] **必需字段检查**
  - [x] 定义响应字段验证规则
    - [x] `choices` 数组必须存在且非空
    - [x] `choices[0].message` 必须存在
    - [x] `usage` 对象应该存在（可选，但建议有）
  - [x] 实现 `checkRequiredFields()` 方法
    - [x] 检查所有必需字段
    - [x] 返回缺失字段列表（如果有）
  - [x] 实现字段类型验证
    - [x] `choices` 必须是数组
    - [x] `usage` 必须是对象
    - [x] `prompt_tokens`、`completion_tokens`、`total_tokens` 必须是数字

- [x] **响应内容验证**
  - [x] 实现 `validateResponseContent()` 方法
    - [x] 检查响应内容是否为空（对于非工具调用响应）
    - [x] 检查工具调用参数是否有效（JSON格式）
    - [x] 检查 finish_reason 是否有效
  - [x] 实现响应完整性检查
    - [x] 流式响应是否完整（收到 `[DONE]` 标记）
    - [x] 工具调用是否完整（所有 tool_calls 都有完整参数）

**验收标准**：
- 单测验证：JSON 格式验证正确（有效/无效 JSON 都能正确识别）。
- 单测验证：必需字段检查正确（缺失字段时返回错误）。
- 单测验证：响应内容验证正确（空内容、无效工具调用等）。

#### 4.2.3.3 响应缓存集成
- [x] **缓存查询集成**
  - [x] 在 `ResponseHandler` 中持有 `CacheManager` 的引用
  - [x] 实现 `checkCache()` 方法
    - [x] 根据请求生成缓存键（使用 `CacheManager::generateKey()`）
    - [x] 查询缓存（使用 `CacheManager::get()`）
    - [x] 如果命中，返回缓存的响应
    - [x] 如果未命中，返回 `std::nullopt`
  - [x] 在响应处理流程中集成缓存查询
    - [x] 在处理请求前先查询缓存
    - [x] 命中缓存时直接返回，跳过 API 调用

- [x] **缓存存储集成**
  - [x] 实现 `storeCache()` 方法
    - [x] 根据请求生成缓存键
    - [x] 存储响应到缓存（使用 `CacheManager::put()`）
    - [x] 只缓存非流式响应（流式响应不适合缓存）
    - [x] 只缓存成功的响应（失败响应不缓存）
  - [x] 在响应处理流程中集成缓存存储
    - [x] API 调用成功后存储响应
    - [x] 检查缓存配置（是否启用缓存）

- [x] **缓存策略配置**
  - [x] 从 `ConfigManager` 读取缓存配置
    - [x] `cache.enabled` - 是否启用缓存
    - [x] `cache.default_ttl_seconds` - 默认 TTL
  - [x] 实现缓存条件判断（`shouldCache()` 方法）
    - [x] 只缓存确定性请求（`temperature=0` 或接近 0）
    - [x] 不缓存包含工具调用的请求（可选，根据配置）
    - [x] 不缓存流式请求

**验收标准**：
- 单测验证：缓存查询集成正确（命中缓存时直接返回）。
- 单测验证：缓存存储集成正确（成功响应被正确缓存）。
- 单测验证：缓存策略生效（符合条件的请求被缓存，不符合的不缓存）。

#### 4.2.3.4 响应统计
- [x] **统计数据结构**
  - [x] 定义 `ResponseStatistics` 结构体
    - [x] `totalResponses`（总响应数）
    - [x] `successfulResponses`（成功响应数）
    - [x] `failedResponses`（失败响应数）
    - [x] `cachedResponses`（缓存命中数）
    - [x] `totalResponseSize`（总响应大小，字节）
    - [x] `streamingResponses`（流式响应数）
    - [x] `getAverageResponseSize()` 方法（计算平均响应大小）
    - [x] `getCacheHitRate()` 方法（计算缓存命中率）
  - [x] 使用线程安全的数据结构（`std::mutex` 保护）

- [x] **统计更新**
  - [x] 在处理响应时更新统计
    - [x] `totalResponses++`（在 `checkCache()` 中）
    - [x] 成功时 `successfulResponses++`（通过 `updateStatistics()` 方法）
    - [x] 失败时 `failedResponses++`（通过 `updateStatistics()` 方法）
    - [x] 缓存命中时 `cachedResponses++`（在 `checkCache()` 中）
    - [x] 流式响应时 `streamingResponses++`（通过 `updateStatistics()` 方法）
    - [x] 响应大小统计（在 `checkCache()` 中通过 `estimateResponseSize()` 更新）
  - [x] 计算平均响应大小（`totalResponseSize / totalResponses`）

- [x] **统计查询接口**
  - [x] 实现 `getStatistics()` 方法
    - [x] 返回完整的 `ResponseStatistics` 结构
  - [x] 实现缓存命中率计算（`cachedResponses / totalResponses`，通过 `getCacheHitRate()` 方法）

**验收标准**：
- 单测验证：统计信息正确更新。
- 单测验证：缓存命中率计算正确。

---

## 4.3 缓存管理器（CacheManager）

### 4.3.1 任务概述
实现基于请求内容的响应缓存，减少API调用次数，提高响应速度。

### 4.3.2 文件结构（建议）
```
include/naw/desktop_pet/service/
└── CacheManager.h

src/naw/desktop_pet/service/
└── CacheManager.cpp
```

### 4.3.3 详细任务清单

#### 4.3.3.1 缓存键生成
- [x] **基于请求内容的哈希**
  - [x] 定义 `CacheKey` 结构体
    - [x] `modelId`（模型ID）
    - [x] `messagesHash`（消息内容的哈希值）
    - [x] `temperature`（温度参数）
    - [x] `maxTokens`（最大Token数）
    - [x] `topP`（Top-p参数，可选）
    - [x] `topK`（Top-k参数，可选）
    - [x] `stop`（停止序列，可选）
  - [x] 实现 `generateKey(const ChatRequest& request)` 方法
    - [x] 序列化消息列表（使用 `ChatMessage::toJson()`）
    - [x] 计算消息内容的哈希值（使用 `std::hash` 或 SHA256）
    - [x] 组合所有参数生成缓存键
    - [x] 返回 `CacheKey` 对象
  - [x] 实现 `CacheKey` 的哈希函数（用于 `std::unordered_map`）
    - [x] 实现 `CacheKeyHash` 函数对象（使用自定义哈希函数对象避免 MSVC 特化问题）

- [x] **模型ID、温度等参数包含**
  - [x] 确保缓存键包含所有影响响应的参数
    - [x] 模型ID（不同模型响应不同）
    - [x] 温度参数（temperature 影响随机性）
    - [x] 采样参数（top_p、top_k 影响输出）
    - [x] 停止序列（stop 影响输出长度）
    - [x] 最大Token数（max_tokens 影响输出长度）
  - [x] 实现参数序列化
    - [x] 将参数组合成字符串
    - [x] 计算哈希值
  - [x] 处理可选参数
    - [x] 未设置的参数使用默认值或特殊标记

- [x] **缓存键比较**
  - [x] 实现 `CacheKey` 的相等比较运算符（`operator==`）
    - [x] 比较所有字段
  - [x] 确保缓存键的唯一性
    - [x] 相同请求生成相同缓存键
    - [x] 不同请求生成不同缓存键

**验收标准**：
- 单测验证：相同请求生成相同缓存键。
- 单测验证：不同请求生成不同缓存键。
- 单测验证：参数变化时缓存键也变化。

#### 4.3.3.2 缓存存储实现
- [x] **内存缓存实现**
  - [x] 使用 `std::unordered_map<CacheKey, CacheEntry, CacheKeyHash>` 存储缓存
  - [x] 定义 `CacheEntry` 结构体
    - [x] `response`（ChatResponse对象）
    - [x] `timestamp`（存储时间戳）
    - [x] `ttl`（Time to live，生存时间）
    - [x] `accessCount`（访问次数，可选，用于LRU）
    - [x] `lastAccessTime`（最后访问时间，可选，用于LRU）
  - [x] 实现线程安全（使用 `std::mutex` 保护）

- [x] **缓存条目结构**
  - [x] 实现 `put(const CacheKey& key, const ChatResponse& response, std::optional<std::chrono::seconds> ttl)` 方法
    - [x] 创建 `CacheEntry` 对象
    - [x] 设置时间戳和 TTL
    - [x] 存储到缓存映射
    - [x] 检查缓存大小限制（如果实现）
    - [x] 线程安全
  - [x] 实现缓存大小限制（可选）
    - [x] 从配置读取 `cache.max_entries`
    - [x] 超过限制时使用 LRU 策略淘汰旧条目

- [x] **TTL管理**
  - [x] 从 `ConfigManager` 读取 `cache.default_ttl_seconds` 配置
  - [x] 支持按请求设置 TTL（可选）
  - [x] 实现 TTL 检查
    - [x] `isExpired(const CacheEntry& entry)` - 检查条目是否过期
    - [x] 比较当前时间与 `timestamp + ttl`

**验收标准**：
- 单测验证：缓存条目正确存储和检索。
- 单测验证：TTL 管理正确（过期条目不再返回）。
- 单测验证：线程安全（并发读写无竞态条件）。

#### 4.3.3.3 缓存查询
- [x] **缓存查找实现**
  - [x] 实现 `get(const CacheKey& key)` 方法
    - [x] 在缓存映射中查找键
    - [x] 如果找到，检查是否过期
    - [x] 如果未过期，更新访问统计（如果实现 LRU）
    - [x] 返回 `std::optional<ChatResponse>`
    - [x] 如果未找到或已过期，返回 `std::nullopt`
    - [x] 线程安全
  - [x] 实现缓存命中统计
    - [x] 记录缓存命中次数
    - [x] 记录缓存未命中次数

- [x] **缓存有效性检查**
  - [x] 实现 `isValid(const CacheEntry& entry)` 方法
    - [x] 检查是否过期
    - [x] 检查响应是否有效（可选）
  - [x] 在查询时自动清理过期条目（可选）
    - [x] 发现过期条目时删除
    - [x] 或定期清理（见 4.3.3.4）

**验收标准**：
- 单测验证：缓存查询正确（命中/未命中/过期都能正确处理）。
- 单测验证：缓存命中统计准确。

#### 4.3.3.4 缓存过期清理
- [x] **过期条目清理**
  - [x] 实现 `evictExpired()` 方法
    - [x] 遍历所有缓存条目
    - [x] 检查每个条目是否过期
    - [x] 删除过期条目
    - [x] 返回清理的条目数
    - [x] 线程安全
  - [x] 实现定期清理机制
    - [x] 定义清理线程或定时器
    - [x] 定期调用 `evictExpired()`
    - [x] 从配置读取清理间隔（`cache.cleanup_interval_seconds`）

- [x] **LRU淘汰策略（可选）**
  - [x] 实现 LRU（Least Recently Used）淘汰
    - [x] 维护访问时间戳
    - [x] 缓存满时淘汰最久未使用的条目
  - [x] 实现 `evictLRU(size_t count)` 方法
    - [x] 按 `lastAccessTime` 排序
    - [x] 删除最久未使用的 N 个条目

- [x] **缓存大小管理**
  - [x] 实现缓存大小限制
    - [x] 从配置读取 `cache.max_entries`
    - [x] 计算当前缓存大小（内存占用，估算）
    - [x] 超过限制时触发清理或淘汰
  - [x] 实现 `getCacheSize()` 方法
    - [x] 返回当前缓存条目数
    - [x] 返回当前缓存大小（字节，通过 `getStatistics()` 获取）

**验收标准**：
- 单测验证：过期条目被正确清理。
- 单测验证：定期清理机制工作正常。
- 单测验证：LRU 淘汰策略正确（如果实现）。

#### 4.3.3.5 缓存统计（命中率等）
- [x] **统计数据结构**
  - [x] 定义 `CacheStatistics` 结构体
    - [x] `totalHits`（总命中数）
    - [x] `totalMisses`（总未命中数）
    - [x] `hitRate`（命中率，计算得出：`hits / (hits + misses)`）
    - [x] `totalEntries`（当前缓存条目数）
    - [x] `totalSize`（当前缓存大小，字节）
    - [x] `evictedEntries`（被淘汰的条目数）
  - [x] 使用线程安全的数据结构

- [x] **统计更新**
  - [x] 在缓存查询时更新统计
    - [x] 命中时 `totalHits++`
    - [x] 未命中时 `totalMisses++`
  - [x] 在缓存存储时更新统计
    - [x] `totalEntries++`
    - [x] `totalSize += entrySize`
  - [x] 在缓存清理时更新统计
    - [x] `evictedEntries += evictedCount`
    - [x] `totalEntries -= evictedCount`

- [x] **统计查询接口**
  - [x] 实现 `getStatistics()` 方法
    - [x] 返回完整的 `CacheStatistics` 结构
    - [x] 计算命中率（`totalHits / (totalHits + totalMisses)`）
  - [x] 实现 `getHitRate()` 方法
    - [x] 返回当前命中率（0-1）

- [x] **缓存清空接口**
  - [x] 实现 `clear()` 方法
    - [x] 清空所有缓存条目
    - [x] 重置统计信息
    - [x] 线程安全

**验收标准**：
- 单测验证：统计信息正确更新。
- 单测验证：命中率计算正确。
- 单测验证：缓存清空功能正常。

---

## 4.4 单元测试与示例

### 4.4.1 单元测试
- [x] **RequestManager测试**
  - [x] 请求队列测试（入队/出队、优先级排序、队列大小限制）
  - [x] 并发控制测试（并发限制、并发计数、多模型独立限制）
  - [x] 请求调度测试（队列处理循环、请求分发、超时管理）
  - [x] 请求取消测试（队列中取消、处理中取消）
  - [x] 请求统计测试（统计更新、平均响应时间计算）

- [x] **ResponseHandler测试**
  - [x] 流式响应处理测试（SSE解析、增量提取、回调管理）
    - [x] 简单文本流测试
    - [x] 完成原因测试
    - [x] 工具调用增量测试
    - [x] 错误处理测试
  - [x] 响应验证测试（JSON格式验证、必需字段检查、内容验证）
    - [x] 有效JSON验证
    - [x] 无效JSON验证（缺失字段、空数组等）
    - [x] 无效完成原因验证
    - [x] 空内容验证
    - [x] 工具调用验证
  - [x] 缓存集成测试（缓存查询、缓存存储、缓存策略）
    - [x] 缓存命中/未命中测试
    - [x] 存储和检索测试
    - [x] 流式请求不缓存测试
    - [x] 高温度请求不缓存测试
    - [x] 低温度请求缓存测试
    - [x] 禁用缓存测试
  - [x] 响应统计测试（统计更新、缓存命中率）
    - [x] 初始状态测试
    - [x] 缓存命中率测试
    - [x] 平均响应大小测试

- [x] **CacheManager测试**
  - [x] 缓存键生成测试（相同请求相同键、不同请求不同键）
  - [x] 缓存存储测试（存储/检索、TTL管理、线程安全）
  - [x] 缓存查询测试（命中/未命中/过期处理）
  - [x] 缓存清理测试（过期清理、LRU淘汰、定期清理）
  - [x] 缓存统计测试（命中率计算、统计更新）

### 4.4.2 集成测试
- [ ] **RequestManager + APIClient 集成测试**
  - [ ] 端到端请求处理流程
  - [ ] 并发请求处理
  - [ ] 超时和取消处理

- [ ] **ResponseHandler + CacheManager 集成测试**
  - [ ] 缓存命中时的响应处理
  - [ ] 缓存未命中时的API调用和存储
  - [ ] 流式响应的缓存策略

- [ ] **完整流程集成测试**
  - [ ] RequestManager -> APIClient -> ResponseHandler -> CacheManager
  - [ ] 并发场景下的完整流程
  - [ ] 错误处理和重试集成

---

## 开发顺序建议

### 第一阶段：缓存管理器（4.3）
1. 先完成 `CacheManager`，因为它相对独立，且被 `ResponseHandler` 依赖。
2. 实现缓存键生成、存储、查询和统计。

### 第二阶段：响应处理器（4.2）
1. 完成 `ResponseHandler`，集成 `CacheManager`。
2. 实现流式响应处理、响应验证和缓存集成。

### 第三阶段：请求管理器（4.1）
1. 完成 `RequestManager`，这是最复杂的模块。
2. 实现请求队列、并发控制和请求调度。
3. 集成 `APIClient` 和 `ResponseHandler`。

### 第四阶段：集成和测试
1. 完成所有模块的集成测试。
2. 性能测试和优化。

---

## 进度追踪

### 4.1 请求管理器（RequestManager）
- [x] 请求队列实现
- [x] 并发控制器实现
- [x] 请求调度器实现
- [x] 请求取消机制
- [x] 请求统计
- [x] 单元测试

**进度**: 5/6 主要模块完成

### 4.2 响应处理器（ResponseHandler）
- [x] 流式响应处理
- [x] 响应验证
- [x] 响应缓存集成
- [x] 响应统计
- [x] 单元测试

**进度**: 5/5 主要模块完成

### 4.3 缓存管理器（CacheManager）
- [x] 缓存键生成
- [x] 缓存存储实现
- [x] 缓存查询
- [x] 缓存过期清理
- [x] 缓存统计
- [x] 单元测试

**进度**: 6/6 主要模块完成

**总计**: 16/17 主要模块完成

---

## 依赖关系

```
Phase1 (基础设施层)
  ↓
Phase2 (API客户端层)
  ↓
Phase3 (核心管理层)
  ↓
Phase4 (服务管理层)
  ├── 4.3 CacheManager (相对独立)
  ├── 4.2 ResponseHandler (依赖 4.3 CacheManager)
  └── 4.1 RequestManager (依赖 4.2 ResponseHandler + Phase2 APIClient)
```

---

## 注意事项

1. **线程安全**：所有模块都需要考虑线程安全，使用 `std::mutex` 或 `std::atomic` 保护共享数据。
2. **性能优化**：请求队列和缓存都需要考虑性能，避免成为瓶颈。
3. **内存管理**：缓存管理器需要注意内存使用，实现合理的淘汰策略。
4. **错误处理**：所有模块都需要完善的错误处理，确保系统稳定性。
5. **配置化**：所有关键参数都应该可配置，通过 `ConfigManager` 读取。
6. **统计和监控**：实现完善的统计功能，便于监控和优化。

---

*最后更新: 2025年12月29日*

## 更新日志

### 2025年12月29日（更新2）
- ✅ 完成 4.2 响应处理器（ResponseHandler）的实现
  - ✅ 实现流式响应处理（SSE数据流解析、增量内容提取、流式回调管理）
    - ✅ 复用 APIClient 中的 SseDecoder 和 ChatStreamAggregator 逻辑
    - ✅ 实现 handleStreamResponse() 方法，支持文本和工具调用的流式处理
  - ✅ 实现响应验证（JSON格式验证、必需字段检查、响应内容验证）
    - ✅ 实现 validateResponse() 方法（支持 JSON 和 ChatResponse 两种输入）
    - ✅ 实现完整的验证流程：结构验证、字段检查、内容验证
  - ✅ 实现响应缓存集成（缓存查询、缓存存储、缓存策略）
    - ✅ 实现 checkCache() 和 storeCache() 方法
    - ✅ 实现 shouldCache() 缓存策略判断（温度、流式、工具调用）
    - ✅ 从 ConfigManager 读取缓存配置
  - ✅ 实现响应统计（统计数据结构、统计更新、统计查询接口）
    - ✅ 实现 ResponseStatistics 结构体（包含所有统计指标）
    - ✅ 实现统计更新逻辑（在 checkCache() 中更新）
    - ✅ 实现 getStatistics() 和 getCacheHitRate() 查询接口
  - ✅ 完成单元测试（22个测试用例，覆盖所有功能点）
    - ✅ 流式响应处理测试（4个测试用例）
    - ✅ 响应验证测试（6个测试用例）
    - ✅ 缓存集成测试（6个测试用例）
    - ✅ 响应统计测试（3个测试用例）
    - ✅ JSON格式验证测试（3个测试用例）
  - 📝 文件已创建：
    - `include/naw/desktop_pet/service/ResponseHandler.h`
    - `src/naw/desktop_pet/service/ResponseHandler.cpp`
    - `src/naw/desktop_pet/service/tests/ResponseHandlerTest.cpp`
  - 📝 已更新构建系统：`src/naw/desktop_pet/service/CMakeLists.txt`

### 2025年12月29日
- ✅ 完成 4.1 请求管理器（RequestManager）的实现
  - ✅ 实现请求队列（优先级队列、入队/出队、队列大小限制）
  - ✅ 实现并发控制（按模型限制并发数、并发槽位管理、并发统计查询）
  - ✅ 实现请求调度（工作线程、队列处理循环、请求分发、超时管理）
  - ✅ 实现请求取消机制（取消令牌、取消接口、取消状态跟踪）
  - ✅ 实现请求统计（统计数据结构、统计更新、统计查询接口）
  - 📝 文件已创建：
    - `include/naw/desktop_pet/service/RequestManager.h`
    - `src/naw/desktop_pet/service/RequestManager.cpp`
  - 📝 已更新构建系统：`src/naw/desktop_pet/service/CMakeLists.txt`

- ✅ 完成 4.3 缓存管理器（CacheManager）的实现
  - ✅ 实现缓存键生成（基于请求内容的哈希、参数包含、键比较）
  - ✅ 实现缓存存储（内存缓存、缓存条目结构、TTL管理、大小限制、LRU淘汰）
  - ✅ 实现缓存查询（缓存查找、命中统计、有效性检查）
  - ✅ 实现缓存过期清理（过期条目清理、定期清理机制、LRU淘汰策略）
  - ✅ 实现缓存统计（统计数据结构、统计更新、统计查询接口、缓存清空）
  - ✅ 完成单元测试（17个测试用例，覆盖所有功能点）
  - 📝 文件已创建：
    - `include/naw/desktop_pet/service/CacheManager.h`
    - `src/naw/desktop_pet/service/CacheManager.cpp`
    - `src/naw/desktop_pet/service/tests/CacheManagerTest.cpp`
  - 📝 已更新构建系统：`src/naw/desktop_pet/service/CMakeLists.txt`

