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
- [ ] **创建ErrorTypes.h文件**
  - [ ] 定义 `ErrorType` 枚举
    - [ ] `NetworkError` - 网络错误（连接失败、DNS解析失败等）
    - [ ] `RateLimitError` - 限流错误（429状态码）
    - [ ] `InvalidRequest` - 请求错误（400、401、403等）
    - [ ] `ServerError` - 服务器错误（500、502、503等）
    - [ ] `TimeoutError` - 超时错误
    - [ ] `UnknownError` - 未知错误
  - [ ] 定义错误严重程度枚举（Critical、Warning、Info）
  - [ ] 定义错误代码常量

#### 1.3.3.2 错误信息结构
- [ ] **定义错误信息结构**
  - [ ] 创建 `ErrorInfo` 结构体
    - [ ] errorType（错误类型）
    - [ ] errorCode（错误代码）
    - [ ] message（错误消息）
    - [ ] details（详细信息，可选）
    - [ ] timestamp（时间戳）
    - [ ] context（上下文信息，可选）
  - [ ] 实现错误信息序列化（JSON格式）

### 1.3.4 错误分类和识别

#### 1.3.4.1 HTTP错误识别
- [ ] **状态码到错误类型映射**
  - [ ] 实现状态码解析方法
  - [ ] 实现状态码到ErrorType的映射
  - [ ] 处理特殊状态码（如429限流）

- [ ] **网络错误识别**
  - [ ] 识别连接超时错误
  - [ ] 识别DNS解析错误
  - [ ] 识别SSL/TLS错误
  - [ ] 识别网络不可达错误

#### 1.3.4.2 API错误响应解析
- [ ] **SiliconFlow API错误格式解析**
  - [ ] 解析API错误响应JSON格式
  - [ ] 提取错误代码和消息
  - [ ] 处理错误详情字段
  - [ ] 实现错误响应到ErrorInfo的转换

#### 1.3.4.3 异常转换
- [ ] **C++异常到错误信息转换**
  - [ ] 捕获std::exception并转换为ErrorInfo
  - [ ] 捕获网络异常并转换为ErrorInfo
  - [ ] 捕获JSON解析异常并转换为ErrorInfo
  - [ ] 实现异常链追踪（可选）

### 1.3.5 重试策略配置

#### 1.3.5.1 重试配置结构
- [ ] **定义RetryPolicy结构**
  - [ ] maxRetries（最大重试次数）
  - [ ] initialDelayMs（初始延迟，毫秒）
  - [ ] backoffMultiplier（退避倍数，如2.0表示每次延迟翻倍）
  - [ ] maxDelayMs（最大延迟，毫秒）
  - [ ] retryableErrors（可重试错误类型映射）

#### 1.3.5.2 重试策略实现
- [ ] **重试判断逻辑**
  - [ ] 实现 `shouldRetry()` 方法
  - [ ] 检查错误类型是否可重试
  - [ ] 检查重试次数是否超限
  - [ ] 实现特殊错误类型的特殊重试策略（如限流错误）

- [ ] **退避算法实现**
  - [ ] 实现指数退避算法
  - [ ] 实现固定延迟退避
  - [ ] 实现随机抖动（jitter）避免惊群效应
  - [ ] 实现 `getRetryDelay()` 方法

#### 1.3.5.3 默认重试策略
- [ ] **定义默认重试策略**
  - [ ] 网络错误：重试3次，指数退避
  - [ ] 限流错误：重试5次，较长延迟
  - [ ] 服务器错误：重试2次，固定延迟
  - [ ] 请求错误：不重试
  - [ ] 超时错误：重试2次，指数退避

### 1.3.6 错误日志记录

#### 1.3.6.1 日志接口设计
- [ ] **定义日志级别**
  - [ ] ERROR（错误）
  - [ ] WARNING（警告）
  - [ ] INFO（信息）
  - [ ] DEBUG（调试）

- [ ] **实现日志记录方法**
  - [ ] `logError()` - 记录错误
  - [ ] `logWarning()` - 记录警告
  - [ ] `logInfo()` - 记录信息
  - [ ] `logDebug()` - 记录调试信息

#### 1.3.6.2 错误日志格式
- [ ] **定义日志格式**
  - [ ] 时间戳
  - [ ] 错误类型
  - [ ] 错误代码
  - [ ] 错误消息
  - [ ] 上下文信息（请求ID、模型ID等）
  - [ ] 堆栈追踪（可选，Debug模式）

#### 1.3.6.3 日志输出
- [ ] **实现日志输出**
  - [ ] 控制台输出（stdout/stderr）
  - [ ] 文件输出（可选）
  - [ ] 日志轮转（可选）
  - [ ] 日志级别过滤

