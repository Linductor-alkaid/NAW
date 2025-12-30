#include "naw/desktop_pet/service/FunctionCallingHandler.h"

#include "naw/desktop_pet/service/ErrorHandler.h"

#include <chrono>
#include <sstream>
#include <stdexcept>

namespace naw::desktop_pet::service {

// ========== FunctionCallResult ==========

nlohmann::json FunctionCallResult::toJson() const {
    nlohmann::json j;
    j["tool_call_id"] = toolCallId;
    j["tool_name"] = toolName;
    if (result.has_value()) {
        j["result"] = result.value();
    }
    if (error.has_value()) {
        j["error"] = error.value();
    }
    j["execution_time_ms"] = executionTimeMs;
    j["success"] = success;
    return j;
}

// ========== 工具调用检测 ==========

bool FunctionCallingHandler::hasToolCalls(const types::ChatResponse& response) {
    return response.hasToolCalls();
}

std::vector<types::ToolCall> FunctionCallingHandler::extractToolCalls(const types::ChatResponse& response) {
    return response.toolCalls;
}

std::optional<nlohmann::json> FunctionCallingHandler::parseToolCallArguments(const types::ToolCall& toolCall) {
    const auto& arguments = toolCall.function.arguments;

    // 如果 arguments 已经是 JSON 对象，直接返回
    if (arguments.is_object()) {
        return std::make_optional(arguments);
    }

    // 如果是 null，视为空对象（因为工具参数通常是对象类型）
    if (arguments.is_null()) {
        return std::make_optional(nlohmann::json::object());
    }

    // 如果是字符串，尝试解析
    if (arguments.is_string()) {
        try {
            std::string argStr = arguments.get<std::string>();
            return std::make_optional(nlohmann::json::parse(argStr));
        } catch (const nlohmann::json::parse_error&) {
            // JSON 解析失败
            return std::nullopt;
        }
    }

    // 其他类型（数组、数字等）直接返回
    return std::make_optional(arguments);
}

bool FunctionCallingHandler::validateToolCall(const types::ToolCall& toolCall,
                                                ToolManager& toolManager,
                                                ErrorInfo* error) {
    // 验证工具调用 ID
    if (toolCall.id.empty()) {
        if (error) {
            error->errorType = ErrorType::InvalidRequest;
            error->errorCode = 400;
            error->message = "Tool call ID is empty";
        }
        return false;
    }

    // 验证工具名称
    if (toolCall.function.name.empty()) {
        if (error) {
            error->errorType = ErrorType::InvalidRequest;
            error->errorCode = 400;
            error->message = "Tool name is empty";
        }
        return false;
    }

    // 验证工具是否在 ToolManager 中注册
    if (!toolManager.hasTool(toolCall.function.name)) {
        if (error) {
            error->errorType = ErrorType::InvalidRequest;
            error->errorCode = 404;
            error->message = "Tool not found: " + toolCall.function.name;
        }
        return false;
    }

    // 解析并验证参数
    auto arguments = parseToolCallArguments(toolCall);
    if (!arguments.has_value()) {
        if (error) {
            error->errorType = ErrorType::InvalidRequest;
            error->errorCode = 400;
            error->message = "Failed to parse tool call arguments";
        }
        return false;
    }

    // 获取工具定义并验证参数
    auto toolDef = toolManager.getTool(toolCall.function.name);
    if (!toolDef.has_value()) {
        if (error) {
            error->errorType = ErrorType::InvalidRequest;
            error->errorCode = 404;
            error->message = "Tool definition not found: " + toolCall.function.name;
        }
        return false;
    }

    // 使用 ToolManager 验证参数
    ErrorInfo validationError;
    if (!ToolManager::validateArguments(toolDef.value(), arguments.value(), &validationError)) {
        if (error) {
            *error = validationError;
        }
        return false;
    }

    return true;
}

// ========== 工具调用执行 ==========

std::vector<FunctionCallResult> FunctionCallingHandler::executeToolCalls(
    const std::vector<types::ToolCall>& toolCalls,
    ToolManager& toolManager
) {
    std::vector<FunctionCallResult> results;
    results.reserve(toolCalls.size());

    for (const auto& toolCall : toolCalls) {
        FunctionCallResult result;
        result.toolCallId = toolCall.id;
        result.toolName = toolCall.function.name;

        // 记录开始时间
        auto startTime = std::chrono::high_resolution_clock::now();

        try {
            // 解析参数
            auto arguments = parseToolCallArguments(toolCall);
            if (!arguments.has_value()) {
                auto endTime = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
                result.executionTimeMs = static_cast<double>(duration.count());
                result.success = false;
                result.error = "Failed to parse tool call arguments";
                results.push_back(result);
                continue;
            }

            // 验证工具调用
            ErrorInfo validationError;
            if (!validateToolCall(toolCall, toolManager, &validationError)) {
                auto endTime = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
                result.executionTimeMs = static_cast<double>(duration.count());
                result.success = false;
                result.error = validationError.message;
                results.push_back(result);
                continue;
            }

            // 执行工具
            ErrorInfo executionError;
            auto toolResult = toolManager.executeTool(
                toolCall.function.name,
                arguments.value(),
                &executionError
            );

            // 计算执行时间
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            result.executionTimeMs = static_cast<double>(duration.count());

            if (toolResult.has_value()) {
                result.success = true;
                result.result = toolResult.value();
            } else {
                result.success = false;
                result.error = executionError.message.empty() ? "Tool execution failed" : executionError.message;
            }
        } catch (const std::exception& e) {
            // 捕获执行异常
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            result.executionTimeMs = static_cast<double>(duration.count());
            result.success = false;
            result.error = std::string("Exception: ") + e.what();
        } catch (...) {
            // 捕获未知异常
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            result.executionTimeMs = static_cast<double>(duration.count());
            result.success = false;
            result.error = "Unknown exception occurred";
        }

        results.push_back(result);
    }

    return results;
}

// ========== 后续请求构建 ==========

std::vector<types::ChatMessage> FunctionCallingHandler::buildToolResultMessages(
    const std::vector<FunctionCallResult>& results
) {
    std::vector<types::ChatMessage> messages;
    messages.reserve(results.size());

    for (const auto& result : results) {
        types::ChatMessage message;
        message.role = types::MessageRole::Tool;
        message.name = result.toolName;
        message.toolCallId = result.toolCallId;

        if (result.success && result.result.has_value()) {
            // On success, convert result to JSON string
            message.setText(result.result.value().dump());
        } else {
            // On failure, return error message
            std::string errorMsg = "Error: ";
            if (result.error.has_value()) {
                errorMsg += result.error.value();
            } else {
                errorMsg += "Unknown error";
            }
            message.setText(std::move(errorMsg));
        }

        messages.push_back(std::move(message));
    }

    return messages;
}

types::ChatRequest FunctionCallingHandler::buildFollowUpRequest(
    const std::vector<types::ChatMessage>& originalMessages,
    const std::vector<types::ChatMessage>& toolResults,
    const types::ChatRequest& originalRequest
) {
    types::ChatRequest followUpRequest;

    // Inherit model ID
    followUpRequest.model = originalRequest.model;

    // Merge messages: original messages + tool result messages
    followUpRequest.messages = originalMessages;
    followUpRequest.messages.insert(
        followUpRequest.messages.end(),
        toolResults.begin(),
        toolResults.end()
    );

    // Inherit request parameters
    followUpRequest.temperature = originalRequest.temperature;
    followUpRequest.maxTokens = originalRequest.maxTokens;
    followUpRequest.stream = originalRequest.stream;
    followUpRequest.stop = originalRequest.stop;
    followUpRequest.topP = originalRequest.topP;
    followUpRequest.topK = originalRequest.topK;

    // 继承工具列表（如果支持）
    followUpRequest.tools = originalRequest.tools;
    followUpRequest.toolChoice = originalRequest.toolChoice;

    return followUpRequest;
}

// ========== 完整流程（便捷方法） ==========

std::optional<types::ChatRequest> FunctionCallingHandler::processToolCalls(
    const types::ChatResponse& response,
    const types::ChatRequest& originalRequest,
    ToolManager& toolManager,
    ErrorInfo* error
) {
    // 检查是否有工具调用
    if (!hasToolCalls(response)) {
        return std::nullopt;
    }

    // 提取工具调用
    auto toolCalls = extractToolCalls(response);
    if (toolCalls.empty()) {
        if (error) {
            error->errorType = ErrorType::InvalidRequest;
            error->errorCode = 400;
            error->message = "No tool calls found in response";
        }
        return std::nullopt;
    }

    // 执行工具调用
    auto results = executeToolCalls(toolCalls, toolManager);

    // 检查是否有执行失败的工具
    bool hasFailure = false;
    for (const auto& result : results) {
        if (!result.success) {
            hasFailure = true;
            break;
        }
    }

    // 即使有部分失败，也继续构建后续请求（让LLM知道哪些工具失败了）

    // 构建工具结果消息
    auto toolResultMessages = buildToolResultMessages(results);

    // 构建后续请求
    auto followUpRequest = buildFollowUpRequest(
        originalRequest.messages,
        toolResultMessages,
        originalRequest
    );

    return followUpRequest;
}

} // namespace naw::desktop_pet::service

