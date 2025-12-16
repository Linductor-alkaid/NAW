# Agent桌宠应用设计方案

## 1. 项目概述

### 1.1 项目背景
本项目旨在基于NAW系统的Agent框架，利用Render渲染引擎构建一个透明窗口的2D精灵桌宠应用。该桌宠集成硅基流动（SiliconFlow）大模型API，作为智能Agent的测试平台，同时提供代码开发辅助功能，通过MCP（Model Context Protocol）协议与开发环境交互。

### 1.2 核心目标
- **桌宠功能**：创建透明窗口的2D精灵桌宠，支持动画、交互和状态展示
- **AI Agent集成**：集成硅基流动API，实现基于大模型的智能对话和决策
- **开发辅助**：通过MCP协议提供代码开发相关的工具和上下文
- **NAW Agent测试**：作为NAW系统Agent框架的应用测试平台

### 1.3 技术栈
- **渲染引擎**：Render Engine (基于OpenGL 4.5+, SDL3)
- **AI API**：硅基流动 (SiliconFlow) OpenAI兼容API
- **协议**：MCP (Model Context Protocol)
- **语言**：C++20
- **构建系统**：CMake 3.15+

## 2. 系统架构

### 2.1 整体架构

```
┌─────────────────────────────────────────────────────────┐
│                    Agent桌宠应用                          │
├─────────────────────────────────────────────────────────┤
│                                                           │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ │
│  │  渲染层      │  │  Agent层      │  │  服务层      │ │
│  │              │  │              │  │              │ │
│  │ - 透明窗口   │  │ - NAW Agent  │  │ - AI API     │ │
│  │ - 2D精灵     │  │ - 状态管理   │  │ - MCP Server │ │
│  │ - 动画系统   │  │ - 记忆系统   │  │ - HTTP Client│ │
│  │ - UI系统     │  │ - 决策系统   │  │ - 事件总线   │ │
│  └──────────────┘  └──────────────┘  └──────────────┘ │
│                                                           │
└─────────────────────────────────────────────────────────┘
         │                    │                    │
         ▼                    ▼                    ▼
    Render Engine        NAW Agent          SiliconFlow API
                                         MCP Protocol
```

### 2.2 模块划分

#### 2.2.1 渲染模块 (Render Module)
- **职责**：窗口管理、2D渲染、精灵动画、UI显示
- **技术**：Render Engine (SDL3 + OpenGL)
- **特性**：
  - 透明窗口支持（Windows平台）
  - 2D精灵渲染和动画
  - 状态可视化UI
  - 交互响应（点击、拖拽等）

#### 2.2.2 Agent核心模块 (Agent Core Module)
- **职责**：Agent状态管理、决策逻辑、记忆系统
- **技术**：NAW Agent框架
- **特性**：
  - 集成NAW Agent数据结构
  - 状态持久化
  - 记忆事件管理
  - 性格和技能系统

#### 2.2.3 AI服务模块 (AI Service Module)
- **职责**：与硅基流动API通信、对话管理、上下文处理
- **技术**：HTTP客户端、JSON处理
- **特性**：
  - OpenAI兼容API调用
  - 对话历史管理
  - 流式响应处理
  - 错误重试机制

#### 2.2.4 MCP服务模块 (MCP Service Module)
- **职责**：MCP协议实现、开发工具集成、上下文提供
- **技术**：MCP协议、文件系统监控、代码分析
- **特性**：
  - MCP Server实现
  - 代码文件读取
  - 项目结构分析
  - 开发上下文收集

#### 2.2.5 事件系统模块 (Event System Module)
- **职责**：模块间通信、事件分发、状态同步
- **技术**：观察者模式、事件总线
- **特性**：
  - 异步事件处理
  - 事件订阅/发布
  - 状态变更通知

## 3. 核心功能设计

### 3.1 透明窗口桌宠

