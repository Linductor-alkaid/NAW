# Phase 1：基础设施层（Foundation Layer）详细任务清单

本文档是阶段一基础设施层的详细开发任务清单，基于《服务层设计方案》制定。

> **参考信息**：
> - 设计方案：`docs/design/服务层设计方案.md`
> - 总体任务清单：`docs/todolists/TODO_服务层开发任务清单.md`
> - API文档：https://docs.siliconflow.cn/cn/api-reference/chat-completions/chat-completions
> - 模型列表：https://cloud.siliconflow.cn/models
> - API Key获取：https://cloud.siliconflow.cn/account/ak

## 概述

基础设施层是服务层的底层支撑，提供HTTP通信、工具类、错误处理、配置管理和基础数据结构等核心功能。本阶段需要完成以下5个主要模块：

1. **HTTP客户端封装** - 提供与SiliconFlow API通信的基础能力
2. **工具类实现** - Token计数和音频处理等实用工具
3. **错误处理系统** - 统一的错误分类、识别和重试机制
4. **配置管理系统** - 配置文件的加载、验证和管理
5. **基础数据结构定义** - 定义服务层使用的核心数据结构

---

## 1.1 HTTP客户端封装（HttpClient）

### 1.1.1 任务概述
实现一个功能完整的HTTP客户端，支持同步/异步请求、连接池管理、超时控制和重试机制。

### 1.1.2 文件结构
```
include/naw/desktop_pet/service/utils/
├── HttpClient.h              # HTTP客户端头文件
└── HttpTypes.h               # HTTP相关类型定义

src/naw/desktop_pet/service/utils/
└── HttpClient.cpp            # HTTP客户端实现
```

### 1.1.3 详细任务清单

#### 1.1.3.1 HTTP库选择与集成
- [x] **调研HTTP库选项**
  - [x] 评估 cpp-httplib（轻量级，C++11，无依赖）
  - [x] 评估 libcurl（功能强大，跨平台，需要链接）
  - [x] 评估其他选项（如 httplib.h 单文件版本）
  - [x] 确定最终选择（推荐：cpp-httplib）
  - [x] 记录选择理由和权衡

- [x] **集成HTTP库到项目**
  - [x] 将HTTP库添加到 `third_party/` 或作为依赖
  - [x] 更新 CMakeLists.txt 添加HTTP库依赖
  - [x] 验证编译和链接成功

#### 1.1.3.2 HTTP请求封装
- [x] **定义HTTP请求结构**
  - [x] 创建 `HttpTypes.h` 文件
  - [x] 定义 `HttpRequest` 结构体
    - [x] method（GET/POST/PUT/DELETE等）
    - [x] url（完整URL）
    - [x] headers（请求头映射）
    - [x] body（请求体字符串）
    - [x] timeoutMs（超时时间，毫秒）
    - [x] followRedirects（是否跟随重定向）
  - [x] 定义 `HttpResponse` 结构体
    - [x] statusCode（状态码）
    - [x] headers（响应头映射）
    - [x] body（响应体字符串）
    - [x] error（错误信息，可选）

- [x] **实现GET请求**
  - [x] 实现 `get()` 方法
  - [x] 支持URL参数（query string）
  - [x] 支持自定义请求头
  - [x] 实现超时控制
  - [x] 实现错误处理

- [x] **实现POST请求**
  - [x] 实现 `post()` 方法
  - [x] 支持JSON请求体
  - [x] 支持表单数据（application/x-www-form-urlencoded）
  - [x] 支持二进制数据（multipart/form-data）
  - [x] 支持自定义Content-Type

- [x] **实现其他HTTP方法**
  - [x] PUT请求
  - [x] DELETE请求
  - [x] PATCH请求（如需要）

- [x] **请求头管理**
  - [x] 实现请求头设置方法
  - [x] 支持默认请求头（如User-Agent）
  - [x] 支持请求头合并和覆盖
  - [x] 实现请求头验证（防止无效字符）

- [x] **请求体序列化**
  - [x] JSON序列化（使用nlohmann/json）
  - [x] 表单数据序列化
  - [x] 二进制数据编码（Base64等）
  - [x] 流式数据支持（用于大文件上传）

#### 1.1.3.3 HTTP响应处理
- [x] **状态码处理**
  - [x] 定义状态码分类（2xx成功，4xx客户端错误，5xx服务器错误）
  - [x] 实现状态码检查方法
  - [x] 实现状态码到错误类型的映射

- [x] **响应头解析**
  - [x] 解析响应头字符串
  - [x] 提取关键响应头（Content-Type, Content-Length等）
  - [x] 处理多值响应头
  - [x] 实现响应头查询方法

