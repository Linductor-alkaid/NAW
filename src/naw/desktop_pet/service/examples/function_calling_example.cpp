#include "naw/desktop_pet/service/APIClient.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/ToolManager.h"
#include "naw/desktop_pet/service/CodeTools.h"
#include "naw/desktop_pet/service/FunctionCallingHandler.h"
#include "naw/desktop_pet/service/ContextRefiner.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"

#include <iostream>
#include <string>
#include <vector>

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
    std::cout << "\n=== Function Calling 示例 ===\n"
              << "命令:\n"
              << "  /exit   - 退出\n"
              << "  /reset  - 清空对话历史\n"
              << "  /help   - 显示帮助\n"
              << "  /tools  - 显示可用工具列表\n\n"
              << "示例问题:\n"
              << "  - 读取文件: 请读取 README.md 文件\n"
              << "  - 列出文件: 列出当前目录的所有 .cpp 文件\n"
              << "  - 搜索代码: 搜索包含 ToolManager 的代码\n"
              << "  - 分析代码: 分析 ToolManager.h 文件\n\n";
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
    
    // 初始化上下文提纯器（用于自动优化过长的工具输出）
    ContextRefiner contextRefiner(cfg, apiClient);
    if (contextRefiner.isEnabled()) {
        std::cout << "上下文提纯已启用（将自动优化过长的工具输出）\n";
    }

    // 3. 注册代码工具
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

    // 维护对话历史
    std::vector<ChatMessage> history;
    history.emplace_back(MessageRole::System, 
        "你是一个代码助手，可以使用工具来读取文件、搜索代码、分析项目结构等。当用户需要查看文件、搜索代码或分析项目时，请使用相应的工具。");

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
            history.clear();
            history.emplace_back(MessageRole::System,
                "你是一个代码助手，可以使用工具来读取文件、搜索代码、分析项目结构等。当用户需要查看文件、搜索代码或分析项目时，请使用相应的工具。");
            std::cout << "对话历史已清空。\n";
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

        // 填充工具列表
        if (!toolManager.populateToolsToRequest(request, {}, "auto")) {
            std::cerr << "[错误] 填充工具列表失败\n";
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
            };
            cb.onToolCallDelta = [&](const APIClient::ToolCallDelta& delta) {
                // 工具调用增量信息（可选显示）
                if (!delta.nameDelta.empty() || !delta.argumentsDelta.empty()) {
                    std::cerr << "\n[工具调用增量] index=" << delta.index 
                              << " id=" << delta.id;
                    if (!delta.nameDelta.empty()) {
                        std::cerr << " name+=" << delta.nameDelta;
                    }
                    if (!delta.argumentsDelta.empty()) {
                        std::cerr << " args+=" << delta.argumentsDelta;
                    }
                    std::cerr << "\n";
                }
            };
            cb.onComplete = [&](const ChatResponse& response) {
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

                        // 先执行工具调用以获取结果（用于显示）
                        auto results = FunctionCallingHandler::executeToolCalls(toolCalls, toolManager);
                        
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

                        // 构建工具结果消息（传递 ContextRefiner 以自动优化过长的工具输出）
                        // 使用用户原始查询作为上下文，帮助提纯器更好地筛选相关信息
                        std::optional<std::string> userQuery;
                        if (!history.empty() && history.back().role == MessageRole::User) {
                            if (auto tv = history.back().textView(); tv.has_value()) {
                                userQuery = std::string(*tv);
                            }
                        }
                        
                        // 记录原始结果长度，用于检测是否进行了提纯
                        std::vector<size_t> originalSizes;
                        if (contextRefiner.isEnabled()) {
                            for (const auto& result : results) {
                                if (result.success && result.result.has_value()) {
                                    try {
                                        std::string originalText = result.result.value().dump();
                                        originalSizes.push_back(originalText.size());
                                    } catch (...) {
                                        originalSizes.push_back(0);
                                    }
                                } else {
                                    originalSizes.push_back(0);
                                }
                            }
                        }
                        
                        auto toolResultMessages = FunctionCallingHandler::buildToolResultMessages(
                            results, 
                            &contextRefiner, 
                            userQuery
                        );
                        
                        // 检测并报告上下文提纯
                        if (contextRefiner.isEnabled() && !originalSizes.empty()) {
                            for (size_t i = 0; i < toolResultMessages.size() && i < originalSizes.size(); ++i) {
                                const auto& msg = toolResultMessages[i];
                                if (msg.role == MessageRole::Tool && msg.name == results[i].toolName) {
                                    if (auto tv = msg.textView(); tv.has_value()) {
                                        size_t refinedSize = tv->size();
                                        size_t originalSize = originalSizes[i];
                                        if (originalSize > 0 && refinedSize < originalSize) {
                                            size_t reduction = originalSize - refinedSize;
                                            double reductionPercent = (100.0 * reduction) / originalSize;
                                            std::cout << "\n[上下文提纯] 工具 \"" << results[i].toolName 
                                                      << "\" 的输出已优化: " << originalSize << " 字符 -> " 
                                                      << refinedSize << " 字符 (减少 " << reductionPercent 
                                                      << "%, " << reduction << " 字符)\n";
                                        }
                                    }
                                }
                            }
                        }

                        // 构建后续请求
                        auto followUpRequest = FunctionCallingHandler::buildFollowUpRequest(
                            request.messages,
                            toolResultMessages,
                            request
                        );

                        // 更新请求为后续请求
                        request = followUpRequest;
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
                conversationComplete = true;
            };

            // 发送流式请求
            try {
                apiClient.chatStream(request, cb);
            } catch (const std::exception& e) {
                std::cerr << "\n[异常] " << e.what() << "\n";
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