#### 3.1.1 窗口特性
- **透明背景**：使用Windows透明窗口API（SetLayeredWindowAttributes或DWM）
- **无边框**：去除窗口边框和标题栏
- **置顶显示**：可选的始终置顶模式
- **点击穿透**：非精灵区域支持点击穿透
- **拖拽移动**：支持拖拽窗口位置

#### 3.1.2 精灵系统
- **精灵渲染**：使用Render Engine的Sprite系统
- **动画支持**：
  - 待机动画（idle）
  - 思考动画（thinking）
  - 说话动画（talking）
  - 工作动画（working）
  - 交互动画（interacting）
- **状态切换**：根据Agent状态自动切换动画
- **资源管理**：精灵图片、动画帧序列管理

#### 3.1.3 UI系统
- **状态显示**：
  - Agent名称和状态
  - 当前任务/对话
  - 情绪指示器
  - 记忆事件数量
- **交互界面**：
  - 对话输入框
  - 快捷命令按钮
  - 设置菜单
- **主题支持**：支持Render Engine的UI主题系统

### 3.2 AI Agent集成

#### 3.2.1 硅基流动API集成
- **API配置**：
  - Base URL: `https://api.siliconflow.cn/v1`
  - API Key管理（配置文件或环境变量）
  - 模型选择（可配置）
- **对话管理**：
  - 对话历史维护
  - 上下文窗口管理
  - 角色设定（System Prompt）
- **响应处理**：
  - 流式响应支持
  - 文本解析和格式化
  - 错误处理和重试

#### 3.2.2 Agent决策系统
- **状态驱动**：基于NAW Agent的状态（PhysicalState, MentalState等）
- **决策流程**：
  1. 状态评估
  2. 上下文收集（记忆、当前任务）
  3. 调用AI API生成响应
  4. 更新Agent状态和记忆
- **个性化**：基于Agent的性格属性（Personality）调整对话风格

#### 3.2.3 记忆系统集成
- **事件记录**：
  - 用户交互事件
  - AI对话事件
  - 开发任务事件
- **记忆检索**：在生成响应时检索相关记忆
- **记忆持久化**：定期保存记忆到文件

### 3.3 MCP开发辅助功能

#### 3.3.1 MCP Server实现
- **协议支持**：实现MCP标准协议
- **工具注册**：
  - `read_file`: 读取代码文件
  - `list_files`: 列出项目文件
  - `search_code`: 代码搜索
  - `get_project_structure`: 获取项目结构
  - `analyze_code`: 代码分析
- **资源提供**：
  - 项目文件列表
  - 代码上下文
  - 构建信息

#### 3.3.2 开发上下文收集
- **项目分析**：
  - CMakeLists.txt解析
  - 源代码结构分析
  - 依赖关系提取
- **代码监控**：
  - 文件变更监控
  - 编译状态监控
  - 错误日志收集
- **上下文构建**：
  - 当前编辑文件
  - 相关文件引用
  - 项目配置信息

#### 3.3.3 开发辅助工具
- **代码建议**：基于项目上下文提供代码建议
- **问题诊断**：分析编译错误和运行时问题
- **文档生成**：辅助生成代码文档
- **重构建议**：提供代码重构建议

## 4. 技术实现方案

### 4.1 透明窗口实现

#### 4.1.1 Windows透明窗口
```cpp
// 伪代码示例
class TransparentWindow {
private:
    SDL_Window* m_window;
    SDL_Renderer* m_renderer;
    
public:
    void createTransparentWindow(int width, int height) {
        // 创建SDL窗口
        m_window = SDL_CreateWindow(
            "Agent Desktop Pet",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            width, height,
            SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP
        );
        
        // Windows特定：设置透明窗口
        #ifdef _WIN32
        HWND hwnd = (HWND)SDL_GetProperty(
            SDL_GetWindowProperties(m_window),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER,
            nullptr
        );
        
        // 设置窗口扩展样式
        LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        exStyle |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
        
        // 设置透明度
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        #endif
    }
};
```