- [x] **响应体反序列化**
  - [x] JSON反序列化
  - [x] 文本响应处理
  - [x] 二进制响应处理
  - [x] 流式响应处理（SSE格式）

#### 1.1.3.4 异步HTTP请求
- [x] **异步请求接口设计**
  - [x] 定义异步请求返回类型（std::future<HttpResponse>）
  - [x] 实现 `getAsync()` 方法
  - [x] 实现 `postAsync()` 方法
  - [x] 实现通用 `executeAsync()` 方法

- [x] **异步执行实现**
  - [x] 使用 std::async 或线程池
  - [x] 实现异步回调机制（可选）
  - [x] 处理异步异常传播
  - [x] 实现异步请求取消（可选）

#### 1.1.3.5 连接池管理
- [x] **连接池设计**
  - [x] 定义连接池配置（最大连接数、空闲超时等）
  - [x] 实现连接池数据结构
  - [x] 实现连接获取和释放机制

- [x] **连接复用**
  - [x] 实现HTTP Keep-Alive支持
  - [x] 实现连接复用逻辑
  - [ ] 实现连接健康检查
  - [x] 实现连接清理（超时连接）

- [x] **连接池统计**
  - [x] 记录活跃连接数
  - [x] 记录总连接数
  - [x] 记录连接复用率

#### 1.1.3.6 重试机制基础框架
- [x] **重试策略定义**
  - [x] 定义重试配置结构（最大重试次数、初始延迟、退避策略）
  - [x] 定义可重试错误类型
  - [x] 实现重试条件判断

- [x] **重试逻辑实现**
  - [x] 实现指数退避算法
  - [x] 实现固定延迟重试
  - [x] 实现自定义退避策略
  - [x] 实现重试日志记录

- [x] **重试集成**
  - [x] 在HTTP请求方法中集成重试逻辑
  - [x] 支持按请求配置重试策略
  - [x] 实现重试统计（重试次数、成功率等）

#### 1.1.3.7 单元测试
- [x] **HTTP请求测试**
  - [x] GET请求测试
  - [x] POST请求测试
  - [x] 请求头测试
  - [ ] 超时测试

- [x] **HTTP响应测试**
  - [x] 状态码处理测试
  - [x] 响应头解析测试
  - [x] JSON反序列化测试

- [x] **异步请求测试**
  - [x] 异步GET测试
  - [x] 异步POST测试
  - [x] 并发请求测试

- [x] **连接池测试**
  - [x] 连接复用测试
  - [x] 连接池容量测试
  - [x] 连接清理测试

- [x] **重试机制测试**
  - [x] 重试逻辑测试
  - [x] 退避策略测试
  - [x] 重试次数限制测试

---

## 1.2 工具类实现（Utils）

### 1.2.1 任务概述
实现Token计数器和音频处理器等实用工具类，为上层服务提供基础能力。

### 1.2.2 文件结构
```
include/naw/desktop_pet/service/utils/
├── TokenCounter.h            # Token计数器头文件
└── AudioProcessor.h         # 音频处理器头文件

src/naw/desktop_pet/service/utils/
├── TokenCounter.cpp         # Token计数器实现
└── AudioProcessor.cpp       # 音频处理器实现
```

### 1.2.3 Token计数器（TokenCounter）

#### 1.2.3.1 Token估算算法实现
- [x] **选择Token估算方法**
  - [x] 采用字符数估算（默认约4字符≈1 token，含固定开销），保留自定义规则入口；暂未集成tiktoken
  - [x] 在代码中内置SiliconFlow常用模型估算参数，可扩展配置
  - [x] 记录选择理由：轻量、无外部依赖、可扩展精度

- [x] **实现基础Token估算**
  - [x] 基于模型规则的字符数到Token转换，支持固定开销
  - [x] 提供文本Token估算接口 `estimateTokens(model, text)`
  - [x] 支持按模型自定义规则（tokensPerChar、fixedOverhead）
  - [ ] 添加估算误差说明文档（待补充）

- [ ] **优化Token估算精度**
  - [x] 添加可配置的更精细规则入口（BPE策略、可配置词表）
  - [ ] 考虑不同语言/代码文本的密度差异
  - [ ] 添加Token估算校准机制（可选）

#### 1.2.3.2 消息Token计数
- [ ] **ChatMessage Token计数**
  - [ ] 定义ChatMessage结构（role, content）
  - [ ] 实现单条/列表消息Token计数（包含role开销）