### 1.3.7 单元测试
- [ ] **错误识别测试**
  - [ ] HTTP状态码错误识别测试
  - [ ] 网络错误识别测试
  - [ ] API错误响应解析测试

- [ ] **重试策略测试**
  - [ ] 重试判断逻辑测试
  - [ ] 退避算法测试
  - [ ] 重试次数限制测试
  - [ ] 特殊错误类型重试测试

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
- [ ] **设计配置文件结构**
  - [ ] 参考设计方案中的配置文件结构
  - [ ] 定义API配置节点（base_url、api_key、timeout等）
  - [ ] 定义模型配置节点（模型列表、参数等）
  - [ ] 定义路由配置节点（任务到模型映射）
  - [ ] 定义上下文配置节点（历史消息数、Token限制等）
  - [ ] 定义请求管理配置节点（队列大小、重试策略等）
  - [ ] 定义缓存配置节点（启用、TTL等）
  - [ ] 定义日志配置节点（级别、输出等）
  - [ ] 定义多模态配置节点（STT、TTS、VLM等）
  - [ ] 定义工具配置节点（MCP、代码工具等）

#### 1.4.3.2 配置文件模板创建
- [ ] **创建配置文件模板**
  - [ ] 创建 `config/ai_service_config.json` 文件
  - [ ] 填写完整的配置示例
  - [ ] 添加配置项说明注释
  - [ ] 添加默认值说明

### 1.4.4 配置文件加载

#### 1.4.4.1 配置文件读取
- [ ] **实现配置文件读取**
  - [ ] 实现 `loadConfig()` 方法
  - [ ] 支持从文件路径加载
  - [ ] 支持从JSON字符串加载
  - [ ] 实现文件不存在时的默认配置
  - [ ] 实现文件读取错误处理

#### 1.4.4.2 JSON解析
- [ ] **使用nlohmann/json解析**
  - [ ] 解析JSON配置文件
  - [ ] 处理JSON解析错误
  - [ ] 实现配置项访问方法（get/set）
  - [ ] 实现配置项类型转换

#### 1.4.4.3 配置缓存
- [ ] **实现配置缓存**
  - [ ] 缓存已加载的配置
  - [ ] 实现配置访问接口
  - [ ] 实现配置更新接口

### 1.4.5 配置验证

#### 1.4.5.1 配置项验证
- [ ] **实现配置验证逻辑**
  - [ ] 验证必需配置项是否存在
  - [ ] 验证配置项类型是否正确
  - [ ] 验证配置项值是否在有效范围内
  - [ ] 验证配置项之间的依赖关系

#### 1.4.5.2 配置验证规则
- [ ] **定义验证规则**
  - [ ] API配置验证（base_url格式、timeout范围等）
  - [ ] 模型配置验证（模型ID格式、参数范围等）
  - [ ] 路由配置验证（任务类型有效性等）
  - [ ] 数值范围验证（Token限制、超时时间等）

#### 1.4.5.3 验证错误处理
- [ ] **实现验证错误处理**
  - [ ] 收集所有验证错误
  - [ ] 生成详细的验证错误报告
  - [ ] 提供配置修复建议
  - [ ] 实现验证错误日志记录

### 1.4.6 环境变量支持

#### 1.4.6.1 环境变量读取
- [ ] **实现环境变量读取**
  - [ ] 实现跨平台环境变量读取（Windows/Linux）
  - [ ] 支持环境变量覆盖配置项
  - [ ] 定义环境变量命名规范

#### 1.4.6.2 敏感信息处理
- [ ] **API Key等敏感信息**
  - [ ] 支持从环境变量读取API Key
  - [ ] 支持 `${ENV_VAR}` 格式的配置值替换
  - [ ] 实现环境变量替换逻辑
  - [ ] 避免在日志中输出敏感信息

#### 1.4.6.3 环境变量映射
- [ ] **定义环境变量映射**
  - [ ] `SILICONFLOW_API_KEY` -> `api.api_key`
  - [ ] `SILICONFLOW_BASE_URL` -> `api.base_url`（可选）
  - [ ] `PROJECT_ROOT` -> `tools.project_root`
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
- [ ] **配置文件加载测试**
  - [ ] 正常配置文件加载测试
  - [ ] 文件不存在测试
  - [ ] JSON格式错误测试
  - [ ] 环境变量替换测试

- [ ] **配置验证测试**
  - [ ] 必需配置项缺失测试
  - [ ] 配置项类型错误测试
  - [ ] 配置项值范围错误测试
  - [ ] 配置依赖关系测试