#### 4.1.2 点击穿透处理
- 检测鼠标点击位置
- 判断是否在精灵或UI元素上
- 非交互区域设置点击穿透标志

### 4.2 精灵动画系统

#### 4.2.1 动画状态机
```cpp
enum class PetAnimationState {
    Idle,       // 待机
    Thinking,   // 思考
    Talking,    // 说话
    Working,    // 工作
    Interacting // 交互
};

class PetAnimationController {
private:
    PetAnimationState m_currentState;
    Render::SpriteAnimation* m_animations[5];
    
public:
    void update(float deltaTime) {
        // 根据Agent状态切换动画
        PetAnimationState targetState = getStateFromAgent();
        if (targetState != m_currentState) {
            switchAnimation(targetState);
        }
        
        // 更新当前动画
        m_animations[static_cast<int>(m_currentState)]->update(deltaTime);
    }
};
```

#### 4.2.2 动画资源
- 使用SpriteSheet或SpriteAtlas
- 支持帧序列动画
- 动画事件回调（用于同步音效等）

### 4.3 AI API集成

#### 4.3.1 HTTP客户端
- **库选择**：使用C++ HTTP客户端库（如libcurl或cpp-httplib）
- **请求封装**：
```cpp
class SiliconFlowClient {
private:
    std::string m_apiKey;
    std::string m_baseUrl;
    std::shared_ptr<HttpClient> m_httpClient;
    
public:
    struct ChatMessage {
        std::string role;    // "system", "user", "assistant"
        std::string content;
    };
    
    std::future<std::string> chat(
        const std::vector<ChatMessage>& messages,
        const std::string& model = "deepseek-ai/DeepSeek-V2.5"
    ) {
        nlohmann::json request = {
            {"model", model},
            {"messages", messages},
            {"temperature", 0.7},
            {"stream", false}
        };
        
        return m_httpClient->post(
            m_baseUrl + "/chat/completions",
            request.dump(),
            {
                {"Authorization", "Bearer " + m_apiKey},
                {"Content-Type", "application/json"}
            }
        ).then([](HttpResponse response) {
            auto json = nlohmann::json::parse(response.body);
            return json["choices"][0]["message"]["content"].get<std::string>();
        });
    }
};
```

#### 4.3.2 对话管理
```cpp
class ConversationManager {
private:
    std::vector<ChatMessage> m_history;
    size_t m_maxHistorySize;
    std::string m_systemPrompt;
    
public:
    void addMessage(const std::string& role, const std::string& content) {
        m_history.push_back({role, content});
        
        // 限制历史长度
        if (m_history.size() > m_maxHistorySize) {
            m_history.erase(m_history.begin());
        }
    }
    
    std::vector<ChatMessage> getContext() const {
        std::vector<ChatMessage> context;
        context.push_back({"system", m_systemPrompt});
        context.insert(context.end(), m_history.begin(), m_history.end());
        return context;
    }
};
```

### 4.4 MCP Server实现

#### 4.4.1 MCP协议基础
```cpp
// MCP消息结构
struct MCPMessage {
    std::string jsonrpc = "2.0";
    std::string id;
    std::string method;
    nlohmann::json params;
};

// MCP工具定义
struct MCPTool {
    std::string name;
    std::string description;
    nlohmann::json inputSchema;
    std::function<nlohmann::json(const nlohmann::json&)> handler;
};

class MCPServer {
private:
    std::vector<MCPTool> m_tools;
    std::string m_projectRoot;
    
public:
    void registerTool(const MCPTool& tool) {
        m_tools.push_back(tool);
    }
    
    nlohmann::json handleRequest(const MCPMessage& request) {
        if (request.method == "tools/list") {
            return listTools();
        } else if (request.method == "tools/call") {
            return callTool(request.params);
        }
        // ...
    }
    
    void initializeTools() {
        // 注册开发相关工具
        registerTool({
            "read_file",
            "读取代码文件内容",
            {/* schema */},
            [this](const nlohmann::json& params) {
                std::string path = params["path"];
                return readFile(m_projectRoot + "/" + path);
            }
        });
        
        registerTool({
            "list_files",
            "列出项目文件",
            {/* schema */},
            [this](const nlohmann::json& params) {
                return listProjectFiles(m_projectRoot);
            }
        });
        
        // 更多工具...
    }
};
```

