#include "naw/desktop_pet/service/FunctionCallingHandler.h"

#include "naw/desktop_pet/service/ErrorHandler.h"
#include "naw/desktop_pet/service/ToolCallContext.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <sstream>
#include <stdexcept>

namespace naw::desktop_pet::service {

// ========== 辅助函数：清理JSON中的无效UTF-8字节 ==========

/**
 * @brief 清理JSON字符串中的无效UTF-8字节，将无效字节替换为替换字符
 */
static std::string cleanUtf8String(const std::string& str) {
    std::string cleaned;
    cleaned.reserve(str.size());
    
    for (size_t i = 0; i < str.size(); ) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if (c < 0x80) {
            // ASCII字符
            cleaned += str[i];
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            // 2字节UTF-8
            if (i + 1 < str.size() && (static_cast<unsigned char>(str[i+1]) & 0xC0) == 0x80) {
                cleaned += str.substr(i, 2);
                i += 2;
            } else {
                // 无效序列，替换为?
                cleaned += '?';
                i++;
            }
        } else if ((c & 0xF0) == 0xE0) {
            // 3字节UTF-8
            if (i + 2 < str.size() && 
                (static_cast<unsigned char>(str[i+1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(str[i+2]) & 0xC0) == 0x80) {
                cleaned += str.substr(i, 3);
                i += 3;
            } else {
                cleaned += '?';
                i++;
            }
        } else if ((c & 0xF8) == 0xF0) {
            // 4字节UTF-8
            if (i + 3 < str.size() &&
                (static_cast<unsigned char>(str[i+1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(str[i+2]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(str[i+3]) & 0xC0) == 0x80) {
                cleaned += str.substr(i, 4);
                i += 4;
            } else {
                cleaned += '?';
                i++;
            }
        } else {
            // 无效起始字节，替换为?
            cleaned += '?';
            i++;
        }
    }
    return cleaned;
}

/**
 * @brief 清理JSON中的无效UTF-8字节
 */
static nlohmann::json cleanJsonForUtf8(const nlohmann::json& j) {
    if (j.is_string()) {
        return cleanUtf8String(j.get<std::string>());
    } else if (j.is_array()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& item : j) {
            arr.push_back(cleanJsonForUtf8(item));
        }
        return arr;
    } else if (j.is_object()) {
        nlohmann::json obj = nlohmann::json::object();
        for (auto it = j.begin(); it != j.end(); ++it) {
            // 清理键和值
            std::string cleanedKey = cleanUtf8String(it.key());
            obj[cleanedKey] = cleanJsonForUtf8(it.value());
        }
        return obj;
    } else {
        // 其他类型直接返回
        return j;
    }
}

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

namespace {
    /**
     * @brief 执行单个工具调用的辅助函数
     * @param toolCall 工具调用对象
     * @param toolManager 工具管理器
     * @param timeoutMs 超时时间（毫秒），0表示无超时限制
     * @param context 工具调用上下文管理器（可选）
     * @return 执行结果
     */
    FunctionCallResult executeSingleToolCall(
        const types::ToolCall& toolCall,
        ToolManager& toolManager,
        int timeoutMs = 0,
        ToolCallContext* context = nullptr
    ) {
        FunctionCallResult result;
        result.toolCallId = toolCall.id;
        result.toolName = toolCall.function.name;

        // 记录开始时间
        auto startTime = std::chrono::high_resolution_clock::now();

        try {
            // 解析参数
            auto arguments = FunctionCallingHandler::parseToolCallArguments(toolCall);
            if (!arguments.has_value()) {
                auto endTime = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
                result.executionTimeMs = static_cast<double>(duration.count());
                result.success = false;
                result.error = "Failed to parse tool call arguments";
                // 记录失败的调用
                if (context) {
                    context->recordToolCall(result, nlohmann::json::object());
                }
                return result;
            }

            // 检查缓存（如果有上下文且启用缓存）
            if (context && context->isCacheEnabled()) {
                auto cachedResult = context->getCachedResult(toolCall.function.name, arguments.value());
                if (cachedResult.has_value()) {
                    auto endTime = std::chrono::high_resolution_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
                    result.executionTimeMs = static_cast<double>(duration.count());
                    result.success = true;
                    result.result = cachedResult.value();
                    // 记录缓存命中的调用（虽然是从缓存获取的）
                    if (context) {
                        context->recordToolCall(result, arguments.value());
                    }
                    return result;
                }
            }

            // 验证工具调用
            ErrorInfo validationError;
            if (!FunctionCallingHandler::validateToolCall(toolCall, toolManager, &validationError)) {
                auto endTime = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
                result.executionTimeMs = static_cast<double>(duration.count());
                result.success = false;
                result.error = validationError.message;
                // 记录失败的调用
                if (context) {
                    context->recordToolCall(result, arguments.value());
                }
                return result;
            }

            // 执行工具（支持超时控制）
            std::optional<nlohmann::json> toolResult;
            ErrorInfo executionError;
            bool timeoutOccurred = false;

            if (timeoutMs > 0) {
                // 使用异步执行并设置超时
                auto future = std::async(std::launch::async, [&toolManager, &toolCall, &arguments, &executionError]() {
                    return toolManager.executeTool(
                        toolCall.function.name,
                        arguments.value(),
                        &executionError
                    );
                });

                // 等待结果或超时
                auto status = future.wait_for(std::chrono::milliseconds(timeoutMs));
                if (status == std::future_status::timeout) {
                    timeoutOccurred = true;
                } else {
                    toolResult = future.get();
                }
            } else {
                // 无超时限制，直接执行
                toolResult = toolManager.executeTool(
                    toolCall.function.name,
                    arguments.value(),
                    &executionError
                );
            }

            // 计算执行时间
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            result.executionTimeMs = static_cast<double>(duration.count());

            if (timeoutOccurred) {
                result.success = false;
                result.error = "Tool execution timeout after " + std::to_string(timeoutMs) + "ms";
            } else if (toolResult.has_value()) {
                result.success = true;
                result.result = toolResult.value();
            } else {
                result.success = false;
                result.error = executionError.message.empty() ? "Tool execution failed" : executionError.message;
            }

            // 记录工具调用（成功后会自动缓存）
            if (context) {
                context->recordToolCall(result, arguments.value());
            }
        } catch (const std::exception& e) {
            // 捕获执行异常
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            result.executionTimeMs = static_cast<double>(duration.count());
            result.success = false;
            result.error = std::string("Exception: ") + e.what();
            // 记录失败的调用
            nlohmann::json arguments = nlohmann::json::object();
            try {
                auto parsedArgs = FunctionCallingHandler::parseToolCallArguments(toolCall);
                if (parsedArgs.has_value()) {
                    arguments = parsedArgs.value();
                }
            } catch (...) {
                // 忽略解析错误
            }
            if (context) {
                context->recordToolCall(result, arguments);
            }
        } catch (...) {
            // 捕获未知异常
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            result.executionTimeMs = static_cast<double>(duration.count());
            result.success = false;
            result.error = "Unknown exception occurred";
            // 记录失败的调用
            nlohmann::json arguments = nlohmann::json::object();
            try {
                auto parsedArgs = FunctionCallingHandler::parseToolCallArguments(toolCall);
                if (parsedArgs.has_value()) {
                    arguments = parsedArgs.value();
                }
            } catch (...) {
                // 忽略解析错误
            }
            if (context) {
                context->recordToolCall(result, arguments);
            }
        }

        return result;
    }
}

std::vector<FunctionCallResult> FunctionCallingHandler::executeToolCalls(
    const std::vector<types::ToolCall>& toolCalls,
    ToolManager& toolManager,
    int timeoutMs,
    ToolCallContext* context
) {
    std::vector<FunctionCallResult> results;
    results.reserve(toolCalls.size());

    for (const auto& toolCall : toolCalls) {
        results.push_back(executeSingleToolCall(toolCall, toolManager, timeoutMs, context));
    }

    return results;
}

std::vector<FunctionCallResult> FunctionCallingHandler::executeToolCallsConcurrent(
    const std::vector<types::ToolCall>& toolCalls,
    ToolManager& toolManager,
    size_t maxConcurrency,
    int timeoutMs,
    ToolCallContext* context
) {
    if (toolCalls.empty()) {
        return {};
    }

    std::vector<FunctionCallResult> results;
    results.reserve(toolCalls.size());

    // 如果没有并发限制或工具调用数量较少，直接并发执行所有
    if (maxConcurrency == 0 || toolCalls.size() <= maxConcurrency) {
        std::vector<std::future<FunctionCallResult>> futures;
        futures.reserve(toolCalls.size());

        // 启动所有异步任务
        for (const auto& toolCallRef : toolCalls) {
            types::ToolCall toolCall = toolCallRef; // 按值复制
            futures.push_back(std::async(
                std::launch::async,
                [&toolManager, toolCall, timeoutMs, context]() {
                    return executeSingleToolCall(toolCall, toolManager, timeoutMs, context);
                }
            ));
        }

        // 收集所有结果（按顺序）
        for (auto& future : futures) {
            results.push_back(future.get());
        }
    } else {
        // 有并发限制，分批执行
        // 使用索引来保持结果顺序
        struct FutureWithIndex {
            std::future<FunctionCallResult> future;
            size_t index;
        };
        
        std::vector<FutureWithIndex> activeFutures;
        activeFutures.reserve(maxConcurrency);
        std::vector<FunctionCallResult> tempResults(toolCalls.size());
        size_t nextIndex = 0;
        size_t completedCount = 0;

        while (completedCount < toolCalls.size()) {
            // 启动新的任务直到达到最大并发数
            while (activeFutures.size() < maxConcurrency && nextIndex < toolCalls.size()) {
                types::ToolCall toolCall = toolCalls[nextIndex]; // 按值复制
                size_t currentIndex = nextIndex;
                activeFutures.push_back({
                    std::async(
                        std::launch::async,
                        [&toolManager, toolCall, timeoutMs, context]() {
                            return executeSingleToolCall(toolCall, toolManager, timeoutMs, context);
                        }
                    ),
                    currentIndex
                });
                nextIndex++;
            }

            // 等待任意一个任务完成
            if (!activeFutures.empty()) {
                // 等待第一个任务完成（使用get()会阻塞直到完成）
                // 这样可以确保至少有一个任务完成，避免忙等待
                auto& firstFuture = activeFutures[0];
                tempResults[firstFuture.index] = firstFuture.future.get();
                activeFutures.erase(activeFutures.begin());
                completedCount++;
            }
        }

        // 按顺序收集结果
        results.assign(tempResults.begin(), tempResults.end());
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
            try {
                // 尝试正常dump
                message.setText(result.result.value().dump());
            } catch (const nlohmann::json::exception& e) {
                // 如果dump失败（可能是UTF-8编码问题），尝试清理JSON中的字符串
                try {
                    // 创建一个清理后的JSON副本
                    nlohmann::json cleaned = cleanJsonForUtf8(result.result.value());
                    message.setText(cleaned.dump());
                } catch (const std::exception& e2) {
                    // 如果还是失败，返回一个简化的错误信息
                    message.setText("Error: Failed to serialize tool result (invalid UTF-8 encoding). Tool: " + result.toolName);
                } catch (...) {
                    // 捕获所有其他异常
                    message.setText("Error: Failed to serialize tool result. Tool: " + result.toolName);
                }
            } catch (const std::exception& e) {
                // 捕获其他标准异常
                message.setText("Error: Failed to serialize tool result: " + std::string(e.what()));
            } catch (...) {
                // 捕获所有其他异常
                message.setText("Error: Failed to serialize tool result");
            }
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
    ErrorInfo* error,
    ToolCallContext* context
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
    auto results = executeToolCalls(toolCalls, toolManager, 0, context);

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

