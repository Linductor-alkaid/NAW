# Phase 5：工具调用层（Tool Calling Layer）详细任务清单

本文档是阶段五工具调用层的详细开发任务清单，基于《服务层设计方案》制定，并以 Phase1、Phase2、Phase3、Phase4 已完成的基础设施层、API 客户端层、核心管理层和服务管理层为依赖。

> **参考信息**：
> - 设计方案：`docs/design/服务层设计方案.md`
> - Phase1 详细清单：`docs/todolists/service_layer/TODO_Phase1_基础设施层.md`
> - Phase2 详细清单：`docs/todolists/service_layer/TODO_Phase2_API客户端层.md`
> - Phase3 详细清单：`docs/todolists/service_layer/TODO_Phase3_核心管理层.md`
> - Phase4 详细清单：`docs/todolists/service_layer/TODO_Phase4_服务管理层.md`
> - 总体任务清单：`docs/todolists/TODO_服务层开发任务清单.md`

## 概述

### 目标
- 实现 **工具管理器（ToolManager）**：管理所有可用工具，支持工具注册、查询和执行，为 Function Calling 提供基础支持。
- 实现 **代码工具集（CodeTools）**：提供代码开发相关的标准工具（read_file、write_file、list_files、search_code、get_project_structure、analyze_code等）。
- 实现 **Function Calling处理器（FunctionCallingHandler）**：处理LLM返回的工具调用请求，执行工具并构建后续请求。
- 实现 **MCP服务（MCPService）**：实现 Model Context Protocol 协议，提供标准化的工具接口和项目上下文访问。
- 实现 **项目上下文收集器（ProjectContextCollector）**：分析项目结构，收集项目信息，为LLM提供项目上下文。

### 非目标（明确留到后续Phase）
- 多模态服务（Phase6）。
- 服务层主接口整合（Phase7）。

### 依赖与关键组件（Phase5 前置）
- [x] Phase1 已提供：
  - `ConfigManager`：配置文件加载和环境变量支持。
  - `types::TaskType`、`types::ChatRequest`、`types::ChatResponse`、`types::ToolCall` 等基础数据结构。
  - `ErrorHandler`：统一错误处理和重试策略。
  - `utils::TokenCounter`：Token计数工具。
- [x] Phase2 已提供：
  - `APIClient`：同步/异步/流式 API 调用能力，支持 Function Calling 请求和响应。
  - `types::Tool`、`types::ToolCall`：工具定义和工具调用数据结构。
- [x] Phase3 已提供：
  - `ModelManager`：模型配置和性能统计。
  - `TaskRouter`：任务路由和模型选择。
  - `ContextManager`：上下文构建和管理。
- [x] Phase4 已提供：
  - `RequestManager`：请求队列管理和并发控制。
  - `ResponseHandler`：响应处理和验证。
  - `CacheManager`：响应缓存管理。

---

## 5.1 工具管理器（ToolManager）

### 5.1.1 任务概述
实现工具的统一管理，包括工具注册、查询和执行，为 Function Calling 提供基础支持。工具管理器是所有工具的执行入口，负责工具定义的管理和工具调用的分发。

### 5.1.2 文件结构（建议）
```
include/naw/desktop_pet/service/
└── ToolManager.h

src/naw/desktop_pet/service/
└── ToolManager.cpp
```

### 5.1.3 详细任务清单

#### 5.1.3.1 工具定义结构
- [x] **工具定义数据结构设计**
  - [x] 定义 `ToolDefinition` 结构体
    - [x] `name`（工具名称，唯一标识）
    - [x] `description`（工具描述，用于 Function Calling）
    - [x] `parametersSchema`（参数Schema，JSON Schema格式）
    - [x] `handler`（工具处理器函数：`std::function<nlohmann::json(const nlohmann::json&)>`）
  - [x] 实现工具定义的序列化/反序列化（可选，用于配置化工具）
  - [x] 实现工具定义验证（名称非空、Schema有效等）

- [x] **工具参数Schema定义**
  - [x] 支持 JSON Schema 格式（`type`、`properties`、`required` 等）
  - [x] 实现Schema验证（参数类型检查、必需字段检查）
  - [x] 支持复杂类型（`object`、`array`、`string`、`number`、`boolean`）
  - [x] 支持嵌套对象和数组

**验收标准**：
- 单测验证：工具定义结构能正确存储和检索。
- 单测验证：参数Schema验证正确（类型检查、必需字段检查）。

#### 5.1.3.2 工具注册机制
- [x] **工具注册接口**
  - [x] 实现 `registerTool(const ToolDefinition& tool)` 方法
    - [x] 验证工具定义有效性（名称非空、Handler非空）
    - [x] 检查工具名称是否已存在（可选：支持覆盖或拒绝）
    - [x] 验证参数Schema格式（使用JSON Schema验证）
    - [x] 将工具存储到内部映射（`toolName -> ToolDefinition`）
    - [x] 线程安全（使用 mutex 保护）
  - [x] 实现 `unregisterTool(const std::string& toolName)` 方法
    - [x] 检查工具是否存在
    - [x] 从映射中移除
  - [x] 实现批量注册接口（`registerTools(const std::vector<ToolDefinition>&)`，可选）

- [x] **工具处理器注册**
  - [x] 支持函数指针注册（`std::function`）
  - [x] 支持 Lambda 表达式注册
  - [x] 支持成员函数注册（使用 `std::bind` 或 Lambda 包装）
  - [x] 处理器签名：`nlohmann::json handler(const nlohmann::json& arguments)`

- [x] **工具权限控制（可选）**
  - [x] 定义权限级别（`Public`、`Restricted`、`Admin` 等，可选）
  - [x] 在工具定义中添加权限字段（可选）
  - [x] 实现权限检查接口（`checkPermission(const std::string& toolName, PermissionLevel level)`，可选）

**验收标准**：
- 单测验证：工具注册后能正确查询。
- 单测验证：重复注册时行为正确（覆盖或拒绝）。
- 单测验证：线程安全（并发注册/查询无竞态条件）。

#### 5.1.3.3 工具查询
- [x] **按名称查询工具**
  - [x] 实现 `getTool(const std::string& toolName)` 方法
    - [x] 在映射中查找工具
    - [x] 返回工具定义的引用或可选值（`std::optional<ToolDefinition>`）
    - [x] 处理工具不存在的情况
  - [x] 实现 `hasTool(const std::string& toolName)` 方法
    - [x] 检查工具是否存在
    - [x] 返回布尔值