- [ ] **多模态消息Token计数**
  - [ ] 文本消息Token计数
  - [ ] 图像消息估算（基于尺寸/分辨率）
  - [ ] 混合消息Token计数

#### 1.2.3.3 上下文Token统计
- [ ] **上下文Token统计接口**
  - [ ] `estimateContextTokens()`，覆盖对话历史/系统提示/项目上下文

- [ ] **Token限制检查**
  - [ ] Token限制检查与裁剪建议
  - [ ] Token使用率计算

- [ ] **Token统计报告**
  - [ ] 按消息类型分类统计
  - [ ] Token使用趋势分析（可选）

#### 1.2.3.4 单元测试
- [x] **Token估算基础测试**
  - [x] 默认规则估算
  - [x] 自定义模型规则估算
  - [x] 计数累积与重置

- [ ] **消息/上下文相关测试**
  - [ ] 消息列表Token计数测试
  - [ ] 多模态消息测试
  - [ ] 上下文Token统计与限制检查测试

### 1.2.4 音频处理器（AudioProcessor）

#### 1.2.4.1 音频格式转换
- [x] **音频格式支持**
  - [x] 调研音频格式库（基于 miniaudio，内置 WAV/MP3/PCM 解码）
  - [x] 确定支持的音频格式（WAV、MP3、PCM 等）
  - [x] 实现音频格式检测（`probeFile()` 返回采样率/声道/格式）

- [x] **格式转换实现**
  - [x] 实现WAV到PCM转换（`decodeFileToPCM`）
  - [x] 实现MP3到PCM转换（miniaudio 通用解码）
  - [x] 实现PCM到WAV转换（`writePcmToWav`）
  - [x] 实现采样率转换（`decodeFileToPCM` 支持目标采样率）
  - [x] 实现声道转换（`decodeFileToPCM` 支持目标声道）

#### 1.2.4.2 音频数据预处理
- [ ] **音频数据验证**
  - [ ] 实现音频数据格式验证
  - [ ] 实现采样率验证
  - [ ] 实现音频长度检查
  - [ ] 实现音频质量检查

- [ ] **音频数据标准化**
  - [ ] 实现音频数据归一化
  - [ ] 实现音量调整
  - [ ] 实现静音检测和裁剪
  - [ ] 实现噪声抑制（可选）

#### 1.2.4.3 音频流处理
- [x] **流式音频处理接口**
  - [x] 定义音频流处理回调接口（`CaptureOptions::onData`）
  - [ ] 实现流式音频格式转换
  - [ ] 实现流式音频数据预处理

- [x] **音频流管理**
  - [x] 实现音频流缓冲区管理（录音缓存与 VAD 环形缓冲）
  - [x] 实现音频流状态管理（录音/被动监听状态机、播放暂停恢复）
  - [ ] 实现音频流错误处理

#### 1.2.4.4 单元测试
- [ ] **音频格式转换测试**
  - [ ] WAV到PCM转换测试
  - [ ] PCM到WAV转换测试
  - [ ] 采样率转换测试

- [ ] **音频预处理测试**
  - [ ] 音频数据验证测试
  - [ ] 音频标准化测试
  - [ ] 静音检测测试

- [ ] **音频流处理测试**
  - [ ] 流式转换测试
  - [ ] 流式预处理测试
  - [ ] 流式错误处理测试

---

## 1.3 错误处理系统（ErrorHandler）

### 1.3.1 任务概述
实现统一的错误分类、识别、重试策略和错误日志记录系统。

### 1.3.2 文件结构
```
include/naw/desktop_pet/service/
├── ErrorHandler.h           # 错误处理器头文件
└── ErrorTypes.h             # 错误类型定义

src/naw/desktop_pet/service/
└── ErrorHandler.cpp         # 错误处理器实现
```

### 1.3.3 错误类型定义

#### 1.3.3.1 定义错误类型枚举
- [x] **创建ErrorTypes.h文件**
  - [x] 定义 `ErrorType` 枚举
    - [x] `NetworkError` - 网络错误（连接失败、DNS解析失败等）
    - [x] `RateLimitError` - 限流错误（429状态码）
    - [x] `InvalidRequest` - 请求错误（400、401、403等）
    - [x] `ServerError` - 服务器错误（500、502、503等）
    - [x] `TimeoutError` - 超时错误
    - [x] `UnknownError` - 未知错误
  - [x] 定义错误严重程度枚举（Critical、Warning、Info）
  - [ ] 定义错误代码常量