- [ ] **环境变量测试**
  - [ ] 环境变量读取测试
  - [ ] 环境变量覆盖测试
  - [ ] 敏感信息处理测试

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
- [ ] **创建TaskType.h文件**
  - [ ] 定义 `TaskType` 枚举类
    - [ ] 对话类任务
      - [ ] `CasualChat` - 日常对话
      - [ ] `CodeDiscussion` - 代码讨论
      - [ ] `TechnicalQnA` - 技术问答
    - [ ] 代码相关任务
      - [ ] `CodeGeneration` - 代码生成
      - [ ] `CodeAnalysis` - 代码分析
      - [ ] `CodeReview` - 代码审查
      - [ ] `CodeExplanation` - 代码解释
      - [ ] `BugFix` - Bug修复
    - [ ] 项目理解任务
      - [ ] `ProjectAnalysis` - 项目分析
      - [ ] `ArchitectureDesign` - 架构设计
      - [ ] `Documentation` - 文档生成
    - [ ] Agent相关任务
      - [ ] `AgentDecision` - Agent决策辅助
      - [ ] `AgentReasoning` - Agent推理
      - [ ] `ContextUnderstanding` - 上下文理解
    - [ ] 语音视觉相关任务
      - [ ] `SpeechRecognition` - 语音识别
      - [ ] `SpeechSynthesis` - 语音合成
      - [ ] `VisionUnderstanding` - 视觉理解
      - [ ] `SceneAnalysis` - 场景分析
      - [ ] `ProactiveResponse` - 主动响应
    - [ ] 工具调用相关任务
      - [ ] `ToolCalling` - 工具调用
      - [ ] `CodeToolExecution` - 代码工具执行

#### 1.5.3.2 TaskType工具函数
- [ ] **实现TaskType工具函数**
  - [ ] `taskTypeToString()` - 枚举转字符串
  - [ ] `stringToTaskType()` - 字符串转枚举
  - [ ] `getTaskTypeDescription()` - 获取任务类型描述
  - [ ] `isCodeRelatedTask()` - 判断是否为代码相关任务
  - [ ] `isMultimodalTask()` - 判断是否为多模态任务

### 1.5.4 聊天消息结构（ChatMessage）

#### 1.5.4.1 定义ChatMessage结构
- [ ] **创建ChatMessage.h文件**
  - [ ] 定义 `ChatMessage` 结构体
    - [ ] role（角色：system/user/assistant/tool）
    - [ ] content（内容：字符串或结构化内容）
    - [ ] name（可选，工具调用时的工具名）
    - [ ] toolCallId（可选，工具调用ID）
  - [ ] 定义消息角色枚举 `MessageRole`

#### 1.5.4.2 多模态消息支持
- [ ] **支持多模态内容**
  - [ ] 定义 `MessageContent` 类型（文本或内容数组）
  - [ ] 支持文本内容
  - [ ] 支持图像内容（base64编码或URL）
  - [ ] 支持混合内容（文本+图像）

#### 1.5.4.3 ChatMessage工具函数
- [ ] **实现工具函数**
  - [ ] `ChatMessage::fromJson()` - 从JSON创建
  - [ ] `ChatMessage::toJson()` - 转换为JSON
  - [ ] `ChatMessage::estimateTokens()` - 估算Token数
  - [ ] `ChatMessage::isValid()` - 验证消息有效性

### 1.5.5 模型配置结构（ModelConfig）

#### 1.5.5.1 定义ModelConfig结构
- [ ] **创建ModelConfig.h文件**
  - [ ] 定义 `ModelConfig` 结构体
    - [ ] modelId（模型ID，如 "deepseek-ai/DeepSeek-V3"）
    - [ ] displayName（显示名称）
    - [ ] supportedTasks（支持的任务类型列表）
    - [ ] maxContextTokens（最大上下文Token数）
    - [ ] defaultTemperature（默认温度参数）
    - [ ] defaultMaxTokens（默认最大生成Token数）
    - [ ] costPer1kTokens（每1K Token的成本）
    - [ ] maxConcurrentRequests（最大并发请求数）
    - [ ] supportsStreaming（是否支持流式响应）
    - [ ] recommendedPromptStyle（推荐的提示词风格）
    - [ ] performanceScore（性能评分，0-1）

#### 1.5.5.2 ModelConfig工具函数
- [ ] **实现工具函数**
  - [ ] `ModelConfig::fromJson()` - 从JSON创建
  - [ ] `ModelConfig::toJson()` - 转换为JSON
  - [ ] `ModelConfig::supportsTask()` - 检查是否支持任务类型
  - [ ] `ModelConfig::isValid()` - 验证配置有效性

### 1.5.6 请求/响应结构（ChatRequest/ChatResponse）

#### 1.5.6.1 定义ChatRequest结构
- [ ] **创建RequestResponse.h文件**
  - [ ] 定义 `ChatRequest` 结构体
    - [ ] model（模型ID）
    - [ ] messages（消息列表）
    - [ ] temperature（温度参数）
    - [ ] maxTokens（最大生成Token数）
    - [ ] stream（是否流式响应）
    - [ ] stopSequence（停止序列，可选）
    - [ ] topP（Top-p采样，可选）
    - [ ] topK（Top-k采样，可选）
    - [ ] tools（工具列表，Function Calling）
    - [ ] toolChoice（工具选择策略）

