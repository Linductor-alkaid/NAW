#include "naw/desktop_pet/service/APIClient.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/ToolManager.h"
#include "naw/desktop_pet/service/CodeTools.h"
#include "naw/desktop_pet/service/FunctionCallingHandler.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"

#include <iostream>
#include <string>
#include <vector>

using namespace naw::desktop_pet::service;
using namespace naw::desktop_pet::service::types;

// Windows UTF-8 控制台支持（简化版）
#if defined(_WIN32)
#include <windows.h>
static void setupConsoleUtf8() {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
}
#else
static void setupConsoleUtf8() {}
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

    // 3. 注册代码工具
    std::cout << "正在注册代码工具...\n";
    CodeTools::registerAllTools(toolManager);
    std::cout << "已注册 " << toolManager.getToolCount() << " 个工具\n";

    // 获取模型ID
    std::string modelId;
    if (auto m = cfg.get("routing.fallback_model"); m.has_value() && m->is_string()) {
        modelId = m->get<std::string>();
    } else if (auto m2 = cfg.get("models"); m2.has_value() && m2->is_array() && !m2->empty() &&
               (*m2)[0].is_object() && (*m2)[0].contains("model_id") && (*m2)[0]["model_id"].is_string()) {
        modelId = (*m2)[0]["model_id"].get<std::string>();
    } else {
        modelId = "deepseek-ai/DeepSeek-V3";
    }

    std::cout << "使用模型: " << modelId << "\n";
    std::cout << "Base URL: " << apiClient.getBaseUrl() << "\n";
    std::cout << "API Key : " << apiClient.getApiKeyRedacted() << "\n\n";

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

        while (iteration < maxIterations && !conversationComplete) {
            iteration++;

            // 发送请求
            ChatResponse response;
            try {
                response = apiClient.chat(request);
            } catch (const std::exception& e) {
                std::cerr << "\n[异常] " << e.what() << "\n";
                break;
            }

            // 检查是否有工具调用
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
                                    std::string resultStr = result.result.value().dump();
                                    if (resultStr.length() > 200) {
                                        std::cout << "  结果: " << resultStr.substr(0, 200) << "...\n";
                                    } else {
                                        std::cout << "  结果: " << resultStr << "\n";
                                    }
                                } catch (const std::exception& e) {
                                    std::cout << "  结果: [无法序列化结果]\n";
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

                    // 构建工具结果消息
                    auto toolResultMessages = FunctionCallingHandler::buildToolResultMessages(results);

                    // 构建后续请求
                    auto followUpRequest = FunctionCallingHandler::buildFollowUpRequest(
                        request.messages,
                        toolResultMessages,
                        request
                    );

                    // 更新请求为后续请求
                    request = followUpRequest;
                    std::cout << "助手> " << std::flush;
                } catch (const std::exception& e) {
                    std::cerr << "\n[异常] 工具调用处理异常: " << e.what() << "\n";
                    break;
                } catch (...) {
                    std::cerr << "\n[异常] 工具调用处理未知异常\n";
                    break;
                }
            } else {
                // 没有工具调用，对话完成
                std::cout << response.content << "\n";
                
                // 将助手回复添加到历史
                history.emplace_back(MessageRole::Assistant, response.content);
                conversationComplete = true;
            }
        }

        if (iteration >= maxIterations) {
            std::cerr << "\n[警告] 达到最大迭代次数，可能陷入循环\n";
        }
    }

    std::cout << "\n再见！\n";
    return 0;
}

