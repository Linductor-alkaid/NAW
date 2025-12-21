# Phase 3：核心管理层（Core Management Layer）详细任务清单

本文档是阶段三核心管理层的详细开发任务清单，基于《服务层设计方案》制定，并以 Phase1、Phase2 已完成的基础设施层和 API 客户端层为依赖。

> **参考信息**：
> - 设计方案：`docs/design/服务层设计方案.md`
> - Phase1 详细清单：`docs/todolists/service_layer/TODO_Phase1_基础设施层.md`
> - Phase2 详细清单：`docs/todolists/service_layer/TODO_Phase2_API客户端层.md`
> - 总体任务清单：`docs/todolists/TODO_服务层开发任务清单.md`

## 概述

### 目标
- 实现 **模型管理器（ModelManager）**：管理所有可用模型及其配置、能力映射、健康状态和性能统计。
- 实现 **任务路由器（TaskRouter）**：根据任务类型、上下文大小、优先级等因素智能选择最合适的模型。
- 实现 **上下文管理器（ContextManager）**：根据任务类型动态构建和管理上下文，包括对话历史、Agent状态、项目上下文等。

### 非目标（明确留到后续Phase）
- 请求队列/并发控制/缓存（Phase4）。
- 工具调用/MCP服务（Phase5）。
- 多模态服务（Phase6）。

### 依赖与关键组件（Phase3 前置）
- [x] Phase1 已提供：
  - `ConfigManager`：配置文件加载和环境变量支持。
  - `types::TaskType`、`types::ModelConfig`、`types::ChatMessage` 等基础数据结构。
  - `ErrorHandler`：统一错误处理和重试策略。
- [x] Phase2 已提供：
  - `APIClient`：同步/异步/流式 API 调用能力。
  - `types::ChatRequest`、`types::ChatResponse`：请求/响应结构。

---

## 3.1 模型管理器（ModelManager）

### 3.1.1 任务概述
实现模型配置的加载、注册、查询和管理，支持模型能力映射、健康状态监控和性能统计。

### 3.1.2 文件结构（建议）
```
include/naw/desktop_pet/service/
└── ModelManager.h

src/naw/desktop_pet/service/
└── ModelManager.cpp
```

### 3.1.3 详细任务清单

#### 3.1.3.1 模型配置加载
- [ ] **从配置文件加载模型列表**
  - [ ] 通过 `ConfigManager` 读取 `models` 配置节点
  - [ ] 解析每个模型的 JSON 配置（使用 `ModelConfig::fromJson()`）
  - [ ] 验证模型配置有效性（`ModelConfig::isValid()`）
  - [ ] 将模型配置存储到内部映射（`modelId -> ModelConfig`）
  - [ ] 处理配置加载错误（缺失必需字段、类型错误等）

- [ ] **模型能力映射（任务类型支持）**
  - [ ] 从配置中解析 `supported_tasks` 数组
  - [ ] 将字符串任务类型转换为 `TaskType` 枚举（使用 `stringToTaskType()`）
  - [ ] 建立任务类型到模型列表的反向索引（`TaskType -> vector<ModelConfig*>`）
  - [ ] 实现 `getModelsForTask(TaskType)` 查询接口

- [ ] **模型参数配置（上下文长度、温度等）**
  - [ ] 解析并存储 `maxContextTokens`、`defaultTemperature`、`defaultMaxTokens` 等参数
  - [ ] 解析并存储 `costPer1kTokens`、`maxConcurrentRequests`、`supportsStreaming` 等参数
  - [ ] 解析并存储 `performanceScore`、`recommendedPromptStyle` 等可选参数
  - [ ] 实现参数访问接口（`getModelConfig(modelId)`）

**验收标准**：
- 单测验证：从配置文件加载模型列表，能正确解析所有字段。
- 单测验证：`getModelsForTask()` 返回支持指定任务的所有模型。
- 单测验证：配置错误时能正确报告错误信息。