#### 4.4.2 项目上下文收集
```cpp
class ProjectContextCollector {
private:
    std::string m_projectRoot;
    std::unordered_map<std::string, std::string> m_fileCache;
    
public:
    struct ProjectInfo {
        std::string name;
        std::vector<std::string> sourceFiles;
        std::vector<std::string> headerFiles;
        nlohmann::json cmakeConfig;
        std::vector<std::string> dependencies;
    };
    
    ProjectInfo analyzeProject() {
        ProjectInfo info;
        
        // 解析CMakeLists.txt
        info.cmakeConfig = parseCMakeLists(m_projectRoot + "/CMakeLists.txt");
        
        // 扫描源代码文件
        scanDirectory(m_projectRoot + "/src", info.sourceFiles);
        scanDirectory(m_projectRoot + "/include", info.headerFiles);
        
        // 提取依赖关系
        info.dependencies = extractDependencies(info.cmakeConfig);
        
        return info;
    }
    
    std::string getContextForFile(const std::string& filePath) {
        // 获取文件内容
        std::string content = readFile(filePath);
        
        // 获取相关文件（通过include等）
        std::vector<std::string> relatedFiles = findRelatedFiles(filePath);
        
        // 构建上下文
        nlohmann::json context = {
            {"file", filePath},
            {"content", content},
            {"related", relatedFiles}
        };
        
        return context.dump();
    }
};
```

### 4.5 Agent状态同步

#### 4.5.1 状态到动画映射
```cpp
class AgentStateToAnimationMapper {
public:
    PetAnimationState mapAgentState(const naw::agent::Agent& agent) {
        // 根据Agent状态决定动画
        if (agent.getMentalState().stress > 70.0f) {
            return PetAnimationState::Thinking;
        }
        
        if (agent.getPhysicalState().health < 50.0f) {
            return PetAnimationState::Idle; // 虚弱状态
        }
        
        // 检查是否有活跃任务
        if (hasActiveTask(agent)) {
            return PetAnimationState::Working;
        }
        
        return PetAnimationState::Idle;
    }
};
```

#### 4.5.2 事件驱动更新
```cpp
class AgentPetController {
private:
    naw::agent::Agent m_agent;
    PetAnimationController m_animationController;
    EventBus m_eventBus;
    
public:
    void initialize() {
        // 订阅Agent状态变更事件
        m_eventBus.subscribe("agent.state.changed", [this](const Event& event) {
            updateAnimation();
        });
        
        m_eventBus.subscribe("agent.memory.added", [this](const Event& event) {
            showMemoryNotification();
        });
    }
    
    void updateAnimation() {
        auto state = m_agentStateMapper.mapAgentState(m_agent);
        m_animationController.setState(state);
    }
};
```

## 5. 数据流设计

### 5.1 用户交互流程
```
用户输入/交互
    ↓
事件系统
    ↓
Agent控制器
    ↓
AI服务 (生成响应)
    ↓
更新Agent状态
    ↓
更新UI/动画
    ↓
显示反馈
```

### 5.2 AI对话流程
```
用户消息
    ↓
构建对话上下文 (包含Agent状态、记忆、项目上下文)
    ↓
调用硅基流动API
    ↓
解析响应
    ↓
更新Agent记忆
    ↓
触发动画/UI更新
    ↓
显示响应
```

### 5.3 MCP工具调用流程
```
AI Agent需要开发上下文
    ↓
调用MCP工具 (如read_file, get_project_structure)
    ↓
MCP Server处理请求
    ↓
收集项目信息/代码
    ↓
返回上下文数据
    ↓
AI Agent使用上下文生成响应
```