- [x] **列出所有工具**
  - [x] 实现 `getAllTools()` 方法
    - [x] 返回所有已注册工具的列表（`std::vector<ToolDefinition>`）
  - [x] 实现 `getToolNames()` 方法
    - [x] 返回所有工具名称列表（`std::vector<std::string>`）
  - [x] 实现工具过滤（按名称前缀、按权限等，可选）

- [x] **工具查询统计**
  - [x] 实现工具使用统计（调用次数、最后调用时间等，可选）
  - [x] 实现工具性能统计（平均执行时间、错误率等，可选）

**验收标准**：
- 单测验证：工具查询接口返回正确结果。
- 单测验证：列表接口返回所有已注册工具。

#### 5.1.3.4 工具执行
- [x] **参数验证**
  - [x] 实现 `validateArguments(const ToolDefinition& tool, const nlohmann::json& arguments)` 方法
    - [x] 检查必需字段是否存在
    - [x] 检查字段类型是否正确（使用JSON Schema验证）
    - [x] 检查字段值是否在允许范围内（enum、range等）
    - [x] 返回验证结果（成功/失败 + 错误信息）
  - [x] 在工具执行前自动进行参数验证
  - [x] 验证失败时返回清晰的错误信息

- [x] **工具调用**
  - [x] 实现 `executeTool(const std::string& toolName, const nlohmann::json& arguments)` 方法
    - [x] 查找工具定义
    - [x] 验证参数
    - [x] 调用工具处理器（`tool.handler(arguments)`）
    - [x] 捕获处理器异常
    - [x] 返回执行结果（`nlohmann::json`）
  - [x] 实现异常处理
    - [x] 捕获 `std::exception` 异常
    - [x] 返回错误信息（使用 `ErrorInfo` 结构）
    - [x] 记录错误日志（可选）

- [x] **结果返回**
  - [x] 工具执行结果统一为 JSON 格式
  - [x] 支持任意JSON结构（对象、数组、基本类型）
  - [x] 实现结果序列化（`nlohmann::json::dump()`）

- [x] **错误处理**
  - [x] 定义工具执行错误类型（通过 `ErrorType` 枚举：`InvalidRequest`、`ServerError` 等）
  - [x] 实现错误分类和错误信息返回
  - [x] 集成 `ErrorHandler` 进行统一错误处理（可选）

- [x] **工具执行统计（可选）**
  - [x] 记录工具调用次数
  - [x] 记录工具执行时间
  - [x] 记录工具执行错误次数

**验收标准**：
- 单测验证：参数验证正确（必需字段、类型检查）。
- 单测验证：工具执行成功时返回正确结果。
- 单测验证：工具执行失败时返回正确错误信息。
- 单测验证：参数验证失败时返回清晰错误信息。

---

## 5.2 代码工具集（CodeTools）

### 5.2.1 任务概述
实现代码开发相关的标准工具，包括文件读取、文件列表、代码搜索、项目结构分析和代码分析等。这些工具为LLM提供代码操作能力，是代码开发辅助功能的基础。

### 5.2.2 文件结构（建议）
```
include/naw/desktop_pet/service/
└── CodeTools.h

src/naw/desktop_pet/service/
└── CodeTools.cpp
```

### 5.2.3 详细任务清单

#### 5.2.3.1 read_file工具实现
- [x] **文件读取基础功能**
  - [x] 实现文件读取逻辑（使用 `std::ifstream` 或平台API）
  - [x] 支持文本文件读取（UTF-8编码）
  - [x] 处理文件不存在错误
  - [x] 处理文件读取权限错误
  - [x] 处理文件过大错误（设置最大文件大小限制）

- [x] **行范围支持**
  - [x] 实现行范围参数解析（`startLine`、`endLine`）
  - [x] 实现行范围提取逻辑
    - [x] 支持单行提取（`startLine == endLine`）
    - [x] 支持多行提取（`startLine < endLine`）
    - [x] 处理边界情况（超出文件范围、负数等）
  - [x] 返回行范围内容（包含行号信息，可选）

- [x] **错误处理**
  - [x] 文件不存在时返回错误信息
  - [x] 文件读取失败时返回错误信息
  - [x] 行范围无效时返回错误信息
  - [x] 返回标准错误格式（JSON格式）

- [x] **返回结果格式**
  - [x] 定义返回JSON结构
    - [x] `content`（文件内容或行范围内容）
    - [x] `path`（文件路径）
    - [x] `line_count`（总行数）
    - [x] `start_line`（起始行，如果指定了行范围）
    - [x] `end_line`（结束行，如果指定了行范围）

- [x] **工具注册**
  - [x] 实现 `registerReadFileTool(ToolManager& toolManager)` 静态方法
  - [x] 定义工具参数Schema
    - [x] `path`（必需，文件路径）
    - [x] `start_line`（可选，起始行号，从1开始）
    - [x] `end_line`（可选，结束行号）
  - [x] 注册到 `ToolManager`

**验收标准**：
- 单测验证：读取完整文件成功。
- 单测验证：读取指定行范围成功。
- 单测验证：文件不存在时返回错误。
- 单测验证：无效行范围时返回错误。

#### 5.2.3.2 list_files工具实现
- [x] **目录遍历**
  - [x] 实现目录遍历逻辑（使用 `std::filesystem`）
  - [x] 支持相对路径和绝对路径
  - [x] 处理目录不存在错误
  - [x] 处理目录访问权限错误

- [x] **文件过滤**
  - [x] 实现文件模式匹配（如 `*.cpp`、`*.h`）
  - [x] 支持通配符匹配（`*`、`?` 等）
  - [x] 支持正则表达式匹配（可选）
  - [x] 支持文件类型过滤（通过扩展名）

- [x] **递归选项**
  - [x] 实现递归遍历参数（`recursive` 布尔值）
  - [x] 支持单层遍历（非递归）
  - [x] 支持递归遍历（遍历所有子目录）
  - [x] 处理循环链接（避免无限递归）

- [x] **返回结果格式**
  - [x] 定义返回JSON结构
    - [x] `files`（文件路径列表）
    - [x] `directories`（目录路径列表，可选）
    - [x] `count`（文件数量）
    - [x] `total_size`（总大小，字节，可选）

