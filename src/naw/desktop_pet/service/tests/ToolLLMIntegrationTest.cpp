#include "naw/desktop_pet/service/ToolManager.h"
#include "naw/desktop_pet/service/ContextManager.h"
#include "naw/desktop_pet/service/FunctionCallingHandler.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace naw::desktop_pet::service;

// 轻量自测断言工具（复用现有测试风格）
namespace mini_test {

inline std::string toString(const std::string& v) { return v; }
inline std::string toString(const char* v) { return v ? std::string(v) : "null"; }
inline std::string toString(bool v) { return v ? "true" : "false"; }

// Specialization for nlohmann::json
inline std::string toString(const nlohmann::json& v) {
    return v.dump();
}

// For numeric types
template <typename T>
auto toString(const T& v) -> std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>, std::string> {
    return std::to_string(v);
}

// For types that support stream output (fallback)
template <typename T>
auto toString(const T& v) -> std::enable_if_t<!std::is_arithmetic_v<T> || std::is_same_v<T, bool>, decltype(std::declval<std::ostringstream&>() << v, std::string{})> {
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
            throw mini_test::AssertionFailed(std::string("CHECK_EQ failed: ") + #a " vs " #b +    \
                                             " (" + mini_test::toString(_va) + " vs " +           \
                                             mini_test::toString(_vb) + ")");                     \
        }                                                                                         \
    } while (0)

#define CHECK_GT(a, b)                                                                            \
    do {                                                                                          \
        const auto _va = (a);                                                                     \
        const auto _vb = (b);                                                                     \
        if (!(_va > _vb)) {                                                                       \
            throw mini_test::AssertionFailed(std::string("CHECK_GT failed: ") + #a " > " #b +    \
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
            std::cout << "[ FAIL ] " << t.name << " :: Exception: " << e.what() << "\n";
        }
    }
    return failed;
}

} // namespace mini_test

// ========== 测试辅助函数 ==========

// 创建测试工具
ToolDefinition createTestTool(const std::string& name, PermissionLevel perm = PermissionLevel::Public) {
    ToolDefinition tool;
    tool.name = name;
    tool.description = "Test tool: " + name;
    tool.parametersSchema = nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"param1", {{"type", "string"}}},
            {"param2", {{"type", "number"}}}
        }},
        {"required", {"param1"}}
    };
    tool.permissionLevel = perm;
    tool.handler = [name](const nlohmann::json& args) -> nlohmann::json {
        nlohmann::json result;
        result["tool"] = name;
        result["args"] = args;
        return result;
    };
    return tool;
}

// ========== 测试用例 ==========

void test_getToolsForAPI_format() {
    ToolManager manager;
    
    // 注册几个测试工具
    manager.registerTool(createTestTool("tool1"));
    manager.registerTool(createTestTool("tool2"));
    
    // 获取工具列表
    auto tools = manager.getToolsForAPI();
    
    CHECK_EQ(tools.size(), 2);
    
    // 验证格式
    for (const auto& tool : tools) {
        CHECK_TRUE(tool.contains("type"));
        CHECK_EQ(tool["type"], "function");
        CHECK_TRUE(tool.contains("function"));
        CHECK_TRUE(tool["function"].contains("name"));
        CHECK_TRUE(tool["function"].contains("description"));
        CHECK_TRUE(tool["function"].contains("parameters"));
    }
}

void test_getToolsForAPI_with_filter() {
    ToolManager manager;
    
    // 注册不同权限级别的工具
    manager.registerTool(createTestTool("public_tool", PermissionLevel::Public));
    manager.registerTool(createTestTool("restricted_tool", PermissionLevel::Restricted));
    manager.registerTool(createTestTool("admin_tool", PermissionLevel::Admin));
    
    // 测试按权限过滤
    ToolFilter filter;
    filter.permissionLevel = PermissionLevel::Public;
    auto publicTools = manager.getToolsForAPI(filter);
    CHECK_EQ(publicTools.size(), 1);
    CHECK_EQ(publicTools[0]["function"]["name"], "public_tool");
    
    // 测试按名称前缀过滤
    ToolFilter prefixFilter;
    prefixFilter.namePrefix = "public_";
    auto prefixTools = manager.getToolsForAPI(prefixFilter);
    CHECK_EQ(prefixTools.size(), 1);
    CHECK_EQ(prefixTools[0]["function"]["name"], "public_tool");
}

