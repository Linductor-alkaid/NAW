#include "naw/desktop_pet/service/APIClient.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/ToolManager.h"
#include "naw/desktop_pet/service/CodeTools.h"
#include "naw/desktop_pet/service/FunctionCallingHandler.h"
#include "naw/desktop_pet/service/ProjectContextCollector.h"
#include "naw/desktop_pet/service/ContextManager.h"
#include "naw/desktop_pet/service/ModelManager.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <thread>
#include <chrono>
#include <atomic>

using namespace naw::desktop_pet::service;
using namespace naw::desktop_pet::service::types;

// Windows UTF-8 控制台支持
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#include <fcntl.h>

static void setupConsoleUtf8() {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
}

class Utf8ConsoleWriter {
public:
    explicit Utf8ConsoleWriter(DWORD stdHandleId)
        : m_h(GetStdHandle(stdHandleId))
    {}

    void write(std::string_view chunk) {
        if (chunk.empty()) return;
        DWORD mode = 0;
        if (m_h == INVALID_HANDLE_VALUE || m_h == nullptr || GetConsoleMode(m_h, &mode) == 0) {
            if (m_h == GetStdHandle(STD_ERROR_HANDLE)) {
                std::cerr << chunk;
            } else {
                std::cout << chunk;
            }
            return;
        }

        m_pending.append(chunk.data(), chunk.size());
        flushComplete();
    }

    void flush() { flushComplete(true); }

private:
    HANDLE m_h{nullptr};
    std::string m_pending;

    static size_t utf8CompletePrefixLen(std::string_view s) {
        size_t i = 0;
        while (i < s.size()) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x80) {
                i += 1;
                continue;
            }
            size_t need = 0;
            if ((c & 0xE0) == 0xC0) need = 2;
            else if ((c & 0xF0) == 0xE0) need = 3;
            else if ((c & 0xF8) == 0xF0) need = 4;
            else {
                i += 1;
                continue;
            }
            if (i + need > s.size()) break;
            bool ok = true;
            for (size_t k = 1; k < need; ++k) {
                const unsigned char cc = static_cast<unsigned char>(s[i + k]);
                if ((cc & 0xC0) != 0x80) {
                    ok = false;
                    break;
                }
            }
            if (!ok) {
                i += 1;
                continue;
            }
            i += need;
        }
        return i;
    }

    void flushComplete(bool flushAll = false) {
        if (m_pending.empty()) return;
        const size_t prefix = flushAll ? m_pending.size() : utf8CompletePrefixLen(m_pending);
        if (prefix == 0) return;

        const std::string_view ready(m_pending.data(), prefix);
        int wlen = MultiByteToWideChar(CP_UTF8, 0, ready.data(), static_cast<int>(ready.size()), nullptr, 0);
        if (wlen <= 0) {
            DWORD written = 0;
            WriteConsoleA(m_h, ready.data(), static_cast<DWORD>(ready.size()), &written, nullptr);
            m_pending.erase(0, prefix);
            return;
        }
        std::wstring ws;
        ws.resize(static_cast<size_t>(wlen));
        MultiByteToWideChar(CP_UTF8, 0, ready.data(), static_cast<int>(ready.size()), ws.data(), wlen);
        DWORD written = 0;
        WriteConsoleW(m_h, ws.data(), static_cast<DWORD>(ws.size()), &written, nullptr);
        m_pending.erase(0, prefix);
    }
};

static Utf8ConsoleWriter& stdoutWriter() {
    static Utf8ConsoleWriter w(STD_OUTPUT_HANDLE);
    return w;
}
static Utf8ConsoleWriter& stderrWriter() {
    static Utf8ConsoleWriter w(STD_ERROR_HANDLE);
    return w;
}
#else
static void setupConsoleUtf8() {}
static void writeUtf8ToConsole(std::string_view s) { std::cout << s; }
#endif