#### 1.3.3.2 错误信息结构
- [x] **定义错误信息结构**
  - [x] 创建 `ErrorInfo` 结构体
    - [x] errorType（错误类型）
    - [x] errorCode（错误代码）
    - [x] message（错误消息）
    - [x] details（详细信息，可选）
    - [x] timestamp（时间戳）
    - [x] context（上下文信息，可选）
  - [x] 实现错误信息序列化（JSON格式）

### 1.3.4 错误分类和识别

#### 1.3.4.1 HTTP错误识别
- [x] **状态码到错误类型映射**
  - [x] 实现状态码解析方法
  - [x] 实现状态码到ErrorType的映射
  - [x] 处理特殊状态码（如429限流）

- [ ] **网络错误识别**
  - [x] 识别连接超时错误（基于 transport error 文案包含 timeout）
  - [ ] 识别DNS解析错误
  - [ ] 识别SSL/TLS错误
  - [ ] 识别网络不可达错误

#### 1.3.4.2 API错误响应解析
- [x] **SiliconFlow API错误格式解析**
  - [x] 解析API错误响应JSON格式
  - [x] 提取错误代码和消息
  - [x] 处理错误详情字段
  - [x] 实现错误响应到ErrorInfo的转换

#### 1.3.4.3 异常转换
- [ ] **C++异常到错误信息转换**
  - [ ] 捕获std::exception并转换为ErrorInfo
  - [ ] 捕获网络异常并转换为ErrorInfo
  - [ ] 捕获JSON解析异常并转换为ErrorInfo
  - [ ] 实现异常链追踪（可选）

### 1.3.5 重试策略配置

#### 1.3.5.1 重试配置结构
- [x] **定义RetryPolicy结构**
  - [x] maxRetries（最大重试次数）
  - [x] initialDelayMs（初始延迟，毫秒）
  - [x] backoffMultiplier（退避倍数，如2.0表示每次延迟翻倍）
  - [x] maxDelayMs（最大延迟，毫秒）
  - [x] retryableErrors（可重试错误类型映射）

#### 1.3.5.2 重试策略实现
- [x] **重试判断逻辑**
  - [x] 实现 `shouldRetry()` 方法
  - [x] 检查错误类型是否可重试
  - [x] 检查重试次数是否超限
  - [x] 实现特殊错误类型的特殊重试策略（如限流错误）

- [x] **退避算法实现**
  - [x] 实现指数退避算法
  - [x] 实现固定延迟退避
  - [x] 实现随机抖动（jitter）避免惊群效应
  - [x] 实现 `getRetryDelay()` 方法

#### 1.3.5.3 默认重试策略
- [x] **定义默认重试策略**
  - [x] 网络错误：重试3次，指数退避
  - [x] 限流错误：重试5次，较长延迟（支持 Retry-After 优先）
  - [x] 服务器错误：重试2次，固定延迟
  - [x] 请求错误：不重试
  - [x] 超时错误：重试2次，指数退避

### 1.3.6 错误日志记录

#### 1.3.6.1 日志接口设计
- [x] **定义日志级别**
  - [x] ERROR（错误）
  - [x] WARNING（警告）
  - [x] INFO（信息）
  - [x] DEBUG（调试）

- [x] **实现日志记录方法**
  - [x] `log()` - 记录错误/警告/信息/调试（stderr 输出）

#### 1.3.6.2 错误日志格式
- [x] **定义日志格式**
  - [x] 时间戳
  - [x] 错误类型
  - [x] 错误代码
  - [x] 错误消息
  - [x] 上下文信息（请求ID、模型ID等，当前实现为可选 context 字段）
  - [ ] 堆栈追踪（可选，Debug模式）

#### 1.3.6.3 日志输出
- [x] **实现日志输出**
  - [x] 控制台输出（stdout/stderr）（当前：stderr）
  - [ ] 文件输出（可选）
  - [ ] 日志轮转（可选）
  - [x] 日志级别过滤

### 1.3.7 单元测试
- [x] **错误识别测试**
  - [x] HTTP状态码错误识别测试
  - [x] 网络错误识别测试（timeout 文案分类）
  - [x] API错误响应解析测试

- [x] **重试策略测试**
  - [x] 重试判断逻辑测试
  - [x] 退避算法测试（含 Retry-After 秒数）
  - [x] 重试次数限制测试
  - [x] 特殊错误类型重试测试（429）

- [ ] **错误日志测试**
  - [ ] 日志记录测试
  - [ ] 日志格式测试
  - [ ] 日志级别过滤测试

---

## 1.4 配置管理系统（ConfigManager）

### 1.4.1 任务概述
实现配置文件的加载、验证、环境变量支持和热重载功能。