void test_populateToolsToRequest_auto() {
    ToolManager manager;
    manager.registerTool(createTestTool("tool1"));
    manager.registerTool(createTestTool("tool2"));
    
    types::ChatRequest request;
    request.model = "test-model";
    
    bool success = manager.populateToolsToRequest(request, {}, "auto");
    CHECK_TRUE(success);
    CHECK_EQ(request.tools.size(), 2);
    CHECK_TRUE(request.toolChoice.has_value());
    CHECK_EQ(*request.toolChoice, "auto");
}

void test_populateToolsToRequest_none() {
    ToolManager manager;
    manager.registerTool(createTestTool("tool1"));
    
    types::ChatRequest request;
    request.model = "test-model";
    
    bool success = manager.populateToolsToRequest(request, {}, "none");
    CHECK_TRUE(success);
    CHECK_EQ(request.tools.size(), 1);
    CHECK_TRUE(request.toolChoice.has_value());
    CHECK_EQ(*request.toolChoice, "none");
}

void test_populateToolsToRequest_specific_tool() {
    ToolManager manager;
    manager.registerTool(createTestTool("tool1"));
    manager.registerTool(createTestTool("tool2"));
    
    types::ChatRequest request;
    request.model = "test-model";
    
    bool success = manager.populateToolsToRequest(request, {}, "tool1");
    CHECK_TRUE(success);
    CHECK_EQ(request.tools.size(), 2);
    CHECK_TRUE(request.toolChoice.has_value());
    CHECK_EQ(*request.toolChoice, "tool1");
}

void test_populateToolsToRequest_invalid_tool() {
    ToolManager manager;
    manager.registerTool(createTestTool("tool1"));
    
    types::ChatRequest request;
    request.model = "test-model";
    
    ErrorInfo error;
    bool success = manager.populateToolsToRequest(request, {}, "nonexistent_tool", &error);
    CHECK_FALSE(success);
    CHECK_EQ(error.errorType, ErrorType::InvalidRequest);
}

void test_populateToolsToRequest_with_filter() {
    ToolManager manager;
    manager.registerTool(createTestTool("public_tool", PermissionLevel::Public));
    manager.registerTool(createTestTool("restricted_tool", PermissionLevel::Restricted));
    
    types::ChatRequest request;
    request.model = "test-model";
    
    ToolFilter filter;
    filter.permissionLevel = PermissionLevel::Public;
    bool success = manager.populateToolsToRequest(request, filter, "auto");
    CHECK_TRUE(success);
    CHECK_EQ(request.tools.size(), 1);
    CHECK_EQ(request.tools[0]["function"]["name"], "public_tool");
}

void test_contextManager_populateTools() {
    ConfigManager configManager;
    ContextManager contextManager(configManager);
    
    ToolManager toolManager;
    toolManager.registerTool(createTestTool("tool1"));
    toolManager.registerTool(createTestTool("tool2"));
    
    // 设置工具管理器
    contextManager.setToolManager(&toolManager);
    
    types::ChatRequest request;
    request.model = "test-model";
    
    // 通过ContextManager填充工具
    bool success = contextManager.populateToolsToRequest(request);
    CHECK_TRUE(success);
    CHECK_EQ(request.tools.size(), 2);
}

void test_contextManager_populateTools_no_manager() {
    ConfigManager configManager;
    ContextManager contextManager(configManager);
    
    types::ChatRequest request;
    request.model = "test-model";
    
    // 未设置工具管理器，应该失败
    ErrorInfo error;
    bool success = contextManager.populateToolsToRequest(request, {}, "auto", &error);
    CHECK_FALSE(success);
    CHECK_EQ(error.errorType, ErrorType::InvalidRequest);
}