#### 3.1.3.2 模型注册和管理
- [ ] **模型添加/移除**
  - [ ] 实现 `registerModel(const ModelConfig&)` 方法
    - [ ] 验证模型配置有效性
    - [ ] 检查模型ID是否已存在（可选：支持覆盖）
    - [ ] 更新内部映射和索引
  - [ ] 实现 `unregisterModel(const std::string& modelId)` 方法
    - [ ] 检查模型是否存在
    - [ ] 从映射和索引中移除
    - [ ] 清理相关统计信息
  - [ ] 实现线程安全（使用 mutex 保护共享数据）

- [ ] **模型查询**
  - [ ] 实现 `getModel(const std::string& modelId)` 方法
    - [ ] 返回模型配置的引用或可选值
    - [ ] 处理模型不存在的情况
  - [ ] 实现 `getAllModels()` 方法
    - [ ] 返回所有已注册模型的列表
  - [ ] 实现 `hasModel(const std::string& modelId)` 方法
    - [ ] 检查模型是否存在

- [ ] **模型健康状态监控**
  - [ ] 定义模型健康状态枚举（`Healthy`、`Degraded`、`Unhealthy`、`Unknown`）
  - [ ] 实现健康状态存储（`modelId -> HealthStatus`）
  - [ ] 实现 `getModelHealth(const std::string& modelId)` 方法
  - [ ] 实现健康状态更新机制（基于错误率、响应时间等）
    - [ ] 连续失败次数阈值
    - [ ] 响应时间阈值
    - [ ] 自动降级/恢复逻辑

**验收标准**：
- 单测验证：模型注册/移除后，查询接口返回正确结果。
- 单测验证：健康状态更新逻辑正确（失败次数达到阈值时降级）。

#### 3.1.3.3 模型性能统计
- [ ] **请求计数**
  - [ ] 定义统计结构（`ModelStatistics`）
    - [ ] `totalRequests`（总请求数）
    - [ ] `successfulRequests`（成功请求数）
    - [ ] `failedRequests`（失败请求数）
  - [ ] 实现按模型ID的统计存储（`modelId -> ModelStatistics`）
  - [ ] 实现 `recordRequest(const std::string& modelId, bool success)` 方法
  - [ ] 实现线程安全的统计更新（使用原子变量或 mutex）

- [ ] **响应时间统计**
  - [ ] 在 `ModelStatistics` 中添加响应时间相关字段
    - [ ] `totalResponseTimeMs`（总响应时间，毫秒）
    - [ ] `minResponseTimeMs`（最小响应时间）
    - [ ] `maxResponseTimeMs`（最大响应时间）
    - [ ] `averageResponseTimeMs`（平均响应时间，计算得出）
  - [ ] 实现 `recordResponseTime(const std::string& modelId, uint32_t responseTimeMs)` 方法
  - [ ] 实现响应时间统计的更新逻辑

- [ ] **成功率统计**
  - [ ] 在 `ModelStatistics` 中添加成功率字段（`successRate`，计算得出）
  - [ ] 实现 `getSuccessRate(const std::string& modelId)` 方法
  - [ ] 实现成功率计算逻辑（`successfulRequests / totalRequests`）

- [ ] **负载因子计算**
  - [ ] 定义负载因子概念（当前并发请求数 / 最大并发请求数）
  - [ ] 在 `ModelStatistics` 中添加负载相关字段
    - [ ] `currentConcurrency`（当前并发数）
    - [ ] `maxConcurrency`（最大并发数，来自 `ModelConfig`）
    - [ ] `loadFactor`（负载因子，0-1，计算得出）
  - [ ] 实现 `getLoadFactor(const std::string& modelId)` 方法
  - [ ] 实现并发数跟踪（请求开始时递增，结束时递减）

- [ ] **统计查询接口**
  - [ ] 实现 `getStatistics(const std::string& modelId)` 方法
    - [ ] 返回完整的 `ModelStatistics` 结构
  - [ ] 实现 `getAllStatistics()` 方法
    - [ ] 返回所有模型的统计信息
  - [ ] 实现统计重置接口（`resetStatistics()`，可选）

**验收标准**：
- 单测验证：记录请求和响应时间后，统计信息正确更新。
- 单测验证：负载因子计算正确（并发数变化时实时更新）。
- 单测验证：成功率计算正确（成功/失败请求比例）。