### 1.4.2 文件结构
```
include/naw/desktop_pet/service/
└── ConfigManager.h           # 配置管理器头文件

src/naw/desktop_pet/service/
└── ConfigManager.cpp         # 配置管理器实现

config/
└── ai_service_config.json    # 配置文件模板
```

### 1.4.3 配置文件结构设计

#### 1.4.3.1 JSON配置文件设计
- [x] **设计配置文件结构**
  - [x] 参考设计方案中的配置文件结构
  - [x] 定义API配置节点（base_url、api_key、timeout等）
  - [x] 定义模型配置节点（模型列表、参数等）
  - [x] 定义路由配置节点（任务到模型映射）
  - [x] 定义上下文配置节点（历史消息数、Token限制等）
  - [x] 定义请求管理配置节点（队列大小、重试策略等）
  - [x] 定义缓存配置节点（启用、TTL等）
  - [x] 定义日志配置节点（级别、输出等）
  - [ ] 定义多模态配置节点（STT、TTS、VLM等）
  - [x] 定义工具配置节点（MCP、代码工具等）

#### 1.4.3.2 配置文件模板创建
- [x] **创建配置文件模板**
  - [x] 创建 `config/ai_service_config.json` 文件
  - [x] 填写完整的配置示例
  - [x] 添加配置项说明注释（使用 `_comment` 字段）
  - [x] 添加默认值说明

### 1.4.4 配置文件加载

#### 1.4.4.1 配置文件读取
- [x] **实现配置文件读取**
  - [x] 实现 `loadConfig()` 方法（当前实现为 `loadFromFile/loadFromString`）
  - [x] 支持从文件路径加载
  - [x] 支持从JSON字符串加载
  - [x] 实现文件不存在时的默认配置
  - [x] 实现文件读取错误处理

#### 1.4.4.2 JSON解析
- [x] **使用nlohmann/json解析**
  - [x] 解析JSON配置文件
  - [x] 处理JSON解析错误（失败不覆盖旧配置）
  - [x] 实现配置项访问方法（get/set，支持 key-path）
  - [x] 实现配置项类型转换（由 `nlohmann::json::get<T>()` 完成）

#### 1.4.4.3 配置缓存
- [x] **实现配置缓存**
  - [x] 缓存已加载的配置（线程安全）
  - [x] 实现配置访问接口
  - [x] 实现配置更新接口

### 1.4.5 配置验证

#### 1.4.5.1 配置项验证
- [x] **实现配置验证逻辑**
  - [x] 验证必需配置项是否存在
  - [x] 验证配置项类型是否正确
  - [x] 验证配置项值是否在有效范围内
  - [x] 验证配置项之间的依赖关系（routing->models：warning）

#### 1.4.5.2 配置验证规则
- [x] **定义验证规则**
  - [x] API配置验证（base_url格式、timeout范围等）
  - [x] 模型配置验证（模型ID存在性、supported_tasks 类型等）
  - [ ] 路由配置验证（任务类型有效性等；1.5 TaskType 已就绪，可直接实现）
  - [x] 数值范围验证（timeout 范围等）

#### 1.4.5.3 验证错误处理
- [x] **实现验证错误处理**
  - [x] 收集所有验证错误（`validate()` 返回字符串列表）
  - [x] 生成详细的验证错误报告（逐条返回）
  - [ ] 提供配置修复建议（可后续增强）
  - [ ] 实现验证错误日志记录（可后续增强）

### 1.4.6 环境变量支持

#### 1.4.6.1 环境变量读取
- [x] **实现环境变量读取**
  - [x] 实现跨平台环境变量读取（Windows/Linux）
  - [x] 支持环境变量覆盖配置项
  - [x] 定义环境变量命名规范（SILICONFLOW_*/PROJECT_ROOT）

#### 1.4.6.2 敏感信息处理
- [x] **API Key等敏感信息**
  - [x] 支持从环境变量读取API Key
  - [x] 支持 `${ENV_VAR}` 格式的配置值替换
  - [x] 实现环境变量替换逻辑（递归遍历 JSON）
  - [x] 避免在日志中输出敏感信息（提供 `redactSensitive()`）

#### 1.4.6.3 环境变量映射
- [x] **定义环境变量映射**
  - [x] `SILICONFLOW_API_KEY` -> `api.api_key`
  - [x] `SILICONFLOW_BASE_URL` -> `api.base_url`（可选）
  - [x] `PROJECT_ROOT` -> `tools.project_root`
  - [ ] 其他环境变量映射

### 1.4.7 配置热重载（可选）

