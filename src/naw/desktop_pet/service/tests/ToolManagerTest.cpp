#include "naw/desktop_pet/service/ToolManager.h"
#include "naw/desktop_pet/service/ErrorHandler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace naw::desktop_pet::service;

// 轻量自测断言工具（复用现有测试风格）
namespace mini_test {

inline std::string toString(const std::string& v) { return v; }
inline std::string toString(const char* v) { return v ? std::string(v) : "null"; }
inline std::string toString(bool v) { return v ? "true" : "false"; }
inline std::string toString(ErrorType v) { 
    return ErrorInfo::errorTypeToString(v); 
}
inline std::string toString(PermissionLevel v) {
    switch (v) {
        case PermissionLevel::Public: return "Public";
        case PermissionLevel::Restricted: return "Restricted";
        case PermissionLevel::Admin: return "Admin";
        default: return "Unknown";
    }
}

template <typename T>
std::string toString(const T& v) {
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

// ========== 测试辅助函数 ==========

static ToolDefinition createSimpleTool(const std::string& name, const std::string& description) {
    ToolDefinition tool;
    tool.name = name;
    tool.description = description;
    tool.parametersSchema = nlohmann::json{
        {"type", "object"},
        {"properties",
         {
             {"value", {{"type", "string"}, {"description", "A string value"}}},
         }},
        {"required", {"value"}},
    };
    tool.handler = [](const nlohmann::json& args) -> nlohmann::json {
        return nlohmann::json{{"result", args["value"].get<std::string>() + "_processed"}};
    };
    return tool;
}

static ToolDefinition createAddTool() {
    ToolDefinition tool;
    tool.name = "add";
    tool.description = "Add two numbers";
    tool.parametersSchema = nlohmann::json{
        {"type", "object"},
        {"properties",
         {
             {"a", {{"type", "number"}, {"description", "First number"}}},
             {"b", {{"type", "number"}, {"description", "Second number"}}},
         }},
        {"required", {"a", "b"}},
    };
    tool.handler = [](const nlohmann::json& args) -> nlohmann::json {
        double a = args["a"].get<double>();
        double b = args["b"].get<double>();
        return nlohmann::json{{"result", a + b}};
    };
    return tool;
}

// ========== 测试用例 ==========

int main() {
    std::vector<mini_test::TestCase> tests;

    // ========== ToolDefinition 验证测试 ==========

    tests.push_back({"ToolDefinition_IsValid_ValidTool", []() {
        auto tool = createSimpleTool("test_tool", "Test tool");
        CHECK_TRUE(tool.isValid());
    }});

    tests.push_back({"ToolDefinition_IsValid_EmptyName", []() {
        auto tool = createSimpleTool("", "Test tool");
        std::string error;
        CHECK_FALSE(tool.isValid(&error));
        CHECK_TRUE(error.find("name") != std::string::npos);
    }});

    tests.push_back({"ToolDefinition_IsValid_NullHandler", []() {
        ToolDefinition tool;
        tool.name = "test_tool";
        tool.description = "Test tool";
        tool.parametersSchema = nlohmann::json{{"type", "object"}};
        // handler 未设置，默认为空
        std::string error;
        CHECK_FALSE(tool.isValid(&error));
        CHECK_TRUE(error.find("handler") != std::string::npos);
    }});

    // ========== 工具注册测试 ==========

    tests.push_back({"RegisterTool_Success", []() {
        ToolManager manager;
        auto tool = createSimpleTool("test_tool", "Test tool");
        CHECK_TRUE(manager.registerTool(tool));
        CHECK_TRUE(manager.hasTool("test_tool"));
    }});

    tests.push_back({"RegisterTool_Duplicate_Reject", []() {
        ToolManager manager;
        auto tool = createSimpleTool("test_tool", "Test tool");
        CHECK_TRUE(manager.registerTool(tool));
        CHECK_FALSE(manager.registerTool(tool, false)); // 不允许覆盖
    }});

    tests.push_back({"RegisterTool_Duplicate_AllowOverwrite", []() {
        ToolManager manager;
        auto tool1 = createSimpleTool("test_tool", "Test tool 1");
        auto tool2 = createSimpleTool("test_tool", "Test tool 2");
        CHECK_TRUE(manager.registerTool(tool1));
        CHECK_TRUE(manager.registerTool(tool2, true)); // 允许覆盖
        auto retrieved = manager.getTool("test_tool");
        CHECK_TRUE(retrieved.has_value());
        CHECK_EQ(retrieved->description, "Test tool 2");
    }});

    tests.push_back({"RegisterTool_InvalidTool", []() {
        ToolManager manager;
        ToolDefinition tool;
        tool.name = ""; // 无效名称
        ErrorInfo error;
        CHECK_FALSE(manager.registerTool(tool, false, &error));
        CHECK_TRUE(error.message.find("name") != std::string::npos);
    }});

    tests.push_back({"UnregisterTool_Success", []() {
        ToolManager manager;
        auto tool = createSimpleTool("test_tool", "Test tool");
        CHECK_TRUE(manager.registerTool(tool));
        CHECK_TRUE(manager.unregisterTool("test_tool"));
        CHECK_FALSE(manager.hasTool("test_tool"));
    }});

    tests.push_back({"UnregisterTool_NotFound", []() {
        ToolManager manager;
        CHECK_FALSE(manager.unregisterTool("nonexistent"));
    }});

    tests.push_back({"RegisterTools_Batch", []() {
        ToolManager manager;
        std::vector<ToolDefinition> tools;
        tools.push_back(createSimpleTool("tool1", "Tool 1"));
        tools.push_back(createSimpleTool("tool2", "Tool 2"));
        tools.push_back(createSimpleTool("tool3", "Tool 3"));
        CHECK_EQ(manager.registerTools(tools), 3);
        CHECK_EQ(manager.getToolCount(), 3);
    }});

    // ========== 工具查询测试 ==========

    tests.push_back({"GetTool_Success", []() {
        ToolManager manager;
        auto tool = createSimpleTool("test_tool", "Test tool");
        CHECK_TRUE(manager.registerTool(tool));
        auto retrieved = manager.getTool("test_tool");
        CHECK_TRUE(retrieved.has_value());
        CHECK_EQ(retrieved->name, "test_tool");
        CHECK_EQ(retrieved->description, "Test tool");
    }});

    tests.push_back({"GetTool_NotFound", []() {
        ToolManager manager;
        auto retrieved = manager.getTool("nonexistent");
        CHECK_FALSE(retrieved.has_value());
    }});

    tests.push_back({"HasTool_Exists", []() {
        ToolManager manager;
        auto tool = createSimpleTool("test_tool", "Test tool");
        CHECK_TRUE(manager.registerTool(tool));
        CHECK_TRUE(manager.hasTool("test_tool"));
    }});

    tests.push_back({"HasTool_NotExists", []() {
        ToolManager manager;
        CHECK_FALSE(manager.hasTool("nonexistent"));
    }});

    tests.push_back({"GetAllTools_Multiple", []() {
        ToolManager manager;
        manager.registerTool(createSimpleTool("tool1", "Tool 1"));
        manager.registerTool(createSimpleTool("tool2", "Tool 2"));
        auto allTools = manager.getAllTools();
        CHECK_EQ(allTools.size(), 2);
    }});

    tests.push_back({"GetToolNames_Multiple", []() {
        ToolManager manager;
        manager.registerTool(createSimpleTool("tool1", "Tool 1"));
        manager.registerTool(createSimpleTool("tool2", "Tool 2"));
        auto names = manager.getToolNames();
        CHECK_EQ(names.size(), 2);
        CHECK_TRUE(std::find(names.begin(), names.end(), "tool1") != names.end());
        CHECK_TRUE(std::find(names.begin(), names.end(), "tool2") != names.end());
    }});

    tests.push_back({"GetToolCount_Empty", []() {
        ToolManager manager;
        CHECK_EQ(manager.getToolCount(), 0);
    }});

    tests.push_back({"GetToolCount_Multiple", []() {
        ToolManager manager;
        manager.registerTool(createSimpleTool("tool1", "Tool 1"));
        manager.registerTool(createSimpleTool("tool2", "Tool 2"));
        CHECK_EQ(manager.getToolCount(), 2);
    }});

    // ========== 工具执行测试 ==========

    tests.push_back({"ExecuteTool_Success", []() {
        ToolManager manager;
        auto tool = createAddTool();
        CHECK_TRUE(manager.registerTool(tool));
        nlohmann::json args = {{"a", 5}, {"b", 3}};
        auto result = manager.executeTool("add", args);
        CHECK_TRUE(result.has_value());
        CHECK_EQ(result->at("result").get<double>(), 8.0);
    }});

    tests.push_back({"ExecuteTool_NotFound", []() {
        ToolManager manager;
        nlohmann::json args = {{"a", 5}, {"b", 3}};
        ErrorInfo error;
        auto result = manager.executeTool("nonexistent", args, &error);
        CHECK_FALSE(result.has_value());
        CHECK_TRUE(error.message.find("not found") != std::string::npos);
    }});

    tests.push_back({"ExecuteTool_MissingRequiredField", []() {
        ToolManager manager;
        auto tool = createAddTool();
        CHECK_TRUE(manager.registerTool(tool));
        nlohmann::json args = {{"a", 5}}; // 缺少 b
        ErrorInfo error;
        auto result = manager.executeTool("add", args, &error);
        CHECK_FALSE(result.has_value());
        CHECK_TRUE(error.message.find("required") != std::string::npos);
    }});

    tests.push_back({"ExecuteTool_InvalidType", []() {
        ToolManager manager;
        auto tool = createAddTool();
        CHECK_TRUE(manager.registerTool(tool));
        nlohmann::json args = {{"a", "not_a_number"}, {"b", 3}}; // a 应该是数字
        ErrorInfo error;
        auto result = manager.executeTool("add", args, &error);
        CHECK_FALSE(result.has_value());
        CHECK_TRUE(error.message.find("Invalid value") != std::string::npos);
    }});

    tests.push_back({"ExecuteTool_HandlerException", []() {
        ToolManager manager;
        ToolDefinition tool;
        tool.name = "error_tool";
        tool.description = "Tool that throws exception";
        tool.parametersSchema = nlohmann::json{{"type", "object"}};
        tool.handler = [](const nlohmann::json&) -> nlohmann::json {
            throw std::runtime_error("Test exception");
        };
        CHECK_TRUE(manager.registerTool(tool));
        // 使用 nlohmann::json::object() 创建空对象，而不是 {}
        nlohmann::json args = nlohmann::json::object();
        ErrorInfo error;
        auto result = manager.executeTool("error_tool", args, &error);
        CHECK_FALSE(result.has_value());
        // 确保错误类型正确
        CHECK_TRUE(error.errorType == ErrorType::ServerError);
        // 检查错误消息不为空
        CHECK_TRUE(!error.message.empty());
        // 错误消息应该是 "Tool execution failed: Test exception"
        // 检查是否包含 "execution failed"（完整短语）
        CHECK_TRUE(error.message.find("execution failed") != std::string::npos);
    }});

    // ========== 参数验证测试 ==========

    tests.push_back({"ValidateArguments_StringType", []() {
        ToolDefinition tool;
        tool.name = "string_tool";
        tool.description = "String tool";
        tool.parametersSchema = nlohmann::json{
            {"type", "object"},
            {"properties", {{"text", {{"type", "string"}}}}},
        };
        tool.handler = [](const nlohmann::json&) -> nlohmann::json { return {}; };
        nlohmann::json args = {{"text", "hello"}};
        ErrorInfo error;
        CHECK_TRUE(ToolManager::validateArguments(tool, args, &error));
    }});

    tests.push_back({"ValidateArguments_NumberType", []() {
        ToolDefinition tool;
        tool.name = "number_tool";
        tool.description = "Number tool";
        tool.parametersSchema = nlohmann::json{
            {"type", "object"},
            {"properties", {{"value", {{"type", "number"}}}}},
        };
        tool.handler = [](const nlohmann::json&) -> nlohmann::json { return {}; };
        nlohmann::json args = {{"value", 42.5}};
        ErrorInfo error;
        CHECK_TRUE(ToolManager::validateArguments(tool, args, &error));
    }});

    tests.push_back({"ValidateArguments_BooleanType", []() {
        ToolDefinition tool;
        tool.name = "bool_tool";
        tool.description = "Boolean tool";
        tool.parametersSchema = nlohmann::json{
            {"type", "object"},
            {"properties", {{"flag", {{"type", "boolean"}}}}},
        };
        tool.handler = [](const nlohmann::json&) -> nlohmann::json { return {}; };
        nlohmann::json args = {{"flag", true}};
        ErrorInfo error;
        CHECK_TRUE(ToolManager::validateArguments(tool, args, &error));
    }});

    tests.push_back({"ValidateArguments_ObjectType", []() {
        ToolDefinition tool;
        tool.name = "object_tool";
        tool.description = "Object tool";
        // 使用分步构建避免 MSVC 解析嵌套 JSON 的问题
        nlohmann::json nestedProperty = nlohmann::json{
            {"type", "string"}
        };
        nlohmann::json nestedProperties = nlohmann::json{
            {"value", nestedProperty}
        };
        nlohmann::json nestedSchema = nlohmann::json{
            {"type", "object"},
            {"properties", nestedProperties}
        };
        tool.parametersSchema = nlohmann::json{
            {"type", "object"},
            {"properties", {{"nested", nestedSchema}}}
        };
        tool.handler = [](const nlohmann::json&) -> nlohmann::json { return {}; };
        nlohmann::json args = {{"nested", {{"value", "test"}}}};
        ErrorInfo error;
        CHECK_TRUE(ToolManager::validateArguments(tool, args, &error));
    }});

    tests.push_back({"ValidateArguments_ArrayType", []() {
        ToolDefinition tool;
        tool.name = "array_tool";
        tool.description = "Array tool";
        // 使用分步构建避免 MSVC 解析嵌套 JSON 的问题
        nlohmann::json itemsSchema = nlohmann::json{
            {"type", "string"}
        };
        nlohmann::json arraySchema = nlohmann::json{
            {"type", "array"},
            {"items", itemsSchema}
        };
        tool.parametersSchema = nlohmann::json{
            {"type", "object"},
            {"properties", {{"items", arraySchema}}}
        };
        tool.handler = [](const nlohmann::json&) -> nlohmann::json { return {}; };
        nlohmann::json args = {{"items", {"a", "b", "c"}}};
        ErrorInfo error;
        CHECK_TRUE(ToolManager::validateArguments(tool, args, &error));
    }});

    tests.push_back({"ValidateArguments_TypeMismatch", []() {
        ToolDefinition tool;
        tool.name = "type_tool";
        tool.description = "Type tool";
        tool.parametersSchema = nlohmann::json{
            {"type", "object"},
            {"properties", {{"value", {{"type", "string"}}}}},
        };
        tool.handler = [](const nlohmann::json&) -> nlohmann::json { return {}; };
        nlohmann::json args = {{"value", 123}}; // 应该是字符串，但给了数字
        ErrorInfo error;
        CHECK_FALSE(ToolManager::validateArguments(tool, args, &error));
        CHECK_TRUE(error.message.find("Invalid value") != std::string::npos);
    }});

    // ========== 线程安全测试 ==========

    tests.push_back({"ThreadSafety_ConcurrentRegister", []() {
        ToolManager manager;
        const int numThreads = 10;
        const int toolsPerThread = 10;
        std::vector<std::thread> threads;
        std::atomic<int> successCount{0};

        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([&manager, i, &successCount]() {
                for (int j = 0; j < toolsPerThread; ++j) {
                    std::string toolName = "tool_" + std::to_string(i) + "_" + std::to_string(j);
                    auto tool = createSimpleTool(toolName, "Test tool");
                    if (manager.registerTool(tool)) {
                        successCount++;
                    }
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        // 所有工具都应该成功注册（没有重复名称）
        CHECK_EQ(successCount.load(), numThreads * toolsPerThread);
        CHECK_EQ(manager.getToolCount(), numThreads * toolsPerThread);
    }});

    tests.push_back({"ThreadSafety_ConcurrentQuery", []() {
        ToolManager manager;
        // 先注册一些工具
        for (int i = 0; i < 10; ++i) {
            std::string toolName = "tool_" + std::to_string(i);
            manager.registerTool(createSimpleTool(toolName, "Test tool"));
        }

        const int numThreads = 5;
        std::vector<std::thread> threads;
        std::atomic<int> queryCount{0};

        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([&manager, &queryCount]() {
                for (int j = 0; j < 100; ++j) {
                    std::string toolName = "tool_" + std::to_string(j % 10);
                    if (manager.hasTool(toolName)) {
                        queryCount++;
                    }
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        // 所有查询都应该成功
        CHECK_EQ(queryCount.load(), numThreads * 100);
    }});

    tests.push_back({"ThreadSafety_ConcurrentExecute", []() {
        ToolManager manager;
        auto tool = createAddTool();
        CHECK_TRUE(manager.registerTool(tool));

        const int numThreads = 5;
        std::vector<std::thread> threads;
        std::atomic<int> successCount{0};

        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([&manager, &successCount]() {
                for (int j = 0; j < 20; ++j) {
                    nlohmann::json args = {{"a", j}, {"b", j + 1}};
                    auto result = manager.executeTool("add", args);
                    if (result.has_value()) {
                        successCount++;
                    }
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        // 所有执行都应该成功
        CHECK_EQ(successCount.load(), numThreads * 20);
    }});

    // ========== 新功能测试：工具定义序列化/反序列化 ==========

    tests.push_back({"ToolDefinition_Serialization", []() {
        ToolDefinition tool = createSimpleTool("test_tool", "Test tool description");
        tool.permissionLevel = PermissionLevel::Restricted;
        
        // 序列化
        nlohmann::json json = tool.toJson();
        CHECK_TRUE(json.contains("name"));
        CHECK_TRUE(json.contains("description"));
        CHECK_TRUE(json.contains("parameters_schema"));
        CHECK_TRUE(json.contains("permission_level"));
        CHECK_EQ(json["name"].get<std::string>(), "test_tool");
        CHECK_EQ(json["permission_level"].get<std::string>(), "Restricted");
        
        // 反序列化（需要提供handler）
        std::string errorMsg;
        auto deserialized = ToolDefinition::fromJson(json, &errorMsg);
        CHECK_TRUE(deserialized.has_value());
        CHECK_EQ(deserialized->name, "test_tool");
        CHECK_EQ(deserialized->description, "Test tool description");
        CHECK_EQ(deserialized->permissionLevel, PermissionLevel::Restricted);
    }});

    // ========== 新功能测试：权限控制 ==========

    tests.push_back({"PermissionControl_CheckPermission", []() {
        ToolManager manager;
        
        ToolDefinition publicTool = createSimpleTool("public_tool", "Public tool");
        publicTool.permissionLevel = PermissionLevel::Public;
        CHECK_TRUE(manager.registerTool(publicTool));
        
        ToolDefinition restrictedTool = createSimpleTool("restricted_tool", "Restricted tool");
        restrictedTool.permissionLevel = PermissionLevel::Restricted;
        CHECK_TRUE(manager.registerTool(restrictedTool));
        
        ToolDefinition adminTool = createSimpleTool("admin_tool", "Admin tool");
        adminTool.permissionLevel = PermissionLevel::Admin;
        CHECK_TRUE(manager.registerTool(adminTool));
        
        // 检查权限
        CHECK_TRUE(manager.checkPermission("public_tool", PermissionLevel::Public));
        CHECK_TRUE(manager.checkPermission("restricted_tool", PermissionLevel::Restricted));
        CHECK_TRUE(manager.checkPermission("admin_tool", PermissionLevel::Admin));
        
        // Restricted级别不能访问Admin工具
        CHECK_FALSE(manager.checkPermission("admin_tool", PermissionLevel::Restricted));
        
        // Public级别不能访问Restricted和Admin工具
        CHECK_FALSE(manager.checkPermission("restricted_tool", PermissionLevel::Public));
        CHECK_FALSE(manager.checkPermission("admin_tool", PermissionLevel::Public));
    }});

    tests.push_back({"PermissionControl_ExecuteWithPermission", []() {
        ToolManager manager;
        
        ToolDefinition restrictedTool = createSimpleTool("restricted_tool", "Restricted tool");
        restrictedTool.permissionLevel = PermissionLevel::Restricted;
        CHECK_TRUE(manager.registerTool(restrictedTool));
        
        // 不检查权限时应该成功
        nlohmann::json args = {{"value", "test"}};
        auto result1 = manager.executeTool("restricted_tool", args, nullptr, false);
        CHECK_TRUE(result1.has_value());
        
        // 检查权限，Public级别应该失败
        ErrorInfo error;
        auto result2 = manager.executeTool("restricted_tool", args, &error, true, PermissionLevel::Public);
        CHECK_FALSE(result2.has_value());
        CHECK_EQ(error.errorType, ErrorType::InvalidRequest);
    }});

    // ========== 新功能测试：工具过滤 ==========

    tests.push_back({"ToolFiltering_ByPrefix", []() {
        ToolManager manager;
        
        CHECK_TRUE(manager.registerTool(createSimpleTool("tool_a", "Tool A")));
        CHECK_TRUE(manager.registerTool(createSimpleTool("tool_b", "Tool B")));
        CHECK_TRUE(manager.registerTool(createSimpleTool("test_tool", "Test tool")));
        
        auto tools = manager.getToolsByPrefix("tool_");
        CHECK_EQ(tools.size(), 2);
        
        std::vector<std::string> names;
        for (const auto& t : tools) {
            names.push_back(t.name);
        }
        CHECK_TRUE(std::find(names.begin(), names.end(), "tool_a") != names.end());
        CHECK_TRUE(std::find(names.begin(), names.end(), "tool_b") != names.end());
    }});

    tests.push_back({"ToolFiltering_ByPermission", []() {
        ToolManager manager;
        
        ToolDefinition tool1 = createSimpleTool("public1", "Public 1");
        tool1.permissionLevel = PermissionLevel::Public;
        CHECK_TRUE(manager.registerTool(tool1));
        
        ToolDefinition tool2 = createSimpleTool("restricted1", "Restricted 1");
        tool2.permissionLevel = PermissionLevel::Restricted;
        CHECK_TRUE(manager.registerTool(tool2));
        
        ToolDefinition tool3 = createSimpleTool("public2", "Public 2");
        tool3.permissionLevel = PermissionLevel::Public;
        CHECK_TRUE(manager.registerTool(tool3));
        
        auto publicTools = manager.getToolsByPermission(PermissionLevel::Public);
        CHECK_EQ(publicTools.size(), 2);
        
        auto restrictedTools = manager.getToolsByPermission(PermissionLevel::Restricted);
        CHECK_EQ(restrictedTools.size(), 1);
    }});

    tests.push_back({"ToolFiltering_ByFilter", []() {
        ToolManager manager;
        
        ToolDefinition tool1 = createSimpleTool("test_public", "Test Public");
        tool1.permissionLevel = PermissionLevel::Public;
        CHECK_TRUE(manager.registerTool(tool1));
        
        ToolDefinition tool2 = createSimpleTool("test_restricted", "Test Restricted");
        tool2.permissionLevel = PermissionLevel::Restricted;
        CHECK_TRUE(manager.registerTool(tool2));
        
        ToolDefinition tool3 = createSimpleTool("other_public", "Other Public");
        tool3.permissionLevel = PermissionLevel::Public;
        CHECK_TRUE(manager.registerTool(tool3));
        
        ToolFilter filter;
        filter.namePrefix = "test_";
        filter.permissionLevel = PermissionLevel::Public;
        
        auto filtered = manager.getFilteredTools(filter);
        CHECK_EQ(filtered.size(), 1);
        CHECK_EQ(filtered[0].name, "test_public");
    }});

    // ========== 新功能测试：参数验证增强 ==========

    tests.push_back({"ParameterValidation_Enum", []() {
        ToolManager manager;
        
        ToolDefinition tool;
        tool.name = "enum_tool";
        tool.description = "Tool with enum validation";
        tool.parametersSchema = nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"status", {
                    {"type", "string"},
                    {"enum", {"active", "inactive", "pending"}}
                }}
            }},
            {"required", {"status"}}
        };
        tool.handler = [](const nlohmann::json&) { return nlohmann::json{{"result", "ok"}}; };
        
        CHECK_TRUE(manager.registerTool(tool));
        
        // 有效值
        nlohmann::json validArgs = {{"status", "active"}};
        ErrorInfo error;
        CHECK_TRUE(manager.validateArguments(tool, validArgs, &error));
        
        // 无效值
        nlohmann::json invalidArgs = {{"status", "invalid"}};
        CHECK_FALSE(manager.validateArguments(tool, invalidArgs, &error));
    }});

    tests.push_back({"ParameterValidation_Range", []() {
        ToolManager manager;
        
        ToolDefinition tool;
        tool.name = "range_tool";
        tool.description = "Tool with range validation";
        tool.parametersSchema = nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"age", {
                    {"type", "integer"},
                    {"minimum", 0},
                    {"maximum", 120}
                }},
                {"score", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"maximum", 100.0}
                }}
            }},
            {"required", {"age", "score"}}
        };
        tool.handler = [](const nlohmann::json&) { return nlohmann::json{{"result", "ok"}}; };
        
        CHECK_TRUE(manager.registerTool(tool));
        
        // 有效值
        nlohmann::json validArgs = {{"age", 25}, {"score", 85.5}};
        ErrorInfo error;
        CHECK_TRUE(manager.validateArguments(tool, validArgs, &error));
        
        // 超出范围
        nlohmann::json invalidArgs1 = {{"age", 150}, {"score", 85.5}};
        CHECK_FALSE(manager.validateArguments(tool, invalidArgs1, &error));
        
        nlohmann::json invalidArgs2 = {{"age", 25}, {"score", 150.0}};
        CHECK_FALSE(manager.validateArguments(tool, invalidArgs2, &error));
    }});

    tests.push_back({"ParameterValidation_StringLength", []() {
        ToolManager manager;
        
        ToolDefinition tool;
        tool.name = "length_tool";
        tool.description = "Tool with string length validation";
        tool.parametersSchema = nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"name", {
                    {"type", "string"},
                    {"minLength", 3},
                    {"maxLength", 10}
                }}
            }},
            {"required", {"name"}}
        };
        tool.handler = [](const nlohmann::json&) { return nlohmann::json{{"result", "ok"}}; };
        