- [x] **工具注册**
  - [x] 实现 `registerListFilesTool(ToolManager& toolManager)` 静态方法
  - [x] 定义工具参数Schema
    - [x] `directory`（可选，目录路径，默认为当前目录）
    - [x] `pattern`（可选，文件匹配模式，如 `*.cpp`）
    - [x] `recursive`（可选，是否递归，默认false）
  - [x] 注册到 `ToolManager`

**验收标准**：
- 单测验证：列出目录文件成功。
- 单测验证：文件过滤正确（模式匹配）。
- 单测验证：递归遍历正确。
- 单测验证：目录不存在时返回错误。

#### 5.2.3.3 search_code工具实现
- [x] **文本搜索**
  - [x] 实现文本搜索逻辑（逐文件搜索）
  - [x] 支持精确匹配搜索
  - [x] 支持大小写敏感/不敏感搜索（`case_sensitive` 参数）
  - [x] 支持多文件搜索（遍历目录）

- [x] **正则表达式支持**
  - [x] 实现正则表达式搜索（使用 `std::regex` 或第三方库）
  - [x] 支持标准正则表达式语法
  - [x] 处理正则表达式编译错误（无效正则）
  - [x] 提供正则表达式和文本搜索的统一接口

- [x] **文件类型过滤**
  - [x] 支持按文件扩展名过滤（`file_pattern` 参数）
  - [x] 支持多种文件类型（如 `*.cpp,*.h`）
  - [x] 在搜索前先进行文件过滤（性能优化）

- [x] **返回结果格式**
  - [x] 定义返回JSON结构
    - [x] `matches`（匹配结果列表）
      - [x] `file`（文件路径）
      - [x] `line`（行号）
      - [x] `column`（列号，可选）
      - [x] `context`（匹配行上下文，可选）
    - [x] `total_matches`（总匹配数）
    - [x] `files_searched`（搜索的文件数）

- [x] **工具注册**
  - [x] 实现 `registerSearchCodeTool(ToolManager& toolManager)` 静态方法
  - [x] 定义工具参数Schema
    - [x] `query`（必需，搜索查询文本或正则表达式）
    - [x] `directory`（可选，搜索目录，默认为当前目录）
    - [x] `file_pattern`（可选，文件类型过滤，如 `*.cpp`）
    - [x] `case_sensitive`（可选，是否区分大小写，默认false）
  - [x] 注册到 `ToolManager`

**验收标准**：
- 单测验证：文本搜索正确（精确匹配）。
- 单测验证：正则表达式搜索正确。
- 单测验证：文件类型过滤正确。
- 单测验证：大小写敏感/不敏感搜索正确。

#### 5.2.3.4 get_project_structure工具实现
- [x] **项目结构分析**
  - [x] 实现项目根目录识别（查找 `.git`、`CMakeLists.txt` 等标识）
  - [x] 实现目录结构扫描（递归遍历）
  - [x] 识别项目类型（C++、Python等，通过配置文件）
  - [x] 提取目录树结构

- [x] **CMake解析**
  - [x] 实现 `CMakeLists.txt` 文件解析
    - [x] 解析项目名称（`project()`）
    - [x] 解析源文件列表（`add_executable()`、`add_library()`）
    - [x] 解析依赖关系（`target_link_libraries()`、`find_package()`）
    - [x] 解析编译选项（`target_compile_options()`）
  - [x] 处理CMake解析错误（语法错误、文件不存在等）
  - [x] 支持简单的CMake语法（不完全解析，提取关键信息）

- [x] **依赖关系提取**
  - [x] 从CMake配置提取依赖库
  - [x] 从源码提取 `#include` 依赖（可选）
  - [x] 构建依赖关系图（可选）
  - [x] 返回依赖列表

- [x] **返回结果格式**
  - [x] 定义返回JSON结构
    - [x] `project_name`（项目名称）
    - [x] `root_path`（项目根路径）
    - [x] `structure`（目录结构树）
    - [x] `source_files`（源文件列表）
    - [x] `header_files`（头文件列表）
    - [x] `cmake_config`（CMake配置信息，可选）
    - [x] `dependencies`（依赖列表，可选）

- [x] **工具注册**
  - [x] 实现 `registerGetProjectStructureTool(ToolManager& toolManager)` 静态方法
  - [x] 定义工具参数Schema
    - [x] `include_files`（可选，是否包含文件列表，默认true）
    - [x] `include_dependencies`（可选，是否包含依赖关系，默认true）
    - [x] `project_root`（可选，项目根路径，默认自动检测）
  - [x] 注册到 `ToolManager`

**验收标准**：
- 单测验证：项目结构分析正确（目录树）。
- 单测验证：CMake解析正确（项目名、源文件、依赖）。
- 单测验证：依赖关系提取正确。
- 单测验证：项目根目录自动识别正确。

#### 5.2.3.5 analyze_code工具实现
- [x] **代码解析**
  - [x] 实现代码文件解析（C++、Python等，根据文件扩展名）
  - [x] 使用简单解析器或正则表达式（不依赖完整编译器）
  - [x] 提取代码结构信息

- [x] **函数/类提取**
  - [x] C++：提取函数定义（函数名、参数、返回类型）
  - [x] C++：提取类定义（类名、成员变量、成员函数）
  - [x] Python：提取函数定义和类定义
  - [x] 支持命名空间/模块（C++ `namespace`、Python `module`）
  - [x] 返回结构化信息（JSON格式）

- [x] **依赖分析**
  - [x] 提取 `#include` 语句（C++）
  - [x] 提取 `import` 语句（Python）
  - [x] 分析函数调用关系（可选，复杂）
  - [x] 分析类继承关系（可选，复杂）

- [x] **返回结果格式**
  - [x] 定义返回JSON结构
    - [x] `path`（文件路径）
    - [x] `language`（语言类型）
    - [x] `functions`（函数列表）
      - [x] `name`（函数名）
      - [x] `signature`（函数签名）
      - [x] `line`（定义行号）
    - [x] `classes`（类列表）
      - [x] `name`（类名）
      - [x] `line`（定义行号）
      - [x] `methods`（方法列表）
    - [x] `includes`（依赖列表，可选）