#### 1.4.7.1 配置文件监控
- [ ] **实现文件监控**
  - [ ] 使用文件系统监控API（如inotify、ReadDirectoryChangesW）
  - [ ] 监控配置文件变化
  - [ ] 实现跨平台文件监控（Windows/Linux）

#### 1.4.7.2 配置重载逻辑
- [ ] **实现配置重载**
  - [ ] 检测配置文件变化
  - [ ] 重新加载配置文件
  - [ ] 验证新配置
  - [ ] 应用新配置（更新内部状态）
  - [ ] 通知配置变更（回调机制）

#### 1.4.7.3 配置重载安全
- [ ] **实现安全重载**
  - [ ] 验证新配置有效性
  - [ ] 实现配置回滚机制（新配置无效时）
  - [ ] 实现配置变更日志
  - [ ] 避免重载过程中的竞态条件

### 1.4.8 单元测试
- [x] **配置文件加载测试**
  - [ ] 正常配置文件加载测试
  - [x] 文件不存在测试
  - [x] JSON格式错误测试
  - [ ] 环境变量替换测试

- [x] **配置验证测试**
  - [x] 必需配置项缺失测试
  - [ ] 配置项类型错误测试
  - [ ] 配置项值范围错误测试
  - [ ] 配置依赖关系测试

- [x] **环境变量测试**
  - [ ] 环境变量读取测试
  - [ ] 环境变量覆盖测试
  - [x] 敏感信息处理测试

- [ ] **配置热重载测试**（如果实现）
  - [ ] 配置文件变化检测测试
  - [ ] 配置重载测试
  - [ ] 配置回滚测试

---

## 1.5 基础数据结构定义（Types）

### 1.5.1 任务概述
定义服务层使用的核心数据结构，包括任务类型、消息结构、模型配置、请求/响应结构和优先级等。

### 1.5.2 文件结构
```
include/naw/desktop_pet/service/types/
├── TaskType.h                # 任务类型枚举
├── ChatMessage.h             # 聊天消息结构
├── ModelConfig.h             # 模型配置结构
├── RequestResponse.h         # 请求/响应结构
├── TaskPriority.h            # 任务优先级枚举
└── CommonTypes.h             # 通用类型定义
```

### 1.5.3 任务类型枚举（TaskType）

#### 1.5.3.1 定义TaskType枚举
- [x] **创建TaskType.h文件**
  - [x] 定义 `TaskType` 枚举类（对齐《服务层设计方案》）
    - [x] 对话类任务：`CasualChat` / `CodeDiscussion` / `TechnicalQnA`
    - [x] 代码相关任务：`CodeGeneration` / `CodeAnalysis` / `CodeReview` / `CodeExplanation` / `BugFix`
    - [x] 项目理解任务：`ProjectAnalysis` / `ArchitectureDesign` / `Documentation`
    - [x] Agent相关任务：`AgentDecision` / `AgentReasoning` / `ContextUnderstanding`
    - [x] 语音视觉相关任务：`SpeechRecognition` / `SpeechSynthesis` / `VisionUnderstanding` / `SceneAnalysis` / `ProactiveResponse`
    - [x] 工具调用相关任务：`ToolCalling` / `CodeToolExecution`

#### 1.5.3.2 TaskType工具函数
- [x] **实现TaskType工具函数**
  - [x] `taskTypeToString()` - 枚举转字符串
  - [x] `stringToTaskType()` - 字符串转枚举（大小写不敏感）
  - [x] `getTaskTypeDescription()` - 获取任务类型描述
  - [x] `isCodeRelatedTask()` - 判断是否为代码相关任务
  - [x] `isMultimodalTask()` - 判断是否为多模态任务

### 1.5.4 聊天消息结构（ChatMessage）

#### 1.5.4.1 定义ChatMessage结构
- [x] **创建ChatMessage.h文件**
  - [x] 定义 `ChatMessage` 结构体（SiliconFlow/OpenAI 兼容：支持文本与多模态数组）
    - [x] role（角色：system/user/assistant/tool）
    - [x] content（支持 string 或 content 数组：text/image_url）
    - [x] name（可选，工具调用时的工具名）
    - [x] toolCallId（可选，工具调用ID）
  - [x] 定义消息角色枚举 `MessageRole`

#### 1.5.4.2 多模态消息支持
- [x] **支持多模态内容（兼容 SiliconFlow/OpenAI）**
  - [x] 定义 `MessageContent` 类型（文本或内容数组：text/image_url）
  - [x] 支持文本内容（`{type:\"text\", text:\"...\"}`）
  - [x] 支持图像内容（`{type:\"image_url\", image_url:{url:\"http(s)://...\"}}`）
  - [x] 支持 data URL（base64）：`data:image/<png|jpg|jpeg|webp>;base64,...`（轻量校验）
  - [x] 支持混合内容（文本+图像）

