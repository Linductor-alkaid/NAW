#pragma once

#include "naw/desktop_pet/service/ErrorTypes.h"
#include "naw/desktop_pet/service/ToolManager.h"
#include "naw/desktop_pet/service/types/ChatMessage.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace naw::desktop_pet::service {

/**
 * @brief 工具调用执行结果
 */
struct FunctionCallResult {
    std::string toolCallId;                      // 工具调用ID
    std::string toolName;                         // 工具名称
    std::optional<nlohmann::json> result;        // 执行结果（成功时）
    std::optional<std::string> error;             // 错误信息（失败时）
    double executionTimeMs{0.0};                  // 执行时间（毫秒）
    bool success{false};                          // 是否成功

    /**
     * @brief 转换为JSON格式
     */
    nlohmann::json toJson() const;
};

/**
 * @brief Function Calling 处理器
 *
 * 处理LLM返回的工具调用请求，执行工具并构建包含工具结果的后续请求。
 * 这是连接LLM和ToolManager的桥梁，实现完整的Function Calling流程。
 */
class FunctionCallingHandler {
public:
    // ========== 工具调用检测 ==========

    /**
     * @brief 检查响应中是否包含工具调用
     * @param response 聊天响应
     * @return 如果包含工具调用返回 true，否则返回 false
     */
    static bool hasToolCalls(const types::ChatResponse& response);

    /**
     * @brief 从响应中提取工具调用列表
     * @param response 聊天响应
     * @return 工具调用列表
     */
    static std::vector<types::ToolCall> extractToolCalls(const types::ChatResponse& response);

    /**
     * @brief 解析工具调用参数
     * @param toolCall 工具调用对象
     * @return 如果成功返回解析后的JSON对象，否则返回 std::nullopt
     */
    static std::optional<nlohmann::json> parseToolCallArguments(const types::ToolCall& toolCall);

    /**
     * @brief 验证工具调用结构
     * @param toolCall 工具调用对象
     * @param toolManager 工具管理器
     * @param error 如果验证失败，输出错误信息
     * @return 如果验证通过返回 true，否则返回 false
     */
    static bool validateToolCall(const types::ToolCall& toolCall,
                                  ToolManager& toolManager,
                                  ErrorInfo* error = nullptr);

    // ========== 工具调用执行 ==========

    /**
     * @brief 批量执行工具调用
     * @param toolCalls 工具调用列表
     * @param toolManager 工具管理器
     * @return 执行结果列表
     */
    static std::vector<FunctionCallResult> executeToolCalls(
        const std::vector<types::ToolCall>& toolCalls,
        ToolManager& toolManager
    );

    // ========== 后续请求构建 ==========

    /**
     * @brief 构建工具结果消息
     * @param results 工具执行结果列表
     * @return 工具结果消息列表
     */
    static std::vector<types::ChatMessage> buildToolResultMessages(
        const std::vector<FunctionCallResult>& results
    );

    /**
     * @brief 构建后续请求
     * @param originalMessages 原始消息列表
     * @param toolResults 工具结果消息列表
     * @param originalRequest 原始请求
     * @return 后续请求对象
     */
    static types::ChatRequest buildFollowUpRequest(
        const std::vector<types::ChatMessage>& originalMessages,
        const std::vector<types::ChatMessage>& toolResults,
        const types::ChatRequest& originalRequest
    );

    // ========== 完整流程（便捷方法） ==========

    /**
     * @brief 处理工具调用完整流程
     *
     * 这是一个便捷方法，整合了工具调用检测、执行和后续请求构建的完整流程。
     *
     * @param response LLM响应
     * @param originalRequest 原始请求
     * @param toolManager 工具管理器
     * @param error 如果处理失败，输出错误信息
     * @return 如果响应中包含工具调用且处理成功，返回后续请求；否则返回 std::nullopt
     */
    static std::optional<types::ChatRequest> processToolCalls(
        const types::ChatResponse& response,
        const types::ChatRequest& originalRequest,
        ToolManager& toolManager,
        ErrorInfo* error = nullptr
    );
};

} // namespace naw::desktop_pet::service