- [x] **工具注册**
  - [x] 实现 `registerAnalyzeCodeTool(ToolManager& toolManager)` 静态方法
  - [x] 定义工具参数Schema
    - [x] `path`（必需，代码文件路径）
    - [x] `analysis_type`（可选，分析类型：`functions`、`classes`、`dependencies`、`all`，默认`all`）
  - [x] 注册到 `ToolManager`

**验收标准**：
- 单测验证：函数提取正确（C++/Python）。
- 单测验证：类提取正确（C++/Python）。
- 单测验证：依赖分析正确（include/import）。
- 单测验证：代码解析错误处理正确（无效语法等）。

#### 5.2.3.6 write_file工具实现
- [x] **文件写入基础功能**
  - [x] 实现文件写入逻辑（使用 `std::ofstream` 或平台API）
  - [x] 支持文本文件写入（UTF-8编码）
  - [x] 处理文件路径不存在错误（创建目录，可选）
  - [x] 处理文件写入权限错误
  - [x] 处理磁盘空间不足错误
  - [x] 支持文件覆盖模式和追加模式

- [x] **写入模式支持**
  - [x] 实现写入模式参数（`mode`：`"overwrite"`、`"append"`、`"create_only"`等）
  - [x] 支持覆盖模式（`overwrite`：如果文件存在则覆盖）
  - [x] 支持追加模式（`append`：在文件末尾追加内容）
  - [x] 支持创建模式（`create_only`：仅当文件不存在时创建）
  - [x] 处理模式冲突（文件已存在但使用 `create_only` 模式）

- [x] **行范围写入支持（可选）**
  - [x] 实现行范围参数解析（`startLine`、`endLine`）
  - [x] 实现行范围替换逻辑
    - [x] 支持单行替换（`startLine == endLine`）
    - [x] 支持多行替换（`startLine < endLine`）
    - [x] 处理边界情况（超出文件范围、负数等）
  - [x] 读取原文件内容，替换指定行范围，写回文件

- [x] **内容格式处理**
  - [x] 支持字符串内容直接写入
  - [x] 支持多行文本写入（保持换行符）
  - [x] 处理不同平台换行符差异（Windows `\r\n`、Unix `\n`）
  - [x] 支持JSON格式内容写入（可选，格式化JSON）

- [x] **错误处理**
  - [x] 文件路径无效时返回错误信息
  - [x] 文件写入失败时返回错误信息
  - [x] 权限不足时返回错误信息
  - [x] 磁盘空间不足时返回错误信息
  - [x] 返回标准错误格式（JSON格式）

- [x] **返回结果格式**
  - [x] 定义返回JSON结构
    - [x] `success`（是否成功，布尔值）
    - [x] `path`（文件路径）
    - [x] `bytes_written`（写入的字节数）
    - [x] `mode`（使用的写入模式）
    - [x] `message`（成功/错误消息）
    - [x] `error`（错误信息，如果写入失败）

- [x] **工具注册**
  - [x] 实现 `registerWriteFileTool(ToolManager& toolManager)` 静态方法
  - [x] 定义工具参数Schema
    - [x] `path`（必需，文件路径）
    - [x] `content`（必需，要写入的内容，字符串）
    - [x] `mode`（可选，写入模式：`"overwrite"`、`"append"`、`"create_only"`，默认`"overwrite"`）
    - [x] `start_line`（可选，起始行号，用于行范围替换，从1开始）
    - [x] `end_line`（可选，结束行号，用于行范围替换）
    - [x] `create_directories`（可选，是否自动创建目录，默认false）
  - [x] 注册到 `ToolManager`

**验收标准**：
- 单测验证：写入新文件成功。
- 单测验证：覆盖现有文件成功。
- 单测验证：追加内容到文件成功。
- 单测验证：行范围替换成功。
- 单测验证：文件路径无效时返回错误。
- 单测验证：权限不足时返回错误。
- 单测验证：创建目录选项正确工作。

- [x] **统一工具注册接口**
  - [x] 实现 `CodeTools::registerAllTools(ToolManager& toolManager)` 静态方法
    - [x] 调用所有工具的注册方法（包括 `registerReadFileTool`、`registerWriteFileTool`、`registerListFilesTool`、`registerSearchCodeTool`、`registerGetProjectStructureTool`、`registerAnalyzeCodeTool`）
    - [x] 一次性注册所有代码工具

---

## 5.3 Function Calling处理器（FunctionCallingHandler）

### 5.3.1 任务概述
处理LLM返回的工具调用请求，执行工具并构建包含工具结果的后续请求。这是连接LLM和工具管理器的桥梁，实现完整的Function Calling流程。

### 5.3.2 文件结构（建议）
```
include/naw/desktop_pet/service/
└── FunctionCallingHandler.h

src/naw/desktop_pet/service/
└── FunctionCallingHandler.cpp
```

### 5.3.3 详细任务清单

#### 5.3.3.1 工具调用检测
- [x] **响应中工具调用识别**
  - [x] 实现 `hasToolCalls(const ChatResponse& response)` 方法
    - [x] 检查 `response.toolCalls` 是否非空
    - [x] 检查 `response.finishReason` 是否为 `"tool_calls"`
    - [x] 返回布尔值
  - [x] 实现 `extractToolCalls(const ChatResponse& response)` 方法
    - [x] 从响应中提取 `toolCalls` 列表
    - [x] 返回 `std::vector<ToolCall>` 或 `std::vector<types::ToolCall>`
    - [x] 处理空列表情况

- [x] **工具调用参数提取**
  - [x] 实现 `parseToolCallArguments(const ToolCall& toolCall)` 方法
    - [x] 从 `toolCall.function.arguments` 提取参数（JSON字符串或JSON对象）
    - [x] 解析JSON参数（`nlohmann::json::parse()`）
    - [x] 处理JSON解析错误
    - [x] 返回 `nlohmann::json` 对象
    - [x] 支持将 `null` 类型视为空对象（增强健壮性）
  - [x] 实现参数验证（使用 `ToolManager::validateArguments()`）

- [x] **工具调用结构验证**
  - [x] 实现 `validateToolCall(const ToolCall& toolCall, ToolManager& toolManager)` 方法
    - [x] 验证工具调用ID是否存在（`toolCall.id`）
    - [x] 验证工具名称是否存在（`toolCall.function.name`）
    - [x] 验证参数格式（JSON格式）
    - [x] 验证工具是否在 ToolManager 中注册
    - [x] 返回验证结果