## 6. 项目结构

### 6.1 目录结构
```
NAW_system/
├── include/
│   └── naw/
│       ├── agent/          # 现有Agent模块
│       └── desktop_pet/    # 新增：桌宠模块
│           ├── PetWindow.h
│           ├── PetAnimation.h
│           ├── PetController.h
│           ├── AIService.h
│           ├── MCPServer.h
│           └── EventBus.h
├── src/
│   └── naw/
│       ├── agent/          # 现有Agent实现
│       └── desktop_pet/    # 新增：桌宠实现
│           ├── PetWindow.cpp
│           ├── PetAnimation.cpp
│           ├── PetController.cpp
│           ├── AIService.cpp
│           ├── MCPServer.cpp
│           ├── EventBus.cpp
│           └── main.cpp    # 主程序入口
├── resources/
│   └── desktop_pet/        # 新增：桌宠资源
│       ├── sprites/        # 精灵图片
│       ├── animations/      # 动画配置
│       └── config/         # 配置文件
│           ├── ai_config.json
│           └── pet_config.json
└── third_party/
    └── render/             # 现有Render引擎
```

### 6.2 CMake配置
```cmake
# desktop_pet模块CMakeLists.txt
add_executable(NAW_DesktopPet
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PetWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PetAnimation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PetController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AIService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MCPServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/EventBus.cpp
)

target_link_libraries(NAW_DesktopPet
    PRIVATE
        NAW_Agent
        ${RENDER_ENGINE_LIBRARY}
        # HTTP客户端库（如cpp-httplib或libcurl）
)

# 复制资源文件
file(COPY ${CMAKE_SOURCE_DIR}/resources/desktop_pet
     DESTINATION ${CMAKE_BINARY_DIR}/bin
)
```

## 7. 依赖库

### 7.1 必需依赖
- **Render Engine**: 已集成，用于渲染
- **nlohmann/json**: 已通过Render集成，用于JSON处理
- **NAW Agent**: 项目现有模块

### 7.2 新增依赖
- **HTTP客户端库**：
  - 选项1: `cpp-httplib` (轻量级，仅头文件)
  - 选项2: `libcurl` (功能强大，需要链接)
  - 推荐：`cpp-httplib`（简单易用）
  
- **可选依赖**：
  - `spdlog`: 日志库（推荐）
  - `fmt`: 字符串格式化（推荐）

## 8. 配置文件设计

### 8.1 AI配置 (ai_config.json)
```json
{
    "api": {
        "base_url": "https://api.siliconflow.cn/v1",
        "api_key": "${SILICONFLOW_API_KEY}",
        "model": "deepseek-ai/DeepSeek-V2.5",
        "temperature": 0.7,
        "max_tokens": 2000
    },
    "system_prompt": "你是一个智能开发助手桌宠，可以帮助用户进行代码开发。",
    "context": {
        "max_history": 50,
        "include_agent_state": true,
        "include_project_context": true
    }
}
```

### 8.2 桌宠配置 (pet_config.json)
```json
{
    "window": {
        "width": 300,
        "height": 400,
        "transparent": true,
        "always_on_top": true,
        "click_through": true
    },
    "animation": {
        "idle": "sprites/idle.png",
        "thinking": "sprites/thinking.png",
        "talking": "sprites/talking.png",
        "working": "sprites/working.png"
    },
    "agent": {
        "initial_state": {
            "name": "开发助手",
            "personality": {
                "courage": 60.0,
                "loyalty": 80.0,
                "independence": 50.0
            }
        }
    }
}
```

## 9. 开发计划

### 9.1 第一阶段：基础框架（1-2周）
- [ ] 创建透明窗口
- [ ] 实现基础精灵渲染
- [ ] 集成Render Engine
- [ ] 实现窗口拖拽和交互

### 9.2 第二阶段：Agent集成（1-2周）
- [ ] 集成NAW Agent框架
- [ ] 实现Agent状态管理
- [ ] 状态到动画的映射
- [ ] 基础UI显示