void test_functionCalling_tool_inheritance() {
    ToolManager manager;
    manager.registerTool(createTestTool("test_tool"));
    
    // 创建原始请求，包含工具列表
    types::ChatRequest originalRequest;
    originalRequest.model = "test-model";
    originalRequest.messages.push_back(types::ChatMessage(types::MessageRole::User, "Hello"));
    manager.populateToolsToRequest(originalRequest);
    
    // 模拟工具调用响应
    types::ChatResponse response;
    response.content = "";
    types::ToolCall toolCall;
    toolCall.id = "call_123";
    toolCall.function.name = "test_tool";
    toolCall.function.arguments = nlohmann::json{{"param1", "value1"}};
    response.toolCalls.push_back(toolCall);
    
    // 执行工具调用
    auto results = FunctionCallingHandler::executeToolCalls(response.toolCalls, manager);
    CHECK_EQ(results.size(), 1);
    CHECK_TRUE(results[0].success);
    
    // 构建后续请求
    auto toolResultMessages = FunctionCallingHandler::buildToolResultMessages(results);
    auto followUpRequest = FunctionCallingHandler::buildFollowUpRequest(
        originalRequest.messages,
        toolResultMessages,
        originalRequest
    );
    
    // 验证工具列表被继承
    CHECK_EQ(followUpRequest.tools.size(), originalRequest.tools.size());
    CHECK_TRUE(followUpRequest.toolChoice.has_value());
    CHECK_EQ(*followUpRequest.toolChoice, *originalRequest.toolChoice);
}

void test_complete_functionCalling_flow() {
    ToolManager manager;
    manager.registerTool(createTestTool("test_tool"));
    
    // 1. 构建包含工具的请求
    types::ChatRequest request;
    request.model = "test-model";
    request.messages.push_back(types::ChatMessage(types::MessageRole::User, "Use test_tool with param1='hello'"));
    manager.populateToolsToRequest(request);
    
    CHECK_EQ(request.tools.size(), 1);
    
    // 2. 模拟LLM返回工具调用
    types::ChatResponse response;
    response.content = "";
    types::ToolCall toolCall;
    toolCall.id = "call_123";
    toolCall.function.name = "test_tool";
    toolCall.function.arguments = nlohmann::json{{"param1", "hello"}};
    response.toolCalls.push_back(toolCall);
    
    // 3. 处理工具调用
    ErrorInfo error;
    auto followUpRequest = FunctionCallingHandler::processToolCalls(
        response,
        request,
        manager,
        &error
    );
    
    CHECK_TRUE(followUpRequest.has_value());
    
    // 4. 验证后续请求包含工具列表
    CHECK_EQ(followUpRequest->tools.size(), 1);
    CHECK_TRUE(followUpRequest->toolChoice.has_value());
    
    // 5. 验证工具结果消息
    CHECK_GT(followUpRequest->messages.size(), request.messages.size());
    bool hasToolResult = false;
    for (const auto& msg : followUpRequest->messages) {
        if (msg.role == types::MessageRole::Tool) {
            hasToolResult = true;
            CHECK_EQ(msg.name, "test_tool");
            CHECK_EQ(msg.toolCallId, "call_123");
            break;
        }
    }
    CHECK_TRUE(hasToolResult);
}

// ========== 主函数 ==========

int main() {
    std::vector<mini_test::TestCase> tests = {
        {"test_getToolsForAPI_format", test_getToolsForAPI_format},
        {"test_getToolsForAPI_with_filter", test_getToolsForAPI_with_filter},
        {"test_populateToolsToRequest_auto", test_populateToolsToRequest_auto},
        {"test_populateToolsToRequest_none", test_populateToolsToRequest_none},
        {"test_populateToolsToRequest_specific_tool", test_populateToolsToRequest_specific_tool},
        {"test_populateToolsToRequest_invalid_tool", test_populateToolsToRequest_invalid_tool},
        {"test_populateToolsToRequest_with_filter", test_populateToolsToRequest_with_filter},
        {"test_contextManager_populateTools", test_contextManager_populateTools},
        {"test_contextManager_populateTools_no_manager", test_contextManager_populateTools_no_manager},
        {"test_functionCalling_tool_inheritance", test_functionCalling_tool_inheritance},
        {"test_complete_functionCalling_flow", test_complete_functionCalling_flow},
    };
    
    int failed = mini_test::run(tests);
    if (failed > 0) {
        std::cout << "\n" << failed << " test(s) failed.\n";
        return 1;
    }
    
    std::cout << "\nAll tests passed!\n";
    return 0;
}