**验收标准**：
- [x] 单测验证：工具调用识别正确（有/无工具调用）。
- [x] 单测验证：工具调用参数提取正确（JSON解析）。
- [x] 单测验证：参数验证正确（无效参数时返回错误）。

#### 5.3.3.2 工具调用执行流程
- [x] **批量工具调用处理**
  - [x] 定义 `FunctionCallResult` 结构体
    - [x] `toolCallId`（工具调用ID）
    - [x] `toolName`（工具名称）
    - [x] `result`（执行结果，`nlohmann::json`）
    - [x] `error`（错误信息，如果执行失败）
    - [x] `executionTimeMs`（执行时间，毫秒）
    - [x] `success`（是否成功）
    - [x] `toJson()` 方法（序列化支持）
  - [x] 实现 `executeToolCalls(const std::vector<ToolCall>& toolCalls, ToolManager& toolManager)` 方法
    - [x] 遍历所有工具调用
    - [x] 对每个工具调用执行以下步骤：
      - [x] 记录开始时间
      - [x] 提取工具名称和参数（使用 `parseToolCallArguments`）
      - [x] 验证工具调用（使用 `validateToolCall`）
      - [x] 调用 `ToolManager::executeTool()` 执行工具
      - [x] 捕获执行异常（`std::exception` 和未知异常）
      - [x] 计算执行时间
      - [x] 创建 `FunctionCallResult` 对象
      - [x] 添加到结果列表
    - [x] 返回 `std::vector<FunctionCallResult>`

- [x] **结果收集**
  - [x] 实现结果聚合逻辑
    - [x] 收集所有成功的结果
    - [x] 收集所有失败的结果（错误信息）
  - [x] 处理部分失败情况（部分工具成功、部分失败）
  - [x] 每个结果包含完整的执行信息（成功/失败、结果/错误、执行时间）

- [x] **并发执行（可选）**
  - [x] 支持并发执行多个工具调用（使用 `std::async` 或线程池）
  - [x] 实现并发控制（最大并发数限制）
  - [x] 处理并发执行的异常和错误

- [x] **执行超时控制（可选）**
  - [x] 为每个工具调用设置超时时间
  - [x] 超时后取消工具执行（如果支持）
  - [x] 返回超时错误信息

**验收标准**：
- [x] 单测验证：批量工具调用执行正确（所有工具成功）。
- [x] 单测验证：部分失败处理正确（部分工具失败）。
- [x] 单测验证：执行结果收集正确。
- [x] 单测验证：执行时间记录正确。
- [x] 单测验证：并发执行正确（支持并发限制）。
- [x] 单测验证：超时控制正确（超时后返回错误）。

#### 5.3.3.3 后续请求构建
- [x] **工具结果消息构建**
  - [x] 实现 `buildToolResultMessages(const std::vector<FunctionCallResult>& results)` 方法
    - [x] 遍历所有工具执行结果
    - [x] 对每个结果创建 `ChatMessage` 对象
      - [x] 设置 `role = MessageRole::Tool`
      - [x] 设置 `name = result.toolName`
      - [x] 设置 `toolCallId = result.toolCallId`
      - [x] 设置 `content`：
        - [x] 成功时：`result.result.value().dump()`（JSON字符串）
        - [x] 失败时：`"Error: " + result.error.value()`
    - [x] 返回 `std::vector<ChatMessage>`
  - [x] 消息格式符合 API 要求（使用 `ChatMessage` 结构）

- [x] **多轮对话支持**
  - [x] 实现 `buildFollowUpRequest(const std::vector<ChatMessage>& originalMessages, const std::vector<ChatMessage>& toolResults, const ChatRequest& originalRequest)` 方法
    - [x] 合并原始消息和工具结果消息
    - [x] 保持消息顺序（原始消息在前，工具结果在后）
    - [x] 创建新的 `ChatRequest` 对象
    - [x] 设置请求参数（从原始请求继承）
    - [x] 返回 `ChatRequest`
  - [x] 支持多轮工具调用（工具调用 -> 工具结果 -> 再次工具调用）
  - [x] 实现完整流程方法 `processToolCalls`（整合检测、执行、构建）

- [x] **请求参数继承**
  - [x] 从原始请求继承模型ID（`request.model`）
  - [x] 从原始请求继承温度等参数（`temperature`、`maxTokens` 等）
  - [x] 从原始请求继承工具列表（`tools`）
  - [x] 从原始请求继承其他参数（`stop`、`topP`、`topK`、`stream`、`toolChoice` 等）

- [x] **工具调用上下文管理（可选）**
  - [x] 记录工具调用历史（用于调试和统计）
  - [x] 实现工具调用链追踪（哪次对话触发了哪些工具调用）
  - [x] 实现工具调用结果缓存（相同工具调用参数时复用结果，可选）

**验收标准**：
- [x] 单测验证：工具结果消息构建正确（格式、内容）。
- [x] 单测验证：后续请求构建正确（消息合并、参数继承）。
- [x] 单测验证：多轮工具调用支持正确（多轮对话流程）。
- [x] 单测验证：完整流程方法 `processToolCalls` 正确工作。
- [x] 单测验证：工具调用上下文管理正确（历史记录、调用链追踪、结果缓存）。

---

## 5.4 工具与LLM集成（Tool-LLM Integration）

### 5.4.1 任务概述
实现ToolManager中的工具与LLM的集成，将工具定义转换为OpenAI兼容的Function Calling格式，供硅基流动等LLM服务使用。本模块负责工具格式转换和ChatRequest构建，使LLM能够识别和调用ToolManager中注册的工具。

> **注意**：根据硅基流动官方文档（https://docs.siliconflow.cn/cn/userguide/guides/function-calling），
> 硅基流动支持OpenAI兼容的Function Calling格式，但不支持MCP协议。
> 因此本模块直接实现Function Calling格式转换，而非MCP Server。

### 5.4.2 文件结构（建议）
```
include/naw/desktop_pet/service/
└── ToolManager.h  (添加 getToolsForAPI() 方法)

src/naw/desktop_pet/service/
└── ToolManager.cpp  (实现 getToolsForAPI() 方法)
```

> **说明**：工具与LLM集成功能主要在ToolManager中实现，无需单独的MCPService模块。

### 5.4.3 详细任务清单

