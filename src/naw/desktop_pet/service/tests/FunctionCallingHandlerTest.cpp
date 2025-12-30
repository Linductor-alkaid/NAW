#include "naw/desktop_pet/service/FunctionCallingHandler.h"
#include "naw/desktop_pet/service/ToolManager.h"
#include "naw/desktop_pet/service/ToolCallContext.h"
#include "naw/desktop_pet/service/ErrorHandler.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"

#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

using namespace naw::desktop_pet::service;

// Lightweight test assertion utilities (reuse existing test style)
namespace mini_test {

inline std::string toString(const std::string& v) { return v; }
inline std::string toString(const char* v) { return v ? std::string(v) : "null"; }
inline std::string toString(bool v) { return v ? "true" : "false"; }

// Specialization for MessageRole enum
inline std::string toString(types::MessageRole v) {
    return types::roleToString(v);
}

// Specialization for ErrorType enum
inline std::string toString(ErrorType v) {
    return ErrorInfo::errorTypeToString(v);
}

// Specialization for std::optional
template <typename T>
std::string toString(const std::optional<T>& v) {
    if (v.has_value()) {
        return toString(v.value());
    } else {
        return "nullopt";
    }
}

// For numeric types (excluding bool, char, signed char, unsigned char)
// This version has higher priority for arithmetic types
template <typename T>
auto toString(const T& v) -> std::enable_if_t<
    std::is_arithmetic_v<T> && 
    !std::is_same_v<T, bool> && 
    !std::is_same_v<T, char> && 
    !std::is_same_v<T, signed char> && 
    !std::is_same_v<T, unsigned char> &&
    !std::is_same_v<T, wchar_t>,
    std::string
> {
    return std::to_string(v);
}

// For types that support stream output (fallback)
// This version only matches if the arithmetic version doesn't match
template <typename T>
auto toString(const T& v) -> std::enable_if_t<
    !std::is_arithmetic_v<T> || 
    std::is_same_v<T, bool> || 
    std::is_same_v<T, char> || 
    std::is_same_v<T, signed char> || 
    std::is_same_v<T, unsigned char> ||
    std::is_same_v<T, wchar_t>,
    decltype(std::declval<std::ostringstream&>() << v, std::string{})
> {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

class AssertionFailed : public std::runtime_error {
public:
    explicit AssertionFailed(const std::string& msg) : std::runtime_error(msg) {}
};

#define CHECK_TRUE(cond)                                                                          \
    do {                                                                                          \
        if (!(cond))                                                                              \
            throw mini_test::AssertionFailed(std::string("CHECK_TRUE failed: ") + #cond);         \
    } while (0)

#define CHECK_FALSE(cond) CHECK_TRUE(!(cond))

#define CHECK_EQ(a, b)                                                                            \
    do {                                                                                          \
        const auto _va = (a);                                                                     \
        const auto _vb = (b);                                                                     \
        if (!(_va == _vb)) {                                                                      \
            throw mini_test::AssertionFailed(std::string("CHECK_EQ failed: ") + std::string(#a) + " vs " + std::string(#b) + \
                                             " (" + mini_test::toString(_va) + " vs " +           \
                                             mini_test::toString(_vb) + ")");                     \
        }                                                                                         \
    } while (0)

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline int run(const std::vector<TestCase>& tests) {
    int failed = 0;
    for (const auto& t : tests) {
        try {
            t.fn();
            std::cout << "[  OK  ] " << t.name << "\n";
        } catch (const AssertionFailed& e) {
            failed++;
            std::cout << "[ FAIL ] " << t.name << " :: " << e.what() << "\n";
        } catch (const std::exception& e) {
            failed++;
            std::cout << "[ EXC  ] " << t.name << " :: " << e.what() << "\n";
        } catch (...) {
            failed++;
            std::cout << "[ EXC  ] " << t.name << " :: unknown exception\n";
        }
    }
    std::cout << "Executed " << tests.size() << " cases, failed " << failed << ".\n";
    return failed == 0 ? 0 : 1;
}

} // namespace mini_test

// ========== Test Helper Functions ==========

namespace {
    // Create a simple test tool
    ToolDefinition createTestTool(const std::string& name, const std::string& description) {
        ToolDefinition tool;
        tool.name = name;
        tool.description = description;
        tool.parametersSchema = nlohmann::json{
            {"type", "object"},
            {"properties", nlohmann::json{
                {"value", nlohmann::json{{"type", "string"}}}
            }},
            {"required", nlohmann::json{"value"}}
        };
        tool.handler = [name](const nlohmann::json& args) -> nlohmann::json {
            std::string value = args.value("value", "");
            return nlohmann::json{{"result", "Tool " + name + " executed with: " + value}};
        };
        return tool;
    }

    // Create a tool that always throws an exception
    ToolDefinition createFailingTool(const std::string& name) {
        ToolDefinition tool;
        tool.name = name;
        tool.description = "A tool that always fails";
        tool.parametersSchema = nlohmann::json{
            {"type", "object"},
            {"properties", nlohmann::json::object()},
            {"required", nlohmann::json::array()}
        };
        tool.handler = [](const nlohmann::json&) -> nlohmann::json {
            throw std::runtime_error("Tool execution failed");
        };
        return tool;
    }

    // Create a slow tool (for testing execution time)
    ToolDefinition createSlowTool(const std::string& name, int delayMs) {
        ToolDefinition tool;
        tool.name = name;
        tool.description = "A slow tool";
        tool.parametersSchema = nlohmann::json{
            {"type", "object"},
            {"properties", nlohmann::json::object()},
            {"required", nlohmann::json::array()}
        };
        tool.handler = [delayMs](const nlohmann::json&) -> nlohmann::json {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            return nlohmann::json{{"result", "slow tool completed"}};
        };
        return tool;
    }

    // Create a test ChatResponse (with tool calls)
    types::ChatResponse createResponseWithToolCalls(const std::vector<types::ToolCall>& toolCalls) {
        types::ChatResponse response;
        response.content = "";
        response.toolCalls = toolCalls;
        response.finishReason = "tool_calls";
        return response;
    }

    // Create a test ChatResponse (without tool calls)
    types::ChatResponse createResponseWithoutToolCalls() {
        types::ChatResponse response;
        response.content = "Hello, world!";
        response.toolCalls = {};
        response.finishReason = "stop";
        return response;
    }

    // Create a test ToolCall
    types::ToolCall createToolCall(const std::string& id, const std::string& toolName, const nlohmann::json& arguments) {
        types::ToolCall toolCall;
        toolCall.id = id;
        toolCall.type = "function";
        toolCall.function.name = toolName;
        toolCall.function.arguments = arguments;
        return toolCall;
    }

    // Create a test ChatRequest
    types::ChatRequest createTestRequest() {
        types::ChatRequest request;
        request.model = "test-model";
        request.messages = {
            types::ChatMessage(types::MessageRole::User, "Test message")
        };
        request.temperature = 0.7f;
        request.maxTokens = 1000;
        return request;
    }
}

    // ========== Test Cases ==========

int main() {
    std::vector<mini_test::TestCase> tests;

    // ========== Tool Call Detection Tests ==========

    tests.push_back({"hasToolCalls - with tool calls", []() {
        auto toolCall = createToolCall("call_1", "test_tool", nlohmann::json{{"value", "test"}});
        auto response = createResponseWithToolCalls({toolCall});
        CHECK_TRUE(FunctionCallingHandler::hasToolCalls(response));
    }});

    tests.push_back({"hasToolCalls - without tool calls", []() {
        auto response = createResponseWithoutToolCalls();
        CHECK_FALSE(FunctionCallingHandler::hasToolCalls(response));
    }});

    tests.push_back({"extractToolCalls - extract tool calls", []() {
        auto toolCall1 = createToolCall("call_1", "tool1", nlohmann::json{{"value", "test1"}});
        auto toolCall2 = createToolCall("call_2", "tool2", nlohmann::json{{"value", "test2"}});
        auto response = createResponseWithToolCalls({toolCall1, toolCall2});
        
        auto extracted = FunctionCallingHandler::extractToolCalls(response);
        CHECK_EQ(extracted.size(), 2u);
        CHECK_EQ(extracted[0].id, "call_1");
        CHECK_EQ(extracted[1].id, "call_2");
    }});

    tests.push_back({"parseToolCallArguments - JSON object", []() {
        nlohmann::json args = nlohmann::json{{"value", "test"}};
        auto toolCall = createToolCall("call_1", "test_tool", args);
        
        auto parsed = FunctionCallingHandler::parseToolCallArguments(toolCall);
        CHECK_TRUE(parsed.has_value());
        CHECK_EQ(parsed.value()["value"], "test");
    }});

    tests.push_back({"parseToolCallArguments - JSON string", []() {
        std::string argsStr = R"({"value":"test"})";
        auto toolCall = createToolCall("call_1", "test_tool", nlohmann::json(argsStr));
        
        auto parsed = FunctionCallingHandler::parseToolCallArguments(toolCall);
        CHECK_TRUE(parsed.has_value());
        CHECK_EQ(parsed.value()["value"], "test");
    }});

    tests.push_back({"parseToolCallArguments - invalid JSON string", []() {
        auto toolCall = createToolCall("call_1", "test_tool", nlohmann::json("invalid json"));
        
        auto parsed = FunctionCallingHandler::parseToolCallArguments(toolCall);
        CHECK_FALSE(parsed.has_value());
    }});

    tests.push_back({"validateToolCall - valid tool call", []() {
        ToolManager toolManager;
        auto tool = createTestTool("test_tool", "Test tool");
        toolManager.registerTool(tool);
        
        auto toolCall = createToolCall("call_1", "test_tool", nlohmann::json{{"value", "test"}});
        CHECK_TRUE(FunctionCallingHandler::validateToolCall(toolCall, toolManager));
    }});

    tests.push_back({"validateToolCall - tool not found", []() {
        ToolManager toolManager;
        auto toolCall = createToolCall("call_1", "nonexistent_tool", nlohmann::json{{"value", "test"}});
        
        ErrorInfo error;
        CHECK_FALSE(FunctionCallingHandler::validateToolCall(toolCall, toolManager, &error));
        CHECK_EQ(error.errorType, ErrorType::InvalidRequest);
    }});

    tests.push_back({"validateToolCall - parameter validation failed", []() {
        ToolManager toolManager;
        auto tool = createTestTool("test_tool", "Test tool");
        toolManager.registerTool(tool);
        
        // Missing required parameter "value"
        auto toolCall = createToolCall("call_1", "test_tool", nlohmann::json{});
        
        ErrorInfo error;
        CHECK_FALSE(FunctionCallingHandler::validateToolCall(toolCall, toolManager, &error));
    }});

    // ========== Tool Call Execution Tests ==========

    tests.push_back({"executeToolCalls - single tool success", []() {
        ToolManager toolManager;
        auto tool = createTestTool("test_tool", "Test tool");
        toolManager.registerTool(tool);
        
        auto toolCall = createToolCall("call_1", "test_tool", nlohmann::json{{"value", "test"}});
        auto results = FunctionCallingHandler::executeToolCalls({toolCall}, toolManager);
        
        CHECK_EQ(results.size(), 1u);
        CHECK_TRUE(results[0].success);
        CHECK_EQ(results[0].toolCallId, "call_1");
        CHECK_EQ(results[0].toolName, "test_tool");
        CHECK_TRUE(results[0].result.has_value());
    }});

    tests.push_back({"executeToolCalls - multiple tools success", []() {
        ToolManager toolManager;
        auto tool1 = createTestTool("tool1", "Tool 1");
        auto tool2 = createTestTool("tool2", "Tool 2");
        toolManager.registerTool(tool1);
        toolManager.registerTool(tool2);
        
        auto toolCall1 = createToolCall("call_1", "tool1", nlohmann::json{{"value", "test1"}});
        auto toolCall2 = createToolCall("call_2", "tool2", nlohmann::json{{"value", "test2"}});
        auto results = FunctionCallingHandler::executeToolCalls({toolCall1, toolCall2}, toolManager);
        
        CHECK_EQ(results.size(), 2u);
        CHECK_TRUE(results[0].success);
        CHECK_TRUE(results[1].success);
    }});

    tests.push_back({"executeToolCalls - tool execution failed", []() {
        ToolManager toolManager;
        auto tool = createFailingTool("failing_tool");
        toolManager.registerTool(tool);
        
        auto toolCall = createToolCall("call_1", "failing_tool", nlohmann::json{});
        auto results = FunctionCallingHandler::executeToolCalls({toolCall}, toolManager);
        
        CHECK_EQ(results.size(), 1u);
        CHECK_FALSE(results[0].success);
        CHECK_TRUE(results[0].error.has_value());
    }});

    tests.push_back({"executeToolCalls - partial success partial failure", []() {
        ToolManager toolManager;
        auto tool1 = createTestTool("tool1", "Tool 1");
        auto tool2 = createFailingTool("failing_tool");
        toolManager.registerTool(tool1);
        toolManager.registerTool(tool2);
        
        auto toolCall1 = createToolCall("call_1", "tool1", nlohmann::json{{"value", "test"}});
        auto toolCall2 = createToolCall("call_2", "failing_tool", nlohmann::json{});
        auto results = FunctionCallingHandler::executeToolCalls({toolCall1, toolCall2}, toolManager);
        
        CHECK_EQ(results.size(), 2u);
        CHECK_TRUE(results[0].success);
        CHECK_FALSE(results[1].success);
    }});

    tests.push_back({"executeToolCalls - execution time recording", []() {
        ToolManager toolManager;
        auto tool = createSlowTool("slow_tool", 50); // 50ms delay
        toolManager.registerTool(tool);
        
        auto toolCall = createToolCall("call_1", "slow_tool", nlohmann::json::object());
        auto results = FunctionCallingHandler::executeToolCalls({toolCall}, toolManager);
        
        CHECK_EQ(results.size(), 1u);
        CHECK_TRUE(results[0].success);
        // Execution time should be at least 50ms (may have additional overhead)
        CHECK_TRUE(results[0].executionTimeMs >= 40.0); // Allow some tolerance
    }});

    // ========== Follow-up Request Building Tests ==========

    tests.push_back({"buildToolResultMessages - success result", []() {
        FunctionCallResult result;
        result.toolCallId = "call_1";
        result.toolName = "test_tool";
        result.success = true;
        result.result = nlohmann::json{{"output", "success"}};
        
        auto messages = FunctionCallingHandler::buildToolResultMessages({result});
        
        CHECK_EQ(messages.size(), 1u);
        CHECK_EQ(messages[0].role, types::MessageRole::Tool);
        CHECK_EQ(messages[0].toolCallId.value(), "call_1");
        CHECK_EQ(messages[0].name.value(), "test_tool");
        CHECK_TRUE(messages[0].isText());
    }});

    tests.push_back({"buildToolResultMessages - failure result", []() {
        FunctionCallResult result;
        result.toolCallId = "call_1";
        result.toolName = "test_tool";
        result.success = false;
        result.error = "Tool execution failed";
        
        auto messages = FunctionCallingHandler::buildToolResultMessages({result});
        
        CHECK_EQ(messages.size(), 1u);
        CHECK_EQ(messages[0].role, types::MessageRole::Tool);
        CHECK_TRUE(messages[0].isText());
        auto text = messages[0].textView();
        CHECK_TRUE(text.has_value());
        CHECK_TRUE(text.value().find("error") != std::string::npos || 
                   text.value().find("Error") != std::string::npos);
    }});

    tests.push_back({"buildToolResultMessages - multiple results", []() {
        FunctionCallResult result1;
        result1.toolCallId = "call_1";
        result1.toolName = "tool1";
        result1.success = true;
        result1.result = nlohmann::json{{"output", "result1"}};
        
        FunctionCallResult result2;
        result2.toolCallId = "call_2";
        result2.toolName = "tool2";
        result2.success = true;
        result2.result = nlohmann::json{{"output", "result2"}};
        
        auto messages = FunctionCallingHandler::buildToolResultMessages({result1, result2});
        
        CHECK_EQ(messages.size(), 2u);
        CHECK_EQ(messages[0].toolCallId.value(), "call_1");
        CHECK_EQ(messages[1].toolCallId.value(), "call_2");
    }});

    tests.push_back({"buildFollowUpRequest - message merging", []() {
        auto originalRequest = createTestRequest();
        
        FunctionCallResult result;
        result.toolCallId = "call_1";
        result.toolName = "test_tool";
        result.success = true;
        result.result = nlohmann::json{{"output", "success"}};
        
        auto toolResults = FunctionCallingHandler::buildToolResultMessages({result});
        auto followUp = FunctionCallingHandler::buildFollowUpRequest(
            originalRequest.messages,
            toolResults,
            originalRequest
        );
        
        CHECK_EQ(followUp.messages.size(), 2u); // original messages + tool results
        CHECK_EQ(followUp.model, originalRequest.model);
        CHECK_EQ(followUp.temperature, originalRequest.temperature);
    }});

    tests.push_back({"buildFollowUpRequest - parameter inheritance", []() {
        auto originalRequest = createTestRequest();
        originalRequest.maxTokens = 2000;
        originalRequest.topP = 0.9f;
        originalRequest.stop = "STOP";
        
        FunctionCallResult result;
        result.toolCallId = "call_1";
        result.toolName = "test_tool";
        result.success = true;
        result.result = nlohmann::json{{"output", "success"}};
        
        auto toolResults = FunctionCallingHandler::buildToolResultMessages({result});
        auto followUp = FunctionCallingHandler::buildFollowUpRequest(
            originalRequest.messages,
            toolResults,
            originalRequest
        );
        
        CHECK_EQ(followUp.maxTokens, originalRequest.maxTokens);
        CHECK_EQ(followUp.topP, originalRequest.topP);
        CHECK_EQ(followUp.stop, originalRequest.stop);
    }});

    // ========== Complete Flow Tests ==========

    tests.push_back({"processToolCalls - complete flow success", []() {
        ToolManager toolManager;
        auto tool = createTestTool("test_tool", "Test tool");
        toolManager.registerTool(tool);
        
        auto originalRequest = createTestRequest();
        auto toolCall = createToolCall("call_1", "test_tool", nlohmann::json{{"value", "test"}});
        auto response = createResponseWithToolCalls({toolCall});
        
        auto followUp = FunctionCallingHandler::processToolCalls(
            response,
            originalRequest,
            toolManager
        );
        
        CHECK_TRUE(followUp.has_value());
        CHECK_EQ(followUp.value().messages.size(), 2u); // original messages + tool results
    }});

    tests.push_back({"processToolCalls - no tool calls", []() {
        ToolManager toolManager;
        auto originalRequest = createTestRequest();
        auto response = createResponseWithoutToolCalls();
        
        auto followUp = FunctionCallingHandler::processToolCalls(
            response,
            originalRequest,
            toolManager
        );
        
        CHECK_FALSE(followUp.has_value());
    }});

    tests.push_back({"processToolCalls - tool not found", []() {
        ToolManager toolManager;
        auto originalRequest = createTestRequest();
        auto toolCall = createToolCall("call_1", "nonexistent_tool", nlohmann::json{{"value", "test"}});
        auto response = createResponseWithToolCalls({toolCall});
        
        ErrorInfo error;
        auto followUp = FunctionCallingHandler::processToolCalls(
            response,
            originalRequest,
            toolManager,
            &error
        );
        
        // Even if tool doesn't exist, should return follow-up request (with error info)
        CHECK_TRUE(followUp.has_value());
    }});

    // ========== FunctionCallResult::toJson Tests ==========

    tests.push_back({"FunctionCallResult::toJson - success result", []() {
        FunctionCallResult result;
        result.toolCallId = "call_1";
        result.toolName = "test_tool";
        result.success = true;
        result.result = nlohmann::json{{"output", "success"}};
        result.executionTimeMs = 10.5;
        
        auto json = result.toJson();
        CHECK_EQ(json["tool_call_id"], "call_1");
        CHECK_EQ(json["tool_name"], "test_tool");
        CHECK_EQ(json["success"], true);
        CHECK_EQ(json["execution_time_ms"], 10.5);
        CHECK_TRUE(json.contains("result"));
    }});

    tests.push_back({"FunctionCallResult::toJson - failure result", []() {
        FunctionCallResult result;
        result.toolCallId = "call_1";
        result.toolName = "test_tool";
        result.success = false;
        result.error = "Tool execution failed";
        result.executionTimeMs = 5.0;
        
        auto json = result.toJson();
        CHECK_EQ(json["success"], false);
        CHECK_EQ(json["error"], "Tool execution failed");
        CHECK_TRUE(json.contains("error"));
    }});

    // ========== Timeout Control Tests ==========

    tests.push_back({"executeToolCalls - timeout control - fast tool", []() {
        ToolManager toolManager;
        auto tool = createTestTool("fast_tool", "Fast tool");
        toolManager.registerTool(tool);

        auto toolCall = createToolCall("call_1", "fast_tool", nlohmann::json{{"value", "test"}});
        auto results = FunctionCallingHandler::executeToolCalls({toolCall}, toolManager, 1000);

        CHECK_EQ(results.size(), 1u);
        CHECK_TRUE(results[0].success);
    }});

    tests.push_back({"executeToolCalls - timeout control - timeout occurs", []() {
        ToolManager toolManager;
        auto slowTool = createSlowTool("slow_tool", 2000); // 2 seconds
        toolManager.registerTool(slowTool);

        auto toolCall = createToolCall("call_1", "slow_tool", nlohmann::json{{"value", "test"}});
        auto results = FunctionCallingHandler::executeToolCalls({toolCall}, toolManager, 100); // 100ms timeout

        CHECK_EQ(results.size(), 1u);
        CHECK_FALSE(results[0].success);
        CHECK_TRUE(results[0].error.has_value());
        CHECK_TRUE(results[0].error.value().find("timeout") != std::string::npos);
    }});

    // ========== Concurrent Execution Tests ==========

    tests.push_back({"executeToolCallsConcurrent - concurrent execution - all succeed", []() {
        ToolManager toolManager;
        auto tool1 = createTestTool("tool1", "Tool 1");
        auto tool2 = createTestTool("tool2", "Tool 2");
        toolManager.registerTool(tool1);
        toolManager.registerTool(tool2);

        auto toolCall1 = createToolCall("call_1", "tool1", nlohmann::json{{"value", "test1"}});
        auto toolCall2 = createToolCall("call_2", "tool2", nlohmann::json{{"value", "test2"}});
        
        auto results = FunctionCallingHandler::executeToolCallsConcurrent(
            {toolCall1, toolCall2},
            toolManager,
            0, // no concurrency limit
            0  // no timeout
        );

        CHECK_EQ(results.size(), 2u);
        CHECK_TRUE(results[0].success);
        CHECK_TRUE(results[1].success);
        CHECK_EQ(results[0].toolCallId, "call_1");
        CHECK_EQ(results[1].toolCallId, "call_2");
    }});

    tests.push_back({"executeToolCallsConcurrent - concurrency limit", []() {
        ToolManager toolManager;
        auto tool = createTestTool("test_tool", "Test tool");
        toolManager.registerTool(tool);

        std::vector<types::ToolCall> toolCalls;
        for (int i = 0; i < 5; ++i) {
            toolCalls.push_back(createToolCall(
                "call_" + std::to_string(i),
                "test_tool",
                nlohmann::json{{"value", std::to_string(i)}}  // Convert to string to match schema
            ));
        }

        auto results = FunctionCallingHandler::executeToolCallsConcurrent(
            toolCalls,
            toolManager,
            2, // max 2 concurrent
            0  // no timeout
        );

        CHECK_EQ(results.size(), 5u);
        for (const auto& result : results) {
            CHECK_TRUE(result.success);
        }
    }});

    // ========== ToolCallContext Tests ==========

    tests.push_back({"ToolCallContext - record and retrieve history", []() {
        ToolCallContext context(false); // disable cache for this test

        FunctionCallResult result;
        result.toolCallId = "call_1";
        result.toolName = "test_tool";
        result.success = true;
        result.result = nlohmann::json{{"output", "success"}};
        result.executionTimeMs = 10.0;

        nlohmann::json arguments = nlohmann::json{{"input", "test"}};
        context.recordToolCall(result, arguments);

        auto history = context.getHistory();
        CHECK_EQ(history.size(), 1u);
        CHECK_EQ(history[0].toolCallId, "call_1");
        CHECK_EQ(history[0].toolName, "test_tool");
        CHECK_TRUE(history[0].success);
    }});

    tests.push_back({"ToolCallContext - call chain tracking", []() {
        ToolCallContext context(false);

        std::string convId = "conv_1";
        context.startCallChain(convId);

        FunctionCallResult result;
        result.toolCallId = "call_1";
        result.toolName = "test_tool";
        result.success = true;
        result.result = nlohmann::json{{"output", "success"}};
        result.executionTimeMs = 10.0;

        context.recordToolCall(result, nlohmann::json{{"input", "test"}});
        context.endCallChain(convId);

        auto chain = context.getCallChain(convId);
        CHECK_TRUE(chain.has_value());
        CHECK_EQ(chain.value().conversationId, convId);
        CHECK_EQ(chain.value().toolCalls.size(), 1u);
    }});

    tests.push_back({"ToolCallContext - result caching", []() {
        ToolCallContext context(true, 60000); // enable cache, 60s TTL

        // First call - should execute
        FunctionCallResult result1;
        result1.toolCallId = "call_1";
        result1.toolName = "test_tool";
        result1.success = true;
        result1.result = nlohmann::json{{"output", "cached"}};
        result1.executionTimeMs = 10.0;

        nlohmann::json arguments = nlohmann::json{{"input", "test"}};
        context.recordToolCall(result1, arguments);

        // Check cache
        auto cached = context.getCachedResult("test_tool", arguments);
        CHECK_TRUE(cached.has_value());
        CHECK_EQ(cached.value()["output"], "cached");
    }});

    tests.push_back({"executeToolCalls - with context - cache hit", []() {
        ToolManager toolManager;
        auto tool = createTestTool("test_tool", "Test tool");
        toolManager.registerTool(tool);

        ToolCallContext context(true, 60000);

        // First call - should execute and cache
        auto toolCall = createToolCall("call_1", "test_tool", nlohmann::json{{"value", "test"}});
        auto results1 = FunctionCallingHandler::executeToolCalls({toolCall}, toolManager, 0, &context);

        CHECK_EQ(results1.size(), 1u);
        CHECK_TRUE(results1[0].success);

        // Second call with same arguments - should hit cache
        auto toolCall2 = createToolCall("call_2", "test_tool", nlohmann::json{{"value", "test"}});
        auto results2 = FunctionCallingHandler::executeToolCalls({toolCall2}, toolManager, 0, &context);

        CHECK_EQ(results2.size(), 1u);
        CHECK_TRUE(results2[0].success);
        CHECK_EQ(results2[0].toolCallId, "call_2"); // Different call ID but same result

        // Verify history
        auto history = context.getHistory();
        CHECK_EQ(history.size(), 2u); // Both calls should be recorded
    }});

    tests.push_back({"processToolCalls - with context", []() {
        ToolManager toolManager;
        auto tool = createTestTool("test_tool", "Test tool");
        toolManager.registerTool(tool);

        ToolCallContext context(false); // disable cache for simplicity

        auto originalRequest = createTestRequest();
        auto toolCall = createToolCall("call_1", "test_tool", nlohmann::json{{"value", "test"}});
        auto response = createResponseWithToolCalls({toolCall});

        auto followUp = FunctionCallingHandler::processToolCalls(
            response,
            originalRequest,
            toolManager,
            nullptr,
            &context
        );

        CHECK_TRUE(followUp.has_value());
        
        // Verify history was recorded
        auto history = context.getHistory();
        CHECK_EQ(history.size(), 1u);
        CHECK_EQ(history[0].toolCallId, "call_1");
    }});

    return mini_test::run(tests);
}