static void printHelp() {
    std::cout << "\n=== Function Calling 示例（集成测试） ===\n"
              << "本示例演示了以下集成测试：\n"
              << "  - ToolManager + CodeTools 集成\n"
              << "  - FunctionCallingHandler + ToolManager 集成\n"
              << "  - ToolManager + LLM集成\n"
              << "  - ProjectContextCollector + ContextManager 集成\n"
              << "  - 完整工具调用流程集成\n\n"
              << "命令:\n"
              << "  /exit   - 退出\n"
              << "  /reset  - 清空对话历史（保留项目上下文）\n"
              << "  /help   - 显示帮助\n"
              << "  /tools  - 显示可用工具列表\n\n"
              << "示例问题:\n"
              << "  - 读取文件: 请读取 README.md 文件\n"
              << "  - 列出文件: 列出当前目录的所有 .cpp 文件\n"
              << "  - 搜索代码: 搜索包含 ToolManager 的代码\n"
              << "  - 分析代码: 分析 ToolManager.h 文件\n"
              << "  - 项目结构: 分析项目结构（如果检测到项目根目录）\n\n";
}

static void printTools(const ToolManager& toolManager) {
    auto tools = toolManager.getToolsForAPI();
    std::cout << "\n可用工具 (" << tools.size() << " 个):\n";
    for (const auto& tool : tools) {
        if (tool.contains("function") && tool["function"].contains("name")) {
            std::cout << "  - " << tool["function"]["name"].get<std::string>();
            if (tool["function"].contains("description")) {
                std::cout << ": " << tool["function"]["description"].get<std::string>();
            }
            std::cout << "\n";
        }
    }
    std::cout << "\n";
}

// 构建包含项目上下文的初始对话历史（集成测试辅助函数）
static std::vector<ChatMessage> buildInitialHistory(
    ContextManager& contextManager,
    const std::optional<ProjectContext>& projectContext
) {
    std::vector<ChatMessage> history;
    std::string systemPrompt = "你是一个代码助手，可以使用工具来读取文件、搜索代码、分析项目结构等。当用户需要查看文件、搜索代码或分析项目时，请使用相应的工具。";
    
    history.emplace_back(MessageRole::System, systemPrompt);
    
    // 如果收集到了项目上下文，将其集成到对话历史中
    if (projectContext.has_value()) {
        // 使用 ContextManager 构建项目上下文消息
        ChatMessage projectContextMsg = contextManager.buildProjectContext(
            projectContext.value(),
            TaskType::CodeGeneration
        );
        
        // 将项目上下文添加到历史中
        if (auto tv = projectContextMsg.textView(); tv.has_value()) {
            // 如果项目上下文消息有内容，添加到系统提示之后
            if (projectContextMsg.role == MessageRole::System) {
                // 合并到系统提示中
                history[0] = ChatMessage(MessageRole::System, 
                    systemPrompt + "\n\n## 项目上下文\n" + std::string(*tv));
            } else {
                // 作为独立消息添加
                history.push_back(projectContextMsg);
            }
        }
    }
    
    return history;
}