#### 3.1.3.4 按任务类型查询模型
- [ ] **实现查询接口**
  - [ ] 实现 `getModelsForTask(TaskType taskType)` 方法
    - [ ] 使用反向索引快速查找
    - [ ] 返回支持该任务的所有模型配置
  - [ ] 实现 `getBestModelForTask(TaskType taskType, const SelectionCriteria& criteria)` 方法（可选，可放在 TaskRouter 中）
    - [ ] 考虑模型能力、性能、负载等因素
    - [ ] 返回最佳模型配置

- [ ] **查询优化**
  - [ ] 缓存查询结果（可选，避免重复计算）
  - [ ] 支持按性能评分排序
  - [ ] 支持过滤不健康的模型

**验收标准**：
- 单测验证：`getModelsForTask()` 返回正确的结果列表。
- 单测验证：查询结果按性能评分排序（如果实现）。

---

## 3.2 任务路由器（TaskRouter）

### 3.2.1 任务概述
根据任务类型、上下文大小、优先级等因素，智能选择最合适的模型。

### 3.2.2 文件结构（建议）
```
include/naw/desktop_pet/service/
└── TaskRouter.h

src/naw/desktop_pet/service/
└── TaskRouter.cpp
```

### 3.2.3 详细任务清单

#### 3.2.3.1 路由表初始化
- [ ] **任务类型到模型的映射**
  - [ ] 从 `ConfigManager` 读取 `routing.default_model_per_task` 配置
  - [ ] 建立任务类型到默认模型的映射（`TaskType -> modelId`）
  - [ ] 建立任务类型到模型优先级列表的映射（`TaskType -> vector<ModelPreference>`）
    - [ ] `ModelPreference` 结构：`{modelId, priority, weight}`
  - [ ] 实现路由表初始化方法（`initializeRoutingTable()`）

- [ ] **模型优先级排序**
  - [ ] 根据配置中的模型顺序确定优先级
  - [ ] 考虑模型性能评分（`performanceScore`）
  - [ ] 考虑模型成本（`costPer1kTokens`）
  - [ ] 实现优先级计算逻辑（综合多个因素）

**验收标准**：
- 单测验证：路由表初始化后，能正确查询任务类型对应的模型列表。
- 单测验证：模型优先级排序符合预期（高性能、低成本优先）。

#### 3.2.3.2 智能路由算法
- [ ] **任务类型匹配**
  - [ ] 使用 `ModelManager::getModelsForTask()` 获取候选模型
  - [ ] 过滤不支持该任务类型的模型
  - [ ] 实现任务类型匹配逻辑

- [ ] **上下文容量检查**
  - [ ] 定义 `TaskContext` 结构
    - [ ] `estimatedTokens`（预估上下文Token数）
    - [ ] `taskType`（任务类型）
    - [ ] `priority`（优先级）
    - [ ] `maxCost`（最大成本限制，可选）
    - [ ] `requiresStreaming`（是否需要流式响应）
  - [ ] 实现上下文容量检查（`model.maxContextTokens >= context.estimatedTokens`）
  - [ ] 过滤无法容纳上下文的模型

- [ ] **模型评分计算**
  - [ ] 实现 `calculateModelScore()` 方法
    - [ ] 能力匹配度（40%）：模型是否支持任务类型
    - [ ] 上下文容量（20%）：模型是否能容纳上下文
    - [ ] 性能评分（20%）：模型的 `performanceScore`
    - [ ] 成本效率（10%）：对于非关键任务，选择成本较低的模型
    - [ ] 负载情况（10%）：选择负载较低的模型（`1.0 - loadFactor`）
  - [ ] 根据任务优先级调整权重（关键任务更重视性能，普通任务更重视成本）
  - [ ] 实现评分归一化（0-1范围）

- [ ] **负载均衡考虑**
  - [ ] 获取模型的当前负载因子（`ModelManager::getLoadFactor()`）
  - [ ] 在评分计算中考虑负载（负载高的模型降低评分）
  - [ ] 实现负载均衡策略（可选：轮询、最少连接等）