#### 1.5.4.3 ChatMessage工具函数
- [x] **实现工具函数**
  - [x] `ChatMessage::fromJson()` - 从JSON创建（兼容 snake_case/camelCase；支持 OpenAI content array）
  - [x] `ChatMessage::toJson()` - 转换为JSON（输出 OpenAI 兼容 content 结构）
  - [x] `ChatMessage::estimateTokens()` - 粗略估算Token数（text 使用 TokenEstimator；image_url 使用固定/按大小估算）
  - [x] `ChatMessage::isValid()` - 验证消息有效性

### 1.5.5 模型配置结构（ModelConfig）

#### 1.5.5.1 定义ModelConfig结构
- [x] **创建ModelConfig.h文件**
  - [x] 定义 `ModelConfig` 结构体
    - [x] modelId（模型ID，如 "deepseek-ai/DeepSeek-V3"）
    - [x] displayName（显示名称）
    - [x] supportedTasks（支持的任务类型列表）
    - [x] maxContextTokens（最大上下文Token数）
    - [x] defaultTemperature（默认温度参数）
    - [x] defaultMaxTokens（默认最大生成Token数）
    - [x] costPer1kTokens（每1K Token的成本）
    - [x] maxConcurrentRequests（最大并发请求数）
    - [x] supportsStreaming（是否支持流式响应）
    - [x] recommendedPromptStyle（推荐的提示词风格，可选）
    - [x] performanceScore（性能评分，0-1）

#### 1.5.5.2 ModelConfig工具函数
- [x] **实现工具函数**
  - [x] `ModelConfig::fromJson()` - 从JSON创建（兼容 snake_case/camelCase）
  - [x] `ModelConfig::toJson()` - 转换为JSON（输出 snake_case）
  - [x] `ModelConfig::supportsTask()` - 检查是否支持任务类型
  - [x] `ModelConfig::isValid()` - 验证配置有效性

### 1.5.6 请求/响应结构（ChatRequest/ChatResponse）

#### 1.5.6.1 定义ChatRequest结构
- [x] **创建RequestResponse.h文件**
  - [x] 定义 `ChatRequest` 结构体
    - [x] model（模型ID）
    - [x] messages（消息列表）
    - [x] temperature（温度参数，可选）
    - [x] maxTokens（最大生成Token数，可选；支持 snake_case/camelCase 输入）
    - [x] stream（是否流式响应，可选）
    - [x] stop（停止序列，可选）
    - [x] topP（Top-p采样，可选；支持 snake_case/camelCase 输入）
    - [x] topK（Top-k采样，可选；支持 snake_case/camelCase 输入）
    - [x] tools（工具列表，Function Calling）
    - [x] toolChoice（工具选择策略）

#### 1.5.6.2 定义ChatResponse结构
- [x] **定义ChatResponse结构**
  - [x] content（响应内容）
  - [x] toolCalls（工具调用列表）
  - [x] finishReason（完成原因：stop/tool_calls/length等）
  - [x] promptTokens（提示Token数）
  - [x] completionTokens（完成Token数）
  - [x] totalTokens（总Token数）
  - [x] model（使用的模型ID，可选）

#### 1.5.6.3 工具调用结构
- [x] **定义工具调用相关结构**
  - [x] `ToolCall` 结构体（id, type, function）
  - [x] `FunctionCall` 结构体（name, arguments）
  - [x] `Tool` 结构体（Function Calling工具定义；toJson 输出 OpenAI 兼容 schema）

#### 1.5.6.4 请求/响应工具函数
- [x] **实现工具函数**
  - [x] `ChatRequest::fromJson()` - 从JSON创建（兼容 snake_case/camelCase）
  - [x] `ChatRequest::toJson()` - 转换为JSON（输出 snake_case）
  - [x] `ChatRequest::estimateTokens()` - 文本估算Token数
  - [x] `ChatResponse::fromJson()` - 从JSON创建（兼容 OpenAI shape + 简化 shape）
  - [x] `ChatResponse::toJson()` - 转换为JSON（输出 snake_case，简化结构）
  - [x] `ChatResponse::hasToolCalls()` - 检查是否有工具调用

### 1.5.7 任务优先级枚举（TaskPriority）