### 9.3 第三阶段：AI服务（2-3周）
- [ ] 实现HTTP客户端
- [ ] 集成硅基流动API
- [ ] 实现对话管理
- [ ] 流式响应处理
- [ ] 错误处理和重试

### 9.4 第四阶段：MCP集成（2-3周）
- [ ] 实现MCP Server基础
- [ ] 实现开发工具（read_file, list_files等）
- [ ] 项目上下文收集
- [ ] 代码分析功能

### 9.5 第五阶段：功能完善（1-2周）
- [ ] 动画系统完善
- [ ] UI系统完善
- [ ] 配置系统
- [ ] 日志系统
- [ ] 错误处理完善

### 9.6 第六阶段：测试和优化（1周）
- [ ] 单元测试
- [ ] 集成测试
- [ ] 性能优化
- [ ] 文档完善

## 10. 技术难点和解决方案

### 10.1 透明窗口实现
- **难点**：跨平台透明窗口支持
- **解决方案**：
  - Windows: 使用SetLayeredWindowAttributes或DWM
  - 考虑使用SDL的窗口属性扩展
  - 可能需要平台特定代码

### 10.2 异步API调用
- **难点**：避免阻塞UI线程
- **解决方案**：
  - 使用std::future/std::async
  - 或使用事件循环 + 回调
  - 确保线程安全

### 10.3 MCP协议实现
- **难点**：MCP协议规范和工具注册
- **解决方案**：
  - 参考MCP官方文档和示例
  - 实现标准JSON-RPC 2.0
  - 逐步实现工具集

### 10.4 项目上下文收集
- **难点**：高效收集和分析项目信息
- **解决方案**：
  - 使用文件系统监控（如std::filesystem）
  - 缓存机制减少重复分析
  - 增量更新策略

## 11. 扩展性考虑

### 11.1 插件系统
- 支持自定义动画
- 支持自定义工具
- 支持自定义UI主题

### 11.2 多Agent支持
- 支持多个桌宠实例
- Agent间交互
- 共享记忆系统

### 11.3 云端同步
- Agent状态云端备份
- 跨设备同步
- 共享记忆库

## 12. 测试策略

### 12.1 单元测试
- Agent状态管理测试
- AI API调用测试
- MCP工具测试
- 动画系统测试

### 12.2 集成测试
- 端到端对话流程
- MCP工具调用流程
- 状态同步测试

### 12.3 性能测试
- 渲染性能
- API响应时间
- 内存使用

## 13. 文档计划

### 13.1 开发文档
- API文档
- 架构文档
- 配置指南

### 13.2 用户文档
- 使用指南
- 配置说明
- 常见问题

## 14. 风险评估

### 14.1 技术风险
- **透明窗口兼容性**：不同Windows版本可能有差异
  - 缓解：提供降级方案（非透明模式）
- **API稳定性**：硅基流动API可能变更
  - 缓解：抽象API层，便于切换
- **性能问题**：实时渲染和AI调用可能影响性能
  - 缓解：优化渲染，异步处理

### 14.2 项目风险
- **开发时间**：功能复杂，可能超时
  - 缓解：分阶段开发，优先核心功能
- **依赖管理**：新增依赖可能带来问题
  - 缓解：选择成熟稳定的库

## 15. 总结

本方案设计了一个基于NAW Agent框架的智能桌宠应用，集成了硅基流动AI API和MCP开发辅助功能。通过分阶段实施，可以逐步实现核心功能，并保持系统的可扩展性和可维护性。

关键成功因素：
1. 充分利用现有Render Engine和Agent框架
2. 合理的模块划分和接口设计
3. 异步处理和性能优化
4. 完善的错误处理和日志系统
5. 良好的用户体验设计

通过本方案的实施，将创建一个功能完整、技术先进的智能桌宠应用，同时作为NAW Agent系统的优秀测试平台。