#### 1.5.6.2 定义ChatResponse结构
- [ ] **定义ChatResponse结构**
  - [ ] content（响应内容）
  - [ ] toolCalls（工具调用列表）
  - [ ] finishReason（完成原因：stop/tool_calls/length等）
  - [ ] promptTokens（提示Token数）
  - [ ] completionTokens（完成Token数）
  - [ ] totalTokens（总Token数）
  - [ ] model（使用的模型ID）

#### 1.5.6.3 工具调用结构
- [ ] **定义工具调用相关结构**
  - [ ] `ToolCall` 结构体（id, type, function）
  - [ ] `FunctionCall` 结构体（name, arguments）
  - [ ] `Tool` 结构体（Function Calling工具定义）

#### 1.5.6.4 请求/响应工具函数
- [ ] **实现工具函数**
  - [ ] `ChatRequest::fromJson()` - 从JSON创建
  - [ ] `ChatRequest::toJson()` - 转换为JSON
  - [ ] `ChatRequest::estimateTokens()` - 估算Token数
  - [ ] `ChatResponse::fromJson()` - 从JSON创建
  - [ ] `ChatResponse::toJson()` - 转换为JSON
  - [ ] `ChatResponse::hasToolCalls()` - 检查是否有工具调用

### 1.5.7 任务优先级枚举（TaskPriority）

#### 1.5.7.1 定义TaskPriority枚举
- [ ] **创建TaskPriority.h文件**
  - [ ] 定义 `TaskPriority` 枚举类
    - [ ] `Critical` - 关键任务（最高优先级）
    - [ ] `High` - 高优先级
    - [ ] `Normal` - 普通优先级（默认）
    - [ ] `Low` - 低优先级

#### 1.5.7.2 TaskPriority工具函数
- [ ] **实现工具函数**
  - [ ] `taskPriorityToString()` - 枚举转字符串
  - [ ] `stringToTaskPriority()` - 字符串转枚举
  - [ ] `comparePriority()` - 比较优先级

### 1.5.8 通用类型定义（CommonTypes）

#### 1.5.8.1 定义通用类型
- [ ] **创建CommonTypes.h文件**
  - [ ] 定义常用类型别名
  - [ ] 定义时间戳类型
  - [ ] 定义请求ID类型
  - [ ] 定义配置路径类型

### 1.5.9 单元测试
- [ ] **TaskType测试**
  - [ ] 枚举转换测试
  - [ ] 工具函数测试

- [ ] **ChatMessage测试**
  - [ ] JSON序列化/反序列化测试
  - [ ] Token估算测试
  - [ ] 多模态消息测试

- [ ] **ModelConfig测试**
  - [ ] JSON序列化/反序列化测试
  - [ ] 任务支持检查测试
  - [ ] 配置验证测试

- [ ] **请求/响应测试**
  - [ ] JSON序列化/反序列化测试
  - [ ] Token估算测试
  - [ ] 工具调用测试

- [ ] **TaskPriority测试**
  - [ ] 枚举转换测试
  - [ ] 优先级比较测试

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
- [ ] HTTP库选择与集成
- [ ] HTTP请求封装
- [ ] HTTP响应处理
- [ ] 异步HTTP请求
- [ ] 连接池管理
- [ ] 重试机制基础框架
- [ ] 单元测试

**进度**: 0/7 主要模块完成

### 1.2 工具类实现
- [x] Token计数器（TokenCounter）
- [ ] 音频处理器（AudioProcessor）
- [ ] 单元测试

**进度**: 0/3 主要模块完成

### 1.3 错误处理系统
- [ ] 错误类型定义
- [ ] 错误分类和识别
- [ ] 重试策略配置
- [ ] 错误日志记录
- [ ] 单元测试

**进度**: 0/5 主要模块完成

### 1.4 配置管理系统
- [ ] 配置文件结构设计
- [ ] 配置文件加载
- [ ] 配置验证
- [ ] 环境变量支持
- [ ] 配置热重载（可选）
- [ ] 单元测试

**进度**: 0/6 主要模块完成

### 1.5 基础数据结构定义
- [ ] 任务类型枚举（TaskType）
- [ ] 聊天消息结构（ChatMessage）
- [ ] 模型配置结构（ModelConfig）
- [ ] 请求/响应结构（ChatRequest/ChatResponse）
- [ ] 任务优先级枚举（TaskPriority）
- [ ] 通用类型定义
- [ ] 单元测试

**进度**: 0/7 主要模块完成

**总计**: 0/28 主要模块完成

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

*最后更新: 2025年12月16日*