#### 1.5.7.1 定义TaskPriority枚举
- [x] **创建TaskPriority.h文件**
  - [x] 定义 `TaskPriority` 枚举类
    - [x] `Critical` - 关键任务（最高优先级）
    - [x] `High` - 高优先级
    - [x] `Normal` - 普通优先级（默认）
    - [x] `Low` - 低优先级

#### 1.5.7.2 TaskPriority工具函数
- [x] **实现工具函数**
  - [x] `taskPriorityToString()` - 枚举转字符串
  - [x] `stringToTaskPriority()` - 字符串转枚举（大小写不敏感）
  - [x] `comparePriority()` - 比较优先级

### 1.5.8 通用类型定义（CommonTypes）

#### 1.5.8.1 定义通用类型
- [x] **创建CommonTypes.h文件**
  - [x] 定义常用类型别名
  - [x] 定义时间戳类型
  - [x] 定义请求ID类型
  - [x] 定义配置路径类型

### 1.5.9 单元测试
- [x] **TaskType测试**
  - [x] 枚举转换测试
  - [x] 工具函数测试

- [x] **ChatMessage测试**
  - [x] JSON序列化/反序列化测试
  - [x] Token估算测试
  - [x] 多模态消息测试（text+image_url，含 data URL base64 校验）

- [x] **ModelConfig测试**
  - [x] JSON序列化/反序列化测试
  - [x] 任务支持检查测试
  - [x] 配置验证测试

- [x] **请求/响应测试**
  - [x] JSON序列化/反序列化测试
  - [x] Token估算测试
  - [x] 工具调用测试

- [x] **TaskPriority测试**
  - [x] 枚举转换测试
  - [x] 优先级比较测试

---

## 开发顺序建议

### 第一阶段：基础数据结构（1.5）
1. 先完成基础数据结构定义（TaskType、ChatMessage、ModelConfig等）
2. 这些是其他模块的依赖，需要优先完成

### 第二阶段：配置管理（1.4）
1. 完成配置管理系统
2. 为其他模块提供配置支持

### 第三阶段：HTTP客户端（1.1）
1. 完成HTTP客户端封装
2. 这是API通信的基础

### 第四阶段：工具类（1.2）
1. 完成Token计数器和音频处理器
2. 为上层服务提供工具支持

### 第五阶段：错误处理（1.3）
1. 完成错误处理系统
2. 集成到HTTP客户端和其他模块中

---

## 进度追踪

### 1.1 HTTP客户端封装
- [x] HTTP库选择与集成
- [x] HTTP请求封装
- [x] HTTP响应处理
- [x] 异步HTTP请求
- [x] 连接池管理
- [x] 重试机制基础框架
- [x] 单元测试

**进度**: 7/7 主要模块完成

### 1.2 工具类实现
- [x] Token计数器（TokenCounter）
- [x] 音频处理器（AudioProcessor）
- [x] 单元测试

**进度**: 3/3 主要模块完成

### 1.3 错误处理系统
- [x] 错误类型定义
- [x] 错误分类和识别
- [x] 重试策略配置
- [x] 错误日志记录
- [x] 单元测试

**进度**: 5/5 主要模块完成

### 1.4 配置管理系统
- [x] 配置文件结构设计
- [x] 配置文件加载
- [x] 配置验证
- [x] 环境变量支持
- [ ] 配置热重载（可选）
- [x] 单元测试

**进度**: 5/6 主要模块完成

### 1.5 基础数据结构定义
- [x] 任务类型枚举（TaskType）
- [x] 聊天消息结构（ChatMessage）
- [x] 模型配置结构（ModelConfig）
- [x] 请求/响应结构（ChatRequest/ChatResponse）
- [x] 任务优先级枚举（TaskPriority）
- [x] 通用类型定义
- [x] 单元测试

**进度**: 7/7 主要模块完成

**总计**: 27/28 主要模块完成

---

## 依赖关系

```
1.5 基础数据结构定义
  ↓
1.4 配置管理系统（使用1.5的数据结构）
  ↓
1.3 错误处理系统
  ↓
1.1 HTTP客户端封装（使用1.3的错误处理）
  ↓
1.2 工具类实现（使用1.5的数据结构）
```

---

## 注意事项

1. **HTTP库选择**：建议使用cpp-httplib，轻量级且易于集成
2. **Token估算**：初期使用简单估算方法，后续可集成tiktoken提高精度
3. **配置管理**：确保支持环境变量，特别是API Key等敏感信息
4. **错误处理**：错误处理系统需要被其他模块广泛使用，设计要通用
5. **单元测试**：每个模块都要有完整的单元测试，确保质量

---

*最后更新: 2025年12月18日*