int main() {
    setupConsoleUtf8();

    // 1. 加载配置
    ConfigManager cfg;
    ErrorInfo err;
    if (!cfg.loadFromFile("config/ai_service_config.json", &err)) {
        std::cerr << "加载配置失败: " << err.toString() << "\n";
        return 1;
    }
    cfg.applyEnvironmentOverrides();

    // 验证配置
    const auto issues = cfg.validate();
    for (const auto& s : issues) {
        if (s.rfind("WARN:", 0) == 0) {
            std::cerr << "[警告] " << s << "\n";
        } else {
            std::cerr << "[错误] " << s << "\n";
        }
    }

    // 2. 初始化组件
    APIClient apiClient(cfg);
    ToolManager toolManager;
    ModelManager modelManager(cfg);  // 用于模型健康状态检测
    ContextManager contextManager(cfg, &apiClient);
    ProjectContextCollector projectCollector;
    
    // 设置工具管理器到 ContextManager
    contextManager.setToolManager(&toolManager);
    
    // 加载模型配置到 ModelManager（用于健康状态监控）
    ErrorInfo modelErr;
    if (!modelManager.loadModelsFromConfig(&modelErr)) {
        std::cerr << "[警告] 加载模型配置失败: " << modelErr.toString() << "\n";
        std::cerr << "[提示] 模型健康状态检测功能可能不可用\n";
    }

    // 3. 尝试收集项目上下文（集成测试：ProjectContextCollector + ContextManager）
    std::optional<ProjectContext> projectContext;
    try {
        // 检测项目根目录（从当前工作目录开始）
        std::filesystem::path currentPath = std::filesystem::current_path();
        std::string projectRoot = ProjectContextCollector::detectProjectRoot(currentPath.string());
        
        if (!projectRoot.empty()) {
            std::cout << "检测到项目根目录: " << projectRoot << "\n";
            std::cout << "正在收集项目上下文...\n";
            
            ErrorInfo projectErr;
            projectContext = projectCollector.collectProjectContext(projectRoot, &projectErr);
            
            if (projectContext.has_value()) {
                std::cout << "项目上下文收集成功\n";
                std::cout << "  项目名称: " << (projectContext->structureSummary.empty() ? "未知" : "已识别") << "\n";
                std::cout << "  相关文件数: " << projectContext->relevantFiles.size() << "\n";
            } else {
                std::cerr << "项目上下文收集失败: " << projectErr.toString() << "\n";
            }
        } else {
            std::cout << "未检测到项目根目录，跳过项目上下文收集\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "项目上下文收集异常: " << e.what() << "\n";
    }
    std::cout << "\n";

    // 4. 注册代码工具
    std::cout << "正在注册代码工具...\n";
    CodeTools::registerAllTools(toolManager);
    std::cout << "已注册 " << toolManager.getToolCount() << " 个工具\n";

    // 获取模型ID - 优先使用代码生成任务的默认模型（GLM-4.7）
    std::string modelId;
    // 1. 优先从路由配置中获取 CodeGeneration 任务的默认模型
    if (auto routing = cfg.get("routing.default_model_per_task"); routing.has_value() && routing->is_object()) {
        if (routing->contains("CodeGeneration") && (*routing)["CodeGeneration"].is_string()) {
            modelId = (*routing)["CodeGeneration"].get<std::string>();
        }
    }
    // 2. 如果没有配置，尝试从 fallback_model 获取
    if (modelId.empty()) {
        if (auto m = cfg.get("routing.fallback_model"); m.has_value() && m->is_string()) {
            modelId = m->get<std::string>();
        }
    }
    // 3. 如果还没有，从 models 数组的第一个模型获取
    if (modelId.empty()) {
        if (auto m2 = cfg.get("models"); m2.has_value() && m2->is_array() && !m2->empty() &&
            (*m2)[0].is_object() && (*m2)[0].contains("model_id") && (*m2)[0]["model_id"].is_string()) {
            modelId = (*m2)[0]["model_id"].get<std::string>();
        }
    }
    // 4. 最后回退到默认值
    if (modelId.empty()) {
        modelId = "glm-4.7";  // 默认使用 GLM-4.7
    }

    std::cout << "使用模型: " << modelId << "\n";
    std::cout << "Base URL: " << apiClient.getBaseUrl() << "\n";
    std::cout << "API Key : " << apiClient.getApiKeyRedacted() << "\n";
    // 注意：如果使用 GLM-4.7，需要确保 APIClient 配置了正确的 API 端点
    // GLM-4.7 使用 api_providers.zhipu.base_url，而不是 api.base_url
    if (modelId == "glm-4.7") {
        if (auto zhipuUrl = cfg.get("api_providers.zhipu.base_url"); zhipuUrl.has_value() && zhipuUrl->is_string()) {
            std::cout << "GLM-4.7 API URL: " << zhipuUrl->get<std::string>() << "\n";
        }
    }
    std::cout << "\n";

    printHelp();

    // 维护对话历史（集成测试：使用 ContextManager 构建包含项目上下文的初始历史）
    std::vector<ChatMessage> history = buildInitialHistory(contextManager, projectContext);

    std::string line;
    while (true) {
        std::cout << "\n用户> ";
        if (!std::getline(std::cin, line)) break;
        
        if (line == "/exit") break;
        if (line == "/help") {
            printHelp();
            continue;
        }
        if (line == "/reset") {
            // 重置对话历史，但保留项目上下文（集成测试：验证上下文构建流程）
            history = buildInitialHistory(contextManager, projectContext);
            std::cout << "对话历史已清空（项目上下文已保留）。\n";
            continue;
        }
        if (line == "/tools") {
            printTools(toolManager);
            continue;
        }
        if (line.empty()) continue;

        // 添加用户消息
        history.emplace_back(MessageRole::User, line);

        // 构建请求
        ChatRequest request;
        request.model = modelId;
        request.messages = history;
        request.temperature = 0.7f;
        request.stream = true;  // 启用流式输出
        
        // 保存当前模型ID，用于错误处理时检查健康状态
        std::string currentModelId = modelId;

        // 填充工具列表（集成测试：使用 ContextManager 填充工具，验证工具与LLM集成）
        ErrorInfo toolErr;
        if (!contextManager.populateToolsToRequest(request, {}, "auto", &toolErr)) {
            std::cerr << "[错误] 填充工具列表失败: " << toolErr.toString() << "\n";
            continue;
        }

        std::cout << "助手> " << std::flush;

        // 支持多轮工具调用（最多5轮）
        int maxIterations = 5;
        int iteration = 0;
        bool conversationComplete = false;
        std::string assistantText;

        while (iteration < maxIterations && !conversationComplete) {
            iteration++;
            assistantText.clear();

            // 工具调用流式输出状态跟踪
            std::atomic<bool> toolCallStreaming{false};
            std::atomic<std::chrono::steady_clock::time_point> lastToolCallActivity{
                std::chrono::steady_clock::now()};
            
            // 保存当前模型ID，用于错误处理时检查健康状态
            std::string currentModelIdForError = currentModelId;
            
            // 记录请求开始时间，用于更新模型健康状态（需要在回调之前定义）
            auto requestStartTime = std::chrono::steady_clock::now();

            // 设置流式回调
            APIClient::Callbacks cb;
            cb.onTextDelta = [&](std::string_view delta) {
                if (!delta.empty()) {
                    assistantText.append(delta.data(), delta.size());
#if defined(_WIN32)
                    stdoutWriter().write(delta);
                    std::cout << std::flush;
#else
                    std::cout << delta << std::flush;
#endif
                }
                // 文本输出也表示活动，重置工具调用活动时间
                if (toolCallStreaming.load()) {
                    lastToolCallActivity.store(std::chrono::steady_clock::now());
                }
            };
            cb.onToolCallDelta = [&](const APIClient::ToolCallDelta& delta) {
                // 标记工具调用流式输出正在进行
                toolCallStreaming.store(true);
                lastToolCallActivity.store(std::chrono::steady_clock::now());
                
                // 工具调用增量信息（可选显示）
                // 注意：工具调用参数可能很长，流式输出需要时间，这是正常的
                if (!delta.nameDelta.empty() || !delta.argumentsDelta.empty()) {
                    // 可以选择显示或不显示增量信息（避免刷屏）
                    // 如果需要看到工具调用进度，可以取消下面的注释
                    /*
                    std::cerr << "\n[工具调用增量] index=" << delta.index 
                              << " id=" << delta.id;
                    if (!delta.nameDelta.empty()) {
                        std::cerr << " name+=" << delta.nameDelta;
                    }
                    if (!delta.argumentsDelta.empty()) {
                        std::cerr << " args+=" << delta.argumentsDelta;
                    }
                    std::cerr << "\n";
                    */
                }
            };
            cb.onComplete = [&](const ChatResponse& response) {
                // 流式响应完成，标记工具调用流式输出结束
                toolCallStreaming.store(false);
                
                // 流式响应完成，检查是否有工具调用
                // 注意：在流式输出中，文本内容应该通过 onTextDelta 实时输出
                // 但如果 onTextDelta 没有被调用，需要从 response.content 获取
                
                // 调试信息：检查文本内容
                if (assistantText.empty() && !response.content.empty()) {
                    // onTextDelta 可能没有被调用，使用 response.content
                    std::cerr << "[调试] onTextDelta 未收到文本，使用 response.content (长度=" 
                              << response.content.size() << ")\n";
                }
                
                if (FunctionCallingHandler::hasToolCalls(response)) {
                    std::cout << "\n[检测到工具调用，正在执行...]\n";

                    try {
                        // 提取工具调用信息（用于显示）
                        auto toolCalls = FunctionCallingHandler::extractToolCalls(response);
                        for (const auto& toolCall : toolCalls) {
                            std::cout << "[调用工具: " << toolCall.function.name << "]\n";
                        }

                        // 工具执行超时设置：
                        // - write_file 工具：10分钟（因为可能写入大文件）
                        // - 其他文件操作（read_file, list_files等）：5分钟
                        // - 其他工具：使用默认超时（30秒）
                        // - 0 表示无超时限制（不推荐）
                        int toolTimeoutMs = 0; // 默认无超时限制
                        
                        // 检查是否有 write_file 工具
                        bool hasWriteFile = false;
                        bool hasOtherFileOperation = false;
                        for (const auto& toolCall : toolCalls) {
                            std::string toolName = toolCall.function.name;
                            if (toolName == "write_file") {
                                hasWriteFile = true;
                            } else if (toolName == "read_file" || toolName == "list_files" || 
                                      toolName == "search_code") {
                                hasOtherFileOperation = true;
                            }
                        }
                        
                        if (hasWriteFile) {
                            // write_file 工具可能需要写入大文件，设置10分钟超时
                            toolTimeoutMs = 10 * 60 * 1000; // 10分钟
                            std::cout << "[提示] 检测到 write_file 工具，工具执行超时设置为 " 
                                      << (toolTimeoutMs / 1000) << " 秒（10分钟）\n";
                        } else if (hasOtherFileOperation) {
                            // 其他文件操作可能需要较长时间，设置5分钟超时
                            toolTimeoutMs = 5 * 60 * 1000; // 5分钟
                            std::cout << "[提示] 检测到文件操作，工具执行超时设置为 " 
                                      << (toolTimeoutMs / 1000) << " 秒\n";
                        }
                        
                        // 执行工具调用（工具执行是本地操作，不受API请求超时影响）
                        // 注意：工具执行期间，API请求已经完成，不会因为工具执行时间长而超时
                        std::cout << "[工具执行中，请稍候...]\n";
                        auto startTime = std::chrono::steady_clock::now();
                        
                        auto results = FunctionCallingHandler::executeToolCalls(
                            toolCalls, 
                            toolManager, 
                            toolTimeoutMs  // 传递超时参数
                        );
                        
                        auto endTime = std::chrono::steady_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                            endTime - startTime).count();
                        std::cout << "[工具执行完成，耗时 " << duration << " 毫秒]\n";
                        
                        for (const auto& result : results) {
                            std::cout << "[工具: " << result.toolName;
                            if (result.success) {
                                std::cout << "] 执行成功\n";
                                if (result.result.has_value()) {
                                    // 打印结果摘要（如果结果太长，只显示前200字符）
                                    try {
                                        // 先检查JSON大小，避免对大结果进行完整序列化
                                        const auto& json = result.result.value();
                                        size_t estimatedSize = 0;
                                        
                                        // 估算JSON大小（避免完整序列化）
                                        if (json.is_object()) {
                                            for (const auto& [key, value] : json.items()) {
                                                estimatedSize += key.size() + 10; // 键名 + 估算值大小
                                                if (value.is_string()) {
                                                    estimatedSize += value.get<std::string>().size();
                                                } else if (value.is_array()) {
                                                    estimatedSize += value.size() * 50; // 数组元素估算
                                                }
                                            }
                                        } else if (json.is_array()) {
                                            estimatedSize = json.size() * 50; // 数组元素估算
                                        } else if (json.is_string()) {
                                            estimatedSize = json.get<std::string>().size();
                                        } else {
                                            estimatedSize = 100; // 其他类型估算
                                        }
                                        
                                        // 如果估算大小超过1MB，只显示摘要，不进行完整序列化
                                        if (estimatedSize > 1024 * 1024) {
                                            std::cout << "  结果: [结果过大，已省略显示（" 
                                                      << (estimatedSize / 1024) << " KB）]\n";
                                            if (json.is_object()) {
                                                std::cout << "  结果类型: JSON对象，包含 " << json.size() << " 个键\n";
                                                // 显示前几个键
                                                size_t keyCount = 0;
                                                for (const auto& [key, value] : json.items()) {
                                                    if (keyCount++ >= 5) break;
                                                    std::cout << "    - " << key;
                                                    if (value.is_array()) {
                                                        std::cout << " (数组，包含 " << value.size() << " 个元素)";
                                                    } else if (value.is_string()) {
                                                        std::string str = value.get<std::string>();
                                                        if (str.size() > 50) {
                                                            std::cout << " (字符串，长度 " << str.size() << ")";
                                                        } else {
                                                            std::cout << ": " << str;
                                                        }
                                                    }
                                                    std::cout << "\n";
                                                }
                                                if (json.size() > 5) {
                                                    std::cout << "    ... (还有 " << (json.size() - 5) << " 个键)\n";
                                                }
                                            } else if (json.is_array()) {
                                                std::cout << "  结果类型: JSON数组，包含 " << json.size() << " 个元素\n";
                                            }
                                        } else {
                                            // 结果不大，可以安全序列化
                                            std::string resultStr = json.dump();
                                            if (resultStr.length() > 200) {
                                                std::cout << "  结果: " << resultStr.substr(0, 200) << "...\n";
                                            } else {
                                                std::cout << "  结果: " << resultStr << "\n";
                                            }
                                        }
                                    } catch (const nlohmann::json::exception& e) {
                                        // JSON序列化失败，可能是UTF-8编码问题（Windows上文件路径可能是GBK编码）
                                        // 使用 FunctionCallingHandler 的清理函数来处理
                                        try {
                                            nlohmann::json cleaned = FunctionCallingHandler::cleanJsonForUtf8(result.result.value());
                                            std::string resultStr = cleaned.dump();
                                            if (resultStr.length() > 200) {
                                                std::cout << "  结果: " << resultStr.substr(0, 200) << "...\n";
                                            } else {
                                                std::cout << "  结果: " << resultStr << "\n";
                                            }
                                            std::cerr << "  [注意: 结果包含无效UTF-8字符，已清理后显示]\n";
                                        } catch (const std::exception& e2) {
                                            // 即使清理后仍然失败，显示基本信息
                                            std::cerr << "  结果: [序列化失败: " << e.what() << "]\n";
                                            std::cerr << "  提示: 结果可能包含无效的UTF-8字符（常见于Windows文件路径）\n";
                                            try {
                                                const auto& json = result.result.value();
                                                if (json.is_object()) {
                                                    std::cerr << "  结果类型: JSON对象，包含 " << json.size() << " 个键\n";
                                                } else if (json.is_array()) {
                                                    std::cerr << "  结果类型: JSON数组，包含 " << json.size() << " 个元素\n";
                                                } else {
                                                    std::cerr << "  结果类型: " << json.type_name() << "\n";
                                                }
                                            } catch (...) {
                                                // 忽略类型检查的异常
                                            }
                                            std::cerr << "  注意: 工具执行成功，但结果无法序列化显示。结果已正常传递给LLM。\n";
                                        }
                                    } catch (const std::exception& e) {
                                        std::cerr << "  结果: [序列化失败: " << e.what() << "]\n";
                                    } catch (...) {
                                        std::cerr << "  结果: [序列化失败: 未知异常]\n";
                                    }
                                }
                            } else {
                                std::cout << "] 执行失败: ";
                                if (result.error.has_value()) {
                                    std::cout << result.error.value() << "\n";
                                } else {
                                    std::cout << "未知错误\n";
                                }
                            }
                        }

                        // 构建工具结果消息（ContextRefiner 已移除）
                        // 注意：工具执行已完成，现在构建后续请求，不会因为工具执行时间长而超时
                        std::cout << "[构建后续请求...]\n";
                        
                        std::optional<std::string> userQuery;
                        if (!history.empty() && history.back().role == MessageRole::User) {
                            if (auto tv = history.back().textView(); tv.has_value()) {
                                userQuery = std::string(*tv);
                            }
                        }
                        
                        auto toolResultMessages = FunctionCallingHandler::buildToolResultMessages(
                            results, 
                            userQuery
                        );

                        // 构建后续请求
                        // 注意：后续请求的API调用有独立的超时设置，不受工具执行时间影响
                        auto followUpRequest = FunctionCallingHandler::buildFollowUpRequest(
                            request.messages,
                            toolResultMessages,
                            request
                        );

                        // 更新请求为后续请求
                        request = followUpRequest;
                        std::cout << "[准备发送后续请求到LLM...]\n";
                        std::cout << "助手> " << std::flush;
                        conversationComplete = false; // 继续下一轮
                    } catch (const std::exception& e) {
                        std::cerr << "\n[异常] 工具调用处理异常: " << e.what() << "\n";
                        conversationComplete = true;
                    } catch (...) {
                        std::cerr << "\n[异常] 工具调用处理未知异常\n";
                        conversationComplete = true;
                    }
                } else {
                    // 没有工具调用，对话完成
                    // 文本内容应该已经通过 onTextDelta 实时输出了
                    // 但如果 onTextDelta 没有被调用，从 response.content 获取
                    std::string finalContent = assistantText;
                    if (finalContent.empty() && !response.content.empty()) {
                        // 如果流式输出没有触发，直接输出 response.content
                        finalContent = response.content;
#if defined(_WIN32)
                        stdoutWriter().write(finalContent);
                        stdoutWriter().flush();
#else
                        std::cout << finalContent;
#endif
                    }
                    
                    std::cout << "\n";
                    
                    // 将助手回复添加到历史
                    if (!finalContent.empty()) {
                        history.emplace_back(MessageRole::Assistant, finalContent);
                    } else {
                        // 如果既没有流式文本也没有响应内容，可能是异常情况
                        std::cerr << "[警告] LLM 响应为空，没有文本内容也没有工具调用\n";
                        std::cerr << "[调试] assistantText.size()=" << assistantText.size() 
                                  << ", response.content.size()=" << response.content.size() << "\n";
                    }
                    conversationComplete = true;
                }
            };
            cb.onError = [&](const ErrorInfo& error) {
                std::cerr << "\n[错误] " << error.toString() << "\n";
                
                // 更新模型健康状态（请求失败）
                auto requestEndTime = std::chrono::steady_clock::now();
                auto requestDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    requestEndTime - requestStartTime).count();
                modelManager.updateModelHealth(currentModelIdForError, false, 
                                               static_cast<uint32_t>(requestDuration));
                
                // 检查是否在工具调用流式输出期间发生的错误
                bool wasStreamingToolCall = toolCallStreaming.load();
                if (wasStreamingToolCall) {
                    std::cerr << "[重要提示] 错误发生在工具调用流式输出期间！\n";
                    std::cerr << "  工具调用参数可能很长（如写入大文件），LLM需要时间流式输出参数。\n";
                    std::cerr << "  如果错误是超时，这可能是正常的，因为：\n";
                    std::cerr << "  1. 工具调用参数很长，流式输出需要较长时间\n";
                    std::cerr << "  2. 在流式输出期间，每次收到数据都会重置超时计时器\n";
                    std::cerr << "  3. 但如果服务器处理时间过长，仍可能触发超时\n";
                    std::cerr << "  建议：增加超时时间（在配置文件中设置 api.default_timeout_ms）\n";
                }
                
                // 检查是否是网络错误，提供更友好的提示
                if (error.errorType == ErrorType::NetworkError) {
                    // 检查模型健康状态，辅助判断是否真的是连接超时
                    ModelHealthStatus modelHealth = modelManager.getModelHealth(currentModelIdForError);
                    bool isModelHealthy = (modelHealth == ModelHealthStatus::Healthy || 
                                          modelHealth == ModelHealthStatus::Degraded);
                    
                    std::cerr << "[提示] 网络连接失败，可能的原因：\n";
                    std::cerr << "  1. 网络连接不稳定，请检查网络状态\n";
                    std::cerr << "  2. API 服务器暂时不可用，请稍后重试\n";
                    std::cerr << "  3. 防火墙或代理设置阻止了连接\n";
                    std::cerr << "  4. API 密钥或端点配置错误\n";
                    
                    // 结合模型健康状态判断
                    if (wasStreamingToolCall && isModelHealthy) {
                        std::cerr << "\n[重要] 模型健康状态检查：\n";
                        std::cerr << "  - 模型状态: " << (modelHealth == ModelHealthStatus::Healthy ? "健康" : "降级") << "\n";
                        std::cerr << "  - 判断：模型本身是健康的，超时更可能是工具调用参数太大导致\n";
                        std::cerr << "  - 建议：这不是真正的网络连接问题，而是工具调用参数流式输出时间过长\n";
                        std::cerr << "  - 解决方案：APIClient已自动增加超时时间，如果仍超时，可能需要进一步增加\n";
                    } else if (wasStreamingToolCall && !isModelHealthy) {
                        std::cerr << "\n[重要] 模型健康状态检查：\n";
                        std::cerr << "  - 模型状态: " << (modelHealth == ModelHealthStatus::Unhealthy ? "不健康" : "未知") << "\n";
                        std::cerr << "  - 判断：模型可能存在问题，超时可能是模型服务异常导致的\n";
                        std::cerr << "  - 建议：检查模型服务状态，或尝试使用其他模型\n";
                    }
                    
                    // 如果是 error_code=4，提供更具体的建议
                    if (error.details.has_value() && error.details->contains("transport_error")) {
                        std::string transportError = error.details->at("transport_error").get<std::string>();
                        if (transportError.find("error_code=4") != std::string::npos) {
                            std::cerr << "\n[详细] 错误代码 4 表示连接失败，建议：\n";
                            std::cerr << "  - 检查网络连接是否正常\n";
                            std::cerr << "  - 验证 API 端点 URL 是否正确\n";
                            std::cerr << "  - 尝试增加超时时间（在配置文件中设置 api.default_timeout_ms）\n";
                            std::cerr << "  - 如果使用代理，检查代理配置\n";
                            if (wasStreamingToolCall) {
                                std::cerr << "  - 注意：如果是在工具调用流式输出期间，可能需要更长的超时时间\n";
                                if (isModelHealthy) {
                                    std::cerr << "  - 模型健康，超时更可能是工具调用参数太大，而非网络问题\n";
                                }
                            }
                        }
                    }
                }
                
                // 标记工具调用流式输出结束（即使出错）
                toolCallStreaming.store(false);
                conversationComplete = true;
            };

            // 发送流式请求（带重试机制）
            // 注意：在工具调用流式输出期间，不应该判断为网络超时
            // 因为工具调用参数可能很长（如write_file包含完整文件内容），LLM需要时间流式输出
            // APIClient会自动为包含工具的请求增加超时时间（默认超时的3倍，最多10分钟）
            int retryCount = 0;
            const int maxRetries = 3;
            bool requestSuccess = false;
            
            // 检测是否包含工具调用（通过检查请求中的工具列表）
            bool hasTools = !request.tools.empty();
            if (hasTools) {
                std::cout << "[提示] 检测到工具调用请求，超时时间已自动增加以支持大工具调用参数流式输出\n";
            }
            
            while (retryCount <= maxRetries && !requestSuccess) {
                try {
                    if (retryCount > 0) {
                        std::cerr << "\n[重试] 第 " << retryCount << " 次重试（共 " << maxRetries << " 次）...\n";
                        // 指数退避：等待时间 = 1秒 * 2^(retryCount-1)
                        int waitMs = 1000 * (1 << (retryCount - 1));
                        std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
                    }
                    
                    // 发送流式请求
                    // 注意：如果请求包含工具，APIClient会自动增加超时时间
                    // 在流式输出期间，每次收到数据都会重置读取超时计时器
                    apiClient.chatStream(request, cb);
                    requestSuccess = true;
                    
                    // 更新模型健康状态（请求成功）
                    auto requestEndTime = std::chrono::steady_clock::now();
                    auto requestDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        requestEndTime - requestStartTime).count();
                    modelManager.updateModelHealth(currentModelIdForError, true, 
                                                   static_cast<uint32_t>(requestDuration));
                } catch (const APIClient::ApiClientError& e) {
                    retryCount++;
                    const auto& errorInfo = e.errorInfo();
                    
                    // 检查是否应该重试
                    bool shouldRetry = false;
                    if (errorInfo.errorType == ErrorType::NetworkError && retryCount <= maxRetries) {
                        shouldRetry = true;
                    } else if (errorInfo.errorType == ErrorType::RateLimitError && retryCount <= maxRetries) {
                        shouldRetry = true;
                    } else if (errorInfo.errorType == ErrorType::ServerError && retryCount <= maxRetries) {
                        // 5xx 错误可以重试
                        shouldRetry = true;
                    }
                    
                    if (shouldRetry) {
                        std::cerr << "[警告] 请求失败，准备重试: " << errorInfo.toString() << "\n";
                        // 更新模型健康状态（请求失败，但会重试）
                        auto requestEndTime = std::chrono::steady_clock::now();
                        auto requestDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                            requestEndTime - requestStartTime).count();
                        modelManager.updateModelHealth(currentModelIdForError, false, 
                                                       static_cast<uint32_t>(requestDuration));
                        continue;
                    } else {
                        std::cerr << "\n[异常] 请求失败且已达到最大重试次数: " << e.what() << "\n";
                        // 更新模型健康状态（请求最终失败）
                        auto requestEndTime = std::chrono::steady_clock::now();
                        auto requestDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                            requestEndTime - requestStartTime).count();
                        modelManager.updateModelHealth(currentModelIdForError, false, 
                                                       static_cast<uint32_t>(requestDuration));
                        break;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "\n[异常] " << e.what() << "\n";
                    break;
                }
            }
            
            if (!requestSuccess) {
                std::cerr << "\n[错误] 请求最终失败，已放弃重试\n";
                break;
            }

#if defined(_WIN32)
            // 确保最后一个 UTF-8 字符不会卡在 pending 缓冲里
            stdoutWriter().flush();
            stderrWriter().flush();
#endif

            // 如果没有工具调用，退出循环
            if (conversationComplete) {
                break;
            }
        }

        if (iteration >= maxIterations) {
            std::cerr << "\n[警告] 达到最大迭代次数，可能陷入循环\n";
        }
    }

    std::cout << "\n再见！\n";
    return 0;
}