#### 5.4.3.1 工具格式转换（ToolManager扩展）
- [x] **ToolDefinition转OpenAI Function Calling格式**
  - [x] 在 `ToolManager` 中实现 `getToolsForAPI()` 方法
    - [x] 获取所有已注册的工具（`getAllTools()`）
    - [x] 将每个 `ToolDefinition` 转换为OpenAI Function Calling格式：
      - [x] 创建 `{"type": "function", "function": {...}}` 对象
      - [x] 设置 `function.name = tool.name`
      - [x] 设置 `function.description = tool.description`
      - [x] 设置 `function.parameters = tool.parametersSchema`
    - [x] 返回 `std::vector<nlohmann::json>`（工具列表）
  - [x] 确保格式符合OpenAI Function Calling规范（参考硅基流动文档）
  - [x] 线程安全（使用mutex保护）

**验收标准**：
- [x] 单测验证：工具格式转换正确（符合OpenAI格式）。
- [x] 单测验证：所有工具都能正确转换。
- [x] 单测验证：线程安全（并发调用无竞态条件）。

#### 5.4.3.2 ChatRequest工具填充
- [x] **构建包含工具的ChatRequest**
  - [x] 实现辅助函数或方法，将ToolManager的工具填充到ChatRequest
    - [x] 调用 `ToolManager::getToolsForAPI()` 获取工具列表
    - [x] 设置 `ChatRequest.tools = toolList`
    - [x] 设置 `ChatRequest.toolChoice = "auto"`（或从配置读取）
    - [x] 保持其他请求参数不变
  - [x] 实现工具选择策略（`auto`、`none`、特定工具名）
    - [x] `auto`：让LLM自动决定是否调用工具
    - [x] `none`：不调用任何工具
    - [x] 特定工具名：强制调用指定工具

- [x] **工具过滤（可选）**
  - [x] 实现按权限级别过滤工具（只暴露Public工具给LLM）
  - [x] 实现按工具名称前缀过滤
  - [x] 实现自定义工具过滤函数（通过ToolFilter结构）

- [x] **工具列表管理**
  - [x] 支持动态添加/移除工具到请求中（通过populateToolsToRequest方法）
  - [x] 支持工具列表缓存（通过getToolsForAPI方法，每次调用时动态生成）

**验收标准**：
- [x] 单测验证：ChatRequest工具列表填充正确。
- [x] 单测验证：工具选择策略正确（auto/none/特定工具）。
- [x] 单测验证：工具过滤功能正确（如果实现）。

#### 5.4.3.3 工具调用流程集成
- [x] **完整的Function Calling流程**
  - [x] 在ContextManager或RequestManager中集成工具列表填充
    - [x] 在ContextManager中实现 `populateToolsToRequest()` 方法
    - [x] 构建ChatRequest时，可以从ToolManager获取工具列表
    - [x] 将工具列表填充到 `request.tools` 字段
  - [x] 确保工具列表在每次请求中都正确传递
  - [x] 支持工具列表的动态更新（工具注册/注销后，下次调用getToolsForAPI时自动更新）

- [x] **工具调用结果处理**
  - [x] 使用已有的 `FunctionCallingHandler::processToolCalls()` 处理工具调用
  - [x] 确保工具调用结果正确返回给LLM
  - [x] 支持多轮工具调用（工具调用 -> 工具结果 -> 再次工具调用）
  - [x] 验证FunctionCallingHandler中的工具列表继承逻辑（buildFollowUpRequest正确继承tools和toolChoice）

- [x] **错误处理和日志**
  - [x] 实现工具列表填充错误处理（返回ErrorInfo）
  - [x] 工具调用统计功能已在ToolManager中实现（ToolUsageStats）
  - [x] 处理工具格式转换错误（参数验证、工具不存在等）

**验收标准**：
- [x] 单测验证：工具列表自动填充到ChatRequest。
- [x] 单测验证：完整的Function Calling流程正确（请求 -> LLM响应 -> 工具执行 -> 后续请求）。
- [x] 单测验证：多轮工具调用流程正确。

#### 5.4.3.4 使用示例和文档
- [x] **使用示例代码**
  - [x] 提供完整的Function Calling使用示例
    - [x] 注册工具到ToolManager
    - [x] 构建包含工具的ChatRequest
    - [x] 发送请求到LLM（硅基流动）
    - [x] 处理LLM返回的工具调用
    - [x] 执行工具并构建后续请求
    - [x] 多轮工具调用流程示例
  - [x] 示例代码应包含错误处理

- [x] **文档说明**
  - [x] 说明如何将ToolManager的工具用于LLM Function Calling
  - [x] 说明硅基流动Function Calling格式要求
  - [x] 提供工具定义的最佳实践（描述、参数Schema等）
  - [x] 工具选择策略说明
  - [x] 工具过滤机制说明
  - [x] 架构设计说明

**验收标准**：
- [x] 示例代码可以正常运行。
- [x] 文档清晰易懂。

---

## 5.5 项目上下文收集器（ProjectContextCollector）

### 5.5.1 任务概述
分析项目结构，收集项目信息（目录结构、CMake配置、依赖关系等），为LLM提供项目上下文。这个模块为 `ContextManager` 提供项目上下文数据，帮助LLM理解项目结构和代码关系。

### 5.5.2 文件结构（建议）
```
include/naw/desktop_pet/service/
└── ProjectContextCollector.h

src/naw/desktop_pet/service/
└── ProjectContextCollector.cpp
```

### 5.5.3 详细任务清单

#### 5.5.3.1 项目结构分析
- [x] **目录扫描**
  - [x] 实现目录扫描逻辑（使用 `std::filesystem`）
  - [x] 支持递归遍历（遍历所有子目录）
  - [x] 识别项目根目录（查找 `.git`、`CMakeLists.txt`、`.project` 等标识）
  - [x] 实现项目根目录自动检测（`detectProjectRoot(const std::string& startPath)`）

- [x] **文件类型识别**
  - [x] 实现文件类型识别（通过扩展名）
    - [x] C++源文件（`.cpp`、`.cc`、`.cxx`）
    - [x] C++头文件（`.h`、`.hpp`、`.hxx`）
    - [x] Python文件（`.py`）
    - [x] CMake文件（`CMakeLists.txt`、`.cmake`）
    - [x] 配置文件（`.json`、`.yaml`、`.toml` 等）
  - [x] 分类存储文件列表（按类型分组）