- [ ] **成本优化考虑**
  - [ ] 对于非关键任务（`priority != Critical`），优先选择成本较低的模型
  - [ ] 在评分计算中考虑成本（成本高的模型降低评分）
  - [ ] 支持成本上限限制（`maxCost` 参数）

- [ ] **路由决策生成**
  - [ ] 定义 `RoutingDecision` 结构
    - [ ] `modelId`（选定的模型ID）
    - [ ] `modelConfig`（模型配置）
    - [ ] `confidence`（选择置信度，0-1）
    - [ ] `reason`（选择原因，字符串）
  - [ ] 实现 `routeTask()` 方法
    - [ ] 获取候选模型
    - [ ] 计算每个模型的评分
    - [ ] 选择评分最高的模型
    - [ ] 生成路由决策

**验收标准**：
- 单测验证：给定任务类型和上下文，能正确选择最合适的模型。
- 单测验证：评分计算逻辑正确（各因素权重符合预期）。
- 单测验证：负载均衡和成本优化生效（高负载/高成本模型评分降低）。

#### 3.2.3.3 路由决策记录和日志
- [ ] **决策记录**
  - [ ] 定义路由决策历史结构（`RoutingHistory`）
    - [ ] `timestamp`（时间戳）
    - [ ] `taskType`（任务类型）
    - [ ] `selectedModel`（选定的模型）
    - [ ] `confidence`（置信度）
    - [ ] `reason`（选择原因）
  - [ ] 实现决策历史存储（可选：内存缓存，限制大小）
  - [ ] 实现 `recordDecision()` 方法

- [ ] **日志输出**
  - [ ] 使用 `ErrorHandler::log()` 记录路由决策
  - [ ] 日志级别：INFO（正常决策）、DEBUG（详细评分信息）
  - [ ] 日志格式：包含任务类型、选定模型、置信度、原因等
  - [ ] 支持日志开关（通过配置控制）

- [ ] **决策统计**
  - [ ] 统计各模型被选中的次数
  - [ ] 统计各任务类型的路由分布
  - [ ] 实现统计查询接口（可选）

**验收标准**：
- 单测验证：路由决策被正确记录和日志输出。
- 单测验证：决策统计信息准确。

---

## 3.3 上下文管理器（ContextManager）

### 3.3.1 任务概述
根据任务类型动态构建和管理上下文，包括对话历史、Agent状态、项目上下文、代码上下文和记忆事件等。

### 3.3.2 文件结构（建议）
```
include/naw/desktop_pet/service/
└── ContextManager.h

src/naw/desktop_pet/service/
└── ContextManager.cpp
```

### 3.3.3 详细任务清单

#### 3.3.3.1 对话历史管理
- [ ] **历史消息存储**
  - [ ] 定义对话历史存储结构（`ConversationHistory`）
    - [ ] 使用 `std::deque<ChatMessage>` 存储消息（支持高效的头尾操作）
    - [ ] 存储会话ID（可选，支持多会话）
  - [ ] 实现 `addMessage(const ChatMessage& message)` 方法
    - [ ] 添加消息到历史
    - [ ] 更新Token计数
  - [ ] 实现线程安全（使用 mutex 保护）

- [ ] **历史消息查询**
  - [ ] 实现 `getHistory(size_t maxMessages)` 方法
    - [ ] 返回最近的 N 条消息
  - [ ] 实现 `getHistoryByRange(size_t start, size_t count)` 方法
    - [ ] 返回指定范围的消息
  - [ ] 实现 `getHistorySince(timestamp)` 方法（可选）
    - [ ] 返回指定时间之后的消息

- [ ] **历史消息裁剪**
  - [ ] 实现 `trimHistory(size_t maxMessages)` 方法
    - [ ] 保留最近的 N 条消息
    - [ ] 删除旧消息
  - [ ] 实现 `trimHistoryByTokens(size_t maxTokens)` 方法
    - [ ] 使用 `TokenCounter` 估算Token数
    - [ ] 保留不超过Token限制的消息
    - [ ] 优先保留最近的消息和系统消息

**验收标准**：
- 单测验证：添加消息后，查询接口返回正确结果。
- 单测验证：裁剪历史后，消息数量符合预期。