        CHECK_TRUE(manager.registerTool(tool));
        
        // 有效值
        nlohmann::json validArgs = {{"name", "test"}};
        ErrorInfo error;
        CHECK_TRUE(manager.validateArguments(tool, validArgs, &error));
        
        // 太短
        nlohmann::json invalidArgs1 = {{"name", "ab"}};
        CHECK_FALSE(manager.validateArguments(tool, invalidArgs1, &error));
        
        // 太长
        nlohmann::json invalidArgs2 = {{"name", "this_is_too_long"}};
        CHECK_FALSE(manager.validateArguments(tool, invalidArgs2, &error));
    }});

    // ========== 新功能测试：工具执行统计 ==========

    tests.push_back({"ToolStatistics_Basic", []() {
        ToolManager manager;
        auto tool = createAddTool();
        CHECK_TRUE(manager.registerTool(tool));
        
        // 执行工具
        nlohmann::json args = {{"a", 1}, {"b", 2}};
        for (int i = 0; i < 5; ++i) {
            manager.executeTool("add", args);
        }
        
        // 检查统计
        auto stats = manager.getToolStats("add");
        CHECK_TRUE(stats.has_value());
        CHECK_EQ(stats->callCount, 5);
        CHECK_TRUE(stats->averageExecutionTimeMs > 0.0);
    }});

    tests.push_back({"ToolStatistics_ErrorTracking", []() {
        ToolManager manager;
        
        ToolDefinition errorTool;
        errorTool.name = "error_tool";
        errorTool.description = "Tool that throws errors";
        errorTool.parametersSchema = nlohmann::json{
            {"type", "object"},
            {"properties", nlohmann::json::object()},
            {"required", nlohmann::json::array()}
        };
        errorTool.handler = [](const nlohmann::json&) -> nlohmann::json {
            throw std::runtime_error("Test error");
        };
        
        CHECK_TRUE(manager.registerTool(errorTool));
        
        // 执行多次（会失败）
        nlohmann::json args = {};
        for (int i = 0; i < 3; ++i) {
            manager.executeTool("error_tool", args);
        }
        
        // 执行一次成功
        ToolDefinition successTool = createSimpleTool("success_tool", "Success tool");
        CHECK_TRUE(manager.registerTool(successTool));
        manager.executeTool("success_tool", nlohmann::json{{"value", "test"}});
        
        // 检查统计
        auto errorStats = manager.getToolStats("error_tool");
        CHECK_TRUE(errorStats.has_value());
        CHECK_EQ(errorStats->callCount, 3);
        CHECK_EQ(errorStats->errorCount, 3);
        CHECK_EQ(errorStats->errorRate, 1.0);
        
        auto successStats = manager.getToolStats("success_tool");
        CHECK_TRUE(successStats.has_value());
        CHECK_EQ(successStats->callCount, 1);
        CHECK_EQ(successStats->errorCount, 0);
        CHECK_EQ(successStats->errorRate, 0.0);
    }});

    tests.push_back({"ToolStatistics_Reset", []() {
        ToolManager manager;
        auto tool = createAddTool();
        CHECK_TRUE(manager.registerTool(tool));
        
        // 执行工具
        nlohmann::json args = {{"a", 1}, {"b", 2}};
        manager.executeTool("add", args);
        
        // 检查统计存在
        auto stats1 = manager.getToolStats("add");
        CHECK_TRUE(stats1.has_value());
        CHECK_EQ(stats1->callCount, 1);
        
        // 重置统计
        manager.resetToolStats("add");
        
        // 检查统计已清除
        auto stats2 = manager.getToolStats("add");
        CHECK_FALSE(stats2.has_value());
    }});

    // ========== 新功能测试：ErrorHandler集成 ==========

    tests.push_back({"ErrorHandler_Integration", []() {
        ErrorHandler errorHandler;
        ToolManager manager(&errorHandler);
        
        // 注册一个会抛出异常的工具
        ToolDefinition errorTool;
        errorTool.name = "error_tool";
        errorTool.description = "Tool that throws errors";
        errorTool.parametersSchema = nlohmann::json{
            {"type", "object"},
            {"properties", nlohmann::json::object()},
            {"required", nlohmann::json::array()}
        };
        errorTool.handler = [](const nlohmann::json&) -> nlohmann::json {
            throw std::runtime_error("Test error");
        };
        
        CHECK_TRUE(manager.registerTool(errorTool));
        
        // 执行工具（应该失败，但会记录日志）
        nlohmann::json args = nlohmann::json::object();  // 确保是有效的JSON对象
        ErrorInfo error;
        auto result = manager.executeTool("error_tool", args, &error);
        
        CHECK_FALSE(result.has_value());
        CHECK_EQ(error.errorType, ErrorType::ServerError);
        // ErrorHandler应该已经记录了日志（我们无法直接验证，但不会崩溃）
    }});

    return mini_test::run(tests);
}