- [x] **CMakeLists.txt解析**
  - [x] 实现CMake文件解析逻辑
    - [x] 解析项目名称（`project(NAME)`）
    - [x] 解析源文件列表（`add_executable()`、`add_library()`）
    - [x] 解析依赖库（`target_link_libraries()`）
    - [x] 解析编译选项（`target_compile_options()`、`target_compile_definitions()`）
    - [x] 解析包含目录（`target_include_directories()`）
  - [x] 处理CMake语法（使用简单解析或正则表达式，不完全解析）
  - [x] 处理嵌套CMakeLists.txt（递归解析子目录）
  - [x] 返回CMake配置JSON结构

- [x] **项目信息结构**
  - [x] 定义 `ProjectInfo` 结构体
    - [x] `name`（项目名称）
    - [x] `rootPath`（项目根路径）
    - [x] `sourceFiles`（源文件列表）
    - [x] `headerFiles`（头文件列表）
    - [x] `cmakeConfig`（CMake配置，JSON格式）
    - [x] `dependencies`（依赖列表）
    - [x] `directoryStructure`（目录结构树，可选）
    - [x] `fileContents`（文件内容缓存，可选）

**验收标准**：
- 单测验证：目录扫描正确（递归遍历、文件分类）。
- 单测验证：项目根目录检测正确。
- 单测验证：CMake解析正确（项目名、源文件、依赖）。

#### 5.5.3.2 依赖关系提取
- [x] **从CMake提取依赖**
  - [x] 实现 `extractDependenciesFromCMake(const nlohmann::json& cmakeConfig)` 方法
    - [x] 从CMake配置中提取 `target_link_libraries()` 中的库名
    - [x] 从CMake配置中提取 `find_package()` 中的包名
    - [x] 返回依赖列表（`std::vector<std::string>`）
  - [x] 处理依赖名称规范化（去除版本号、路径等）

- [x] **从源码提取依赖（可选）**
  - [x] 实现 `extractIncludesFromSource(const std::string& filePath)` 方法
    - [x] 解析 `#include` 语句（C++）
    - [x] 解析 `import` 语句（Python）
    - [x] 提取包含的文件路径
    - [x] 区分系统头文件和项目内文件
  - [ ] 实现依赖图构建（文件之间的依赖关系图，可选）

- [ ] **依赖关系分析**
  - [ ] 实现依赖关系可视化（JSON格式的依赖树，可选）
  - [ ] 实现循环依赖检测（可选）
  - [ ] 实现依赖层次分析（核心依赖、可选依赖等，可选）

**验收标准**：
- 单测验证：从CMake提取依赖正确。
- 单测验证：从源码提取include正确（C++）。
- 单测验证：依赖关系分析正确（如果实现）。

#### 5.5.3.3 文件上下文收集
- [x] **相关文件查找**
  - [x] 实现 `findRelatedFiles(const std::string& filePath, const ProjectInfo& projectInfo)` 方法
    - [x] 分析文件的 `#include` 语句（C++）
    - [x] 查找被包含的文件（在项目内）
    - [x] 查找包含该文件的文件（反向依赖）
    - [x] 返回相关文件列表
  - [ ] 实现文件相关性评分（按依赖关系深度、直接/间接依赖等，可选）

- [x] **上下文范围确定**
  - [x] 实现 `getFileContext(const std::string& filePath, const ProjectInfo& projectInfo, int maxDepth = 1)` 方法
    - [x] 确定上下文范围（直接依赖、间接依赖等）
    - [x] 限制上下文大小（最大文件数、最大Token数等）
    - [x] 按相关性排序（相关文件在前）
  - [x] 实现上下文裁剪（超过限制时选择最重要的文件）

- [x] **文件内容收集**
  - [x] 实现文件内容读取（使用 `read_file` 工具或直接读取）
  - [x] 实现文件内容缓存（避免重复读取）
  - [x] 支持行范围读取（只读取相关部分，节省Token）
  - [x] 返回文件上下文字符串（格式化后的文件内容）

**验收标准**：
- 单测验证：相关文件查找正确（include关系）。
- 单测验证：上下文范围确定正确（深度限制、大小限制）。
- 单测验证：文件内容收集正确（缓存、格式化）。

#### 5.5.3.4 项目摘要生成
- [x] **项目摘要内容**
  - [x] 实现 `getProjectSummary(const ProjectInfo& projectInfo)` 方法
    - [x] 生成项目基本信息（名称、路径、语言类型等）
    - [x] 生成项目结构摘要（目录结构、文件数量等）
    - [x] 生成依赖摘要（主要依赖库列表）
    - [x] 生成构建配置摘要（CMake配置要点）
  - [x] 返回格式化字符串（Markdown或纯文本格式）

- [x] **摘要格式**
  - [x] 定义摘要模板（结构化格式）
    - [x] 项目名称和描述
    - [x] 目录结构（树形结构，简化版）
    - [x] 主要文件列表
    - [x] 依赖关系
    - [x] 构建配置
  - [x] 实现摘要长度控制（限制Token数，适合LLM处理）

- [x] **摘要缓存**
  - [x] 实现摘要缓存机制（项目结构变化时才重新生成）
  - [x] 使用文件修改时间判断是否需要更新
  - [x] 缓存摘要结果（内存缓存或文件缓存）

**验收标准**：
- 单测验证：项目摘要生成正确（包含所有关键信息）。
- 单测验证：摘要格式正确（结构化、可读）。
- 单测验证：摘要缓存机制正确（缓存命中、失效更新）。

---

## 5.6 单元测试与示例

### 5.6.1 单元测试
- [x] **ToolManager测试**
  - [x] 工具注册测试（注册、查询、移除）
  - [x] 工具执行测试（成功、失败、参数验证）
  - [x] 工具权限控制测试（如果实现）
  - [x] 线程安全测试（并发注册/执行）

- [x] **CodeTools测试**
  - [x] read_file工具测试（完整文件、行范围、错误处理）
  - [x] write_file工具测试（写入新文件、覆盖、追加、行范围替换、错误处理）
  - [x] list_files工具测试（目录遍历、过滤、递归）
  - [x] search_code工具测试（文本搜索、正则、文件过滤）
  - [x] get_project_structure工具测试（结构分析、CMake解析）
  - [x] analyze_code工具测试（函数提取、类提取、依赖分析）