#### 3.3.3.2 上下文构建器
- [ ] **System Prompt构建**
  - [ ] 定义系统提示词模板（按任务类型）
    - [ ] 代码生成任务：强调代码质量和规范
    - [ ] 代码分析任务：强调详细分析
    - [ ] 对话任务：强调友好和自然
  - [ ] 实现 `buildSystemPrompt(TaskType taskType)` 方法
    - [ ] 根据任务类型选择模板
    - [ ] 支持模板参数替换（可选）
  - [ ] 实现系统提示词配置化（从配置文件读取）

- [ ] **Agent状态上下文构建**
  - [ ] 定义 `AgentState` 结构（需要与 Agent 模块对接）
    - [ ] Agent当前状态（情绪、目标等）
    - [ ] Agent记忆摘要（可选）
  - [ ] 实现 `buildAgentStateContext(const AgentState& agent)` 方法
    - [ ] 将Agent状态转换为 `ChatMessage`（role=system 或 user）
    - [ ] 格式化状态信息（JSON或自然语言）
  - [ ] 实现Agent状态更新机制（监听Agent状态变化）

- [ ] **项目上下文构建**
  - [ ] 定义项目上下文结构（`ProjectContext`）
    - [ ] 项目根路径
    - [ ] 项目结构摘要
    - [ ] 相关文件列表
  - [ ] 实现 `buildProjectContext(const std::string& projectPath, TaskType taskType)` 方法
    - [ ] 根据任务类型确定需要收集的项目信息
    - [ ] 收集项目结构（目录、文件列表）
    - [ ] 收集相关文件内容（可选，通过 MCP 工具，Phase5 实现）
    - [ ] 生成项目上下文摘要
    - [ ] 转换为 `ChatMessage`（role=system 或 user）

- [ ] **代码上下文构建**
  - [ ] 定义代码上下文结构（`CodeContext`）
    - [ ] 相关文件路径列表
    - [ ] 文件内容（可选）
    - [ ] 焦点区域（函数、类等）
  - [ ] 实现 `buildCodeContext(const std::vector<std::string>& filePaths, const std::string& focusArea)` 方法
    - [ ] 读取指定文件内容（通过 MCP 工具，Phase5 实现）
    - [ ] 提取焦点区域（函数、类等，可选）
    - [ ] 格式化代码上下文
    - [ ] 转换为 `ChatMessage`（role=user，content包含代码）

- [ ] **记忆事件上下文构建**
  - [ ] 定义记忆事件结构（`MemoryEvent`）
    - [ ] 事件类型
    - [ ] 事件内容
    - [ ] 时间戳
    - [ ] 重要性评分
  - [ ] 实现 `buildMemoryContext(const std::vector<MemoryEvent>& events, TaskType taskType)` 方法
    - [ ] 根据任务类型过滤相关事件
    - [ ] 按重要性排序
    - [ ] 格式化记忆事件
    - [ ] 转换为 `ChatMessage`（role=system 或 user）

- [ ] **完整上下文构建**
  - [ ] 定义 `ContextConfig` 结构
    - [ ] `taskType`（任务类型）
    - [ ] `maxTokens`（最大Token数）
    - [ ] `includeConversationHistory`（是否包含对话历史）
    - [ ] `includeAgentState`（是否包含Agent状态）
    - [ ] `includeProjectContext`（是否包含项目上下文）
    - [ ] `includeCodeContext`（是否包含代码上下文）
    - [ ] `includeMemoryEvents`（是否包含记忆事件）
    - [ ] `maxHistoryMessages`（最大历史消息数）
  - [ ] 实现 `buildContext(const ContextConfig& config, const std::string& userMessage)` 方法
    - [ ] 按顺序构建各部分上下文
    - [ ] 使用 `TokenCounter` 估算Token数
    - [ ] 在Token限制内尽可能包含更多上下文
    - [ ] 返回完整的消息列表（`vector<ChatMessage>`）

**验收标准**：
- 单测验证：各部分上下文构建正确（System Prompt、Agent状态、项目上下文等）。
- 单测验证：完整上下文构建后，Token数不超过限制。
- 单测验证：上下文构建顺序和内容符合预期。