- [x] **FunctionCallingHandler测试**
  - [x] 工具调用检测测试（识别、参数提取）
  - [x] 工具调用执行测试（批量执行、结果收集、执行时间记录）
  - [x] 后续请求构建测试（消息构建、参数继承）
  - [x] 多轮对话测试（多轮工具调用流程）
  - [x] 完整流程测试（`processToolCalls` 方法）
  - [x] 错误处理测试（工具不存在、参数验证失败、执行失败等）

- [x] **工具与LLM集成测试**
  - [x] 工具格式转换测试（ToolDefinition -> OpenAI格式）
  - [x] ChatRequest工具填充测试
  - [x] 完整Function Calling流程测试（请求 -> 工具执行 -> 后续请求）
  - [x] 硅基流动API兼容性测试

- [x] **ProjectContextCollector测试**
  - [x] 项目结构分析测试（目录扫描、CMake解析）
  - [x] 依赖关系提取测试（CMake依赖、源码依赖）
  - [x] 文件上下文收集测试（相关文件查找、上下文范围）
  - [x] 项目摘要生成测试（摘要内容、格式、缓存）

### 5.6.2 集成测试
- [ ] **ToolManager + CodeTools 集成测试**
  - [ ] 代码工具注册和执行流程
  - [ ] 工具错误处理集成

- [ ] **FunctionCallingHandler + ToolManager 集成测试**
  - [ ] 完整的Function Calling流程（检测 -> 执行 -> 构建后续请求）
  - [ ] 多轮工具调用流程
  - [ ] 工具调用错误处理

- [ ] **ToolManager + LLM集成测试**
  - [ ] 工具列表自动填充到ChatRequest
  - [ ] 工具调用完整流程（LLM请求 -> 工具执行 -> LLM响应）
  - [ ] 多轮工具调用流程

- [ ] **ProjectContextCollector + ContextManager 集成测试**
  - [ ] 项目上下文收集和集成到对话上下文
  - [ ] 上下文构建流程

- [ ] **完整工具调用流程集成测试**
  - [ ] APIClient -> FunctionCallingHandler -> ToolManager -> CodeTools
  - [ ] 端到端工具调用流程（LLM请求 -> 工具执行 -> LLM响应）
  - [ ] 多轮对话中的工具调用

---

## 开发顺序建议

### 第一阶段：工具管理器（5.1）
1. 先完成 `ToolManager`，这是所有工具的基础。
2. 实现工具注册、查询和执行功能。

### 第二阶段：代码工具集（5.2）
1. 完成 `CodeTools`，实现标准代码工具。
2. 逐个实现各个工具（read_file、write_file、list_files等）。
3. 集成到 `ToolManager`。

### 第三阶段：Function Calling处理器（5.3）
1. 完成 `FunctionCallingHandler`。
2. 集成 `ToolManager` 实现工具调用流程。
3. 实现后续请求构建。

### 第四阶段：工具与LLM集成（5.4）
1. 在 `ToolManager` 中实现 `getToolsForAPI()` 方法，将工具转换为OpenAI Function Calling格式。
2. 实现ChatRequest工具填充功能。
3. 集成到ContextManager或RequestManager，实现完整的Function Calling流程。

### 第五阶段：项目上下文收集器（5.5）
1. 完成 `ProjectContextCollector`。
2. 实现项目结构分析和依赖提取。
3. 集成到 `ContextManager`（可选，如果需要）。

### 第六阶段：集成和测试
1. 完成所有模块的集成测试。
2. 性能测试和优化。

---

## 进度追踪

### 5.1 工具管理器（ToolManager）
- [x] 工具定义结构
- [x] 工具注册机制
- [x] 工具查询
- [x] 工具执行
- [x] 单元测试
- [x] 可选功能（序列化/反序列化、权限控制、工具过滤、统计功能、参数验证增强、ErrorHandler集成）

**进度**: 5/5 主要模块完成 ✅（包含所有可选功能）

### 5.2 代码工具集（CodeTools）
- [x] read_file工具实现
- [x] write_file工具实现
- [x] list_files工具实现
- [x] search_code工具实现
- [x] get_project_structure工具实现
- [x] analyze_code工具实现
- [x] 统一工具注册接口
- [x] 单元测试

**进度**: 8/8 主要模块完成 ✅

### 5.3 Function Calling处理器（FunctionCallingHandler）
- [x] 工具调用检测
- [x] 工具调用执行流程（包含并发执行和超时控制可选功能）
- [x] 后续请求构建（包含工具调用上下文管理可选功能）
- [x] 单元测试（包含可选功能的测试）

**进度**: 4/4 主要模块完成 ✅（包含所有可选功能）

### 5.4 工具与LLM集成（Tool-LLM Integration）
- [x] 工具格式转换（ToolManager扩展 - getToolsForAPI方法）
- [x] ChatRequest工具填充
- [x] 工具调用流程集成
- [x] 使用示例和文档
- [x] 单元测试

**进度**: 4/4 主要模块完成 ✅

### 5.5 项目上下文收集器（ProjectContextCollector）
- [x] 项目结构分析
- [x] 依赖关系提取
- [x] 文件上下文收集
- [x] 项目摘要生成
- [x] 单元测试

**进度**: 5/5 主要模块完成 ✅

### 5.6 单元测试与示例
- [x] ToolManager测试
- [x] CodeTools测试
- [x] FunctionCallingHandler测试
- [x] 工具与LLM集成测试
- [x] ProjectContextCollector测试
- [ ] 集成测试

**进度**: 5/6 主要模块完成

---

## 总体进度

**Phase 5 总体进度**: 27/27 主要模块完成 ✅

**各模块完成情况**：
- 5.1 工具管理器（ToolManager）: 5/5 ✅
- 5.2 代码工具集（CodeTools）: 8/8 ✅
- 5.3 Function Calling处理器（FunctionCallingHandler）: 4/4 ✅
- 5.4 工具与LLM集成（Tool-LLM Integration）: 4/4 ✅
- 5.5 项目上下文收集器（ProjectContextCollector）: 5/5 ✅
- 5.6 单元测试与示例: 5/6

> **注意**：5.4节已从"MCP服务"调整为"工具与LLM集成"，直接实现OpenAI Function Calling格式转换，
> 适配硅基流动等支持OpenAI兼容Function Calling的LLM服务提供商。