#### 3.3.3.3 上下文窗口管理
- [ ] **Token限制检查**
  - [ ] 使用 `TokenCounter` 估算消息列表的Token数
  - [ ] 实现 `estimateTokens(const vector<ChatMessage>& messages)` 方法
  - [ ] 实现 `checkTokenLimit(const vector<ChatMessage>& messages, size_t maxTokens)` 方法
    - [ ] 返回是否超过限制
    - [ ] 返回超出量（可选）

- [ ] **智能上下文裁剪**
  - [ ] 实现 `trimContext(vector<ChatMessage>& messages, size_t maxTokens, TaskType taskType)` 方法
    - [ ] 计算消息重要性评分（`calculateMessageImportance()`）
    - [ ] 按重要性排序
    - [ ] 保留重要消息，删除不重要消息
    - [ ] 确保不超过Token限制
  - [ ] 实现裁剪策略：
    - [ ] System prompt始终保留
    - [ ] 最近的用户消息优先保留
    - [ ] 根据任务类型保留相关历史（代码任务保留代码相关消息）
    - [ ] 重要事件（如关键决策）优先保留

- [ ] **消息重要性评分**
  - [ ] 实现 `calculateMessageImportance(const ChatMessage& message, TaskType taskType)` 方法
    - [ ] 考虑消息角色（system > user > assistant）
    - [ ] 考虑消息时间（越新越重要）
    - [ ] 考虑任务类型相关性（代码任务中，包含代码的消息更重要）
    - [ ] 考虑消息长度（过短的消息可能不重要）
    - [ ] 返回重要性评分（0-1）

**验收标准**：
- 单测验证：Token限制检查正确（超过限制时返回true）。
- 单测验证：智能裁剪后，Token数不超过限制，且重要消息被保留。
- 单测验证：消息重要性评分符合预期（system消息、新消息、相关消息评分更高）。

#### 3.3.3.4 上下文配置管理
- [ ] **配置加载**
  - [ ] 从 `ConfigManager` 读取 `context` 配置节点
  - [ ] 解析配置项：
    - [ ] `max_history_messages`（最大历史消息数）
    - [ ] `max_context_tokens`（最大上下文Token数）
    - [ ] `default_include_agent_state`（默认是否包含Agent状态）
    - [ ] `default_include_project_context`（默认是否包含项目上下文）
  - [ ] 实现配置更新接口（`updateConfig()`）

- [ ] **配置验证**
  - [ ] 验证配置项的有效性（数值范围、布尔值等）
  - [ ] 实现配置验证方法（`validateConfig()`）

- [ ] **默认配置**
  - [ ] 定义默认配置值
  - [ ] 配置文件缺失时使用默认配置

**验收标准**：
- 单测验证：配置加载和验证正确。
- 单测验证：默认配置生效。

---

## 3.4 单元测试与示例

### 3.4.1 单元测试
- [ ] **ModelManager测试**
  - [ ] 模型配置加载测试
  - [ ] 模型注册/移除测试
  - [ ] 模型查询测试
  - [ ] 性能统计测试（请求计数、响应时间、成功率、负载因子）
  - [ ] 按任务类型查询模型测试

- [ ] **TaskRouter测试**
  - [ ] 路由表初始化测试
  - [ ] 任务类型匹配测试
  - [ ] 上下文容量检查测试
  - [ ] 模型评分计算测试
  - [ ] 负载均衡测试
  - [ ] 成本优化测试
  - [ ] 路由决策生成测试

- [ ] **ContextManager测试**
  - [ ] 对话历史管理测试（添加、查询、裁剪）
  - [ ] System Prompt构建测试
  - [ ] Agent状态上下文构建测试
  - [ ] 项目上下文构建测试
  - [ ] 代码上下文构建测试
  - [ ] 记忆事件上下文构建测试
  - [ ] 完整上下文构建测试
  - [ ] Token限制检查测试
  - [ ] 智能上下文裁剪测试
  - [ ] 消息重要性评分测试

---

*最后更新: 2025年12月21日*
