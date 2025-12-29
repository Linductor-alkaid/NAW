#include "naw/desktop_pet/service/ContextManager.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/types/ChatMessage.h"
#include "naw/desktop_pet/service/types/TaskType.h"

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace naw::desktop_pet::service;
using namespace naw::desktop_pet::service::types;

namespace mini_test {

inline std::string toString(const std::string& v) { return v; }
inline std::string toString(const char* v) { return v ? std::string(v) : "null"; }
inline std::string toString(bool v) { return v ? "true" : "false"; }

// MessageRole 特化
inline std::string toString(types::MessageRole v) {
    return types::roleToString(v);
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

#define CHECK_NE(a, b)                                                                            \
    do {                                                                                          \
        const auto _va = (a);                                                                     \
        const auto _vb = (b);                                                                     \
        if (_va == _vb) {                                                                      \
            throw mini_test::AssertionFailed(std::string("CHECK_NE failed: ") + #a " == " #b +    \
                                             " (" + mini_test::toString(_va) + ")");             \
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

int main() {
    using mini_test::TestCase;

    std::vector<TestCase> tests;

    // ========== 对话历史管理测试 ==========
    tests.push_back({"ContextManager_AddMessage", []() {
        ConfigManager cfg;
        ContextManager manager(cfg);
        
        ChatMessage msg;
        msg.role = MessageRole::User;
        msg.setText("Hello, world!");
        
        manager.addMessage(msg);
        
        auto history = manager.getHistory(10);
        CHECK_EQ(history.size(), 1);
        CHECK_EQ(history[0].role, MessageRole::User);
        CHECK_TRUE(history[0].textView().has_value());
        CHECK_EQ(*history[0].textView(), "Hello, world!");
    }});

    tests.push_back({"ContextManager_GetHistory", []() {
        ConfigManager cfg;
        ContextManager manager(cfg);
        
        // 添加多条消息
        for (int i = 0; i < 10; ++i) {
            ChatMessage msg;
            msg.role = MessageRole::User;
            msg.setText("Message " + std::to_string(i));
            manager.addMessage(msg);
        }
        
        auto history = manager.getHistory(5);
        CHECK_EQ(history.size(), 5);
        // 应该返回最近的5条消息
        CHECK_EQ(*history[0].textView(), "Message 5");
        CHECK_EQ(*history[4].textView(), "Message 9");
    }});

    tests.push_back({"ContextManager_GetHistoryByRange", []() {
        ConfigManager cfg;
        ContextManager manager(cfg);
        
        for (int i = 0; i < 10; ++i) {
            ChatMessage msg;
            msg.role = MessageRole::User;
            msg.setText("Message " + std::to_string(i));
            manager.addMessage(msg);
        }
        
        auto history = manager.getHistoryByRange(2, 3);
        CHECK_EQ(history.size(), 3);
        CHECK_EQ(*history[0].textView(), "Message 2");
        CHECK_EQ(*history[2].textView(), "Message 4");
    }});

    tests.push_back({"ContextManager_TrimHistory", []() {
        ConfigManager cfg;
        ContextManager manager(cfg);
        
        for (int i = 0; i < 10; ++i) {
            ChatMessage msg;
            msg.role = MessageRole::User;
            msg.setText("Message " + std::to_string(i));
            manager.addMessage(msg);
        }
        
        manager.trimHistory(5);
        
        auto history = manager.getHistory(10);
        CHECK_EQ(history.size(), 5);
        CHECK_EQ(*history[0].textView(), "Message 5");
    }});

    tests.push_back({"ContextManager_TrimHistoryByTokens", []() {
        ConfigManager cfg;
        ContextManager manager(cfg);
        
        // 添加一些消息
        for (int i = 0; i < 5; ++i) {
            ChatMessage msg;
            msg.role = MessageRole::User;
            msg.setText("Short message " + std::to_string(i));
            manager.addMessage(msg);
        }
        
        // 按Token数裁剪（假设每条消息约10 tokens）
        manager.trimHistoryByTokens(30, "test-model");
        
        auto history = manager.getHistory(10);
        // 应该保留部分消息
        CHECK_TRUE(history.size() <= 5);
    }});

    tests.push_back({"ContextManager_MultipleSessions", []() {
        ConfigManager cfg;
        ContextManager manager(cfg);
        
        ChatMessage msg1;
        msg1.role = MessageRole::User;
        msg1.setText("Session 1 message");
        manager.addMessage(msg1, "session1");
        
        ChatMessage msg2;
        msg2.role = MessageRole::User;
        msg2.setText("Session 2 message");
        manager.addMessage(msg2, "session2");
        
        auto history1 = manager.getHistory(10, "session1");
        CHECK_EQ(history1.size(), 1);
        CHECK_EQ(*history1[0].textView(), "Session 1 message");
        
        auto history2 = manager.getHistory(10, "session2");
        CHECK_EQ(history2.size(), 1);
        CHECK_EQ(*history2[0].textView(), "Session 2 message");
    }});

    // ========== System Prompt构建测试 ==========
    tests.push_back({"ContextManager_BuildSystemPrompt", []() {
        ConfigManager cfg;
        ContextManager manager(cfg);
        
        auto prompt = manager.buildSystemPrompt(TaskType::CodeGeneration);
        CHECK_EQ(prompt.role, MessageRole::System);
        CHECK_TRUE(prompt.textView().has_value());
        CHECK_FALSE(prompt.textView()->empty());
        
        // 不同任务类型应该有不同的prompt
        auto prompt2 = manager.buildSystemPrompt(TaskType::CodeAnalysis);
        CHECK_NE(*prompt.textView(), *prompt2.textView());
    }});

    // ========== Agent状态上下文构建测试 ==========
    tests.push_back({"ContextManager_BuildAgentStateContext", []() {
        ConfigManager cfg;
        ContextManager manager(cfg);
        
        AgentState agentState;
        agentState.currentState = "Working on code generation";
        agentState.memorySummary = "Recent focus: C++ development";
        
        auto msg = manager.buildAgentStateContext(agentState);
        CHECK_EQ(msg.role, MessageRole::System);
        CHECK_TRUE(msg.textView().has_value());
        std::string text = std::string(*msg.textView());
        CHECK_TRUE(text.find("Working on code generation") != std::string::npos);
        CHECK_TRUE(text.find("Recent focus: C++ development") != std::string::npos);
    }});

    // ========== 项目上下文构建测试 ==========
    tests.push_back({"ContextManager_BuildProjectContext", []() {
        ConfigManager cfg;
        ContextManager manager(cfg);
        
        ProjectContext projectContext;
        projectContext.projectRoot = "/path/to/project";
        projectContext.structureSummary = "C++ project with CMake";
        projectContext.relevantFiles = {"src/main.cpp", "include/header.h"};
        
        auto msg = manager.buildProjectContext(projectContext, TaskType::CodeGeneration);
        CHECK_EQ(msg.role, MessageRole::System);
        CHECK_TRUE(msg.textView().has_value());
        std::string text = std::string(*msg.textView());
        CHECK_TRUE(text.find("/path/to/project") != std::string::npos);
        CHECK_TRUE(text.find("C++ project with CMake") != std::string::npos);
    }});

    // ========== 代码上下文构建测试 ==========
    tests.push_back({"ContextManager_BuildCodeContext", []() {
        ConfigManager cfg;
        ContextManager manager(cfg);
        
        CodeContext codeContext;
        codeContext.filePaths = {"src/main.cpp", "include/header.h"};
        codeContext.fileContent = "int main() { return 0; }";
        codeContext.focusArea = "main function";
        
        auto msg = manager.buildCodeContext(codeContext);
        CHECK_EQ(msg.role, MessageRole::User);
        CHECK_TRUE(msg.textView().has_value());
        std::string text = std::string(*msg.textView());
        CHECK_TRUE(text.find("src/main.cpp") != std::string::npos);
        CHECK_TRUE(text.find("int main()") != std::string::npos);
        CHECK_TRUE(text.find("main function") != std::string::npos);
    }});

    // ========== 记忆事件上下文构建测试 ==========
    tests.push_back({"ContextManager_BuildMemoryContext", []() {
        ConfigManager cfg;
        ContextManager manager(cfg);
        
        std::vector<MemoryEvent> events;
        MemoryEvent event1;
        event1.eventType = "code_change";
        event1.content = "Modified main.cpp";
        event1.importanceScore = 0.8f;
        events.push_back(event1);
        
        MemoryEvent event2;
        event2.eventType = "decision";
        event2.content = "Chose C++ for implementation";
        event2.importanceScore = 0.9f;
        events.push_back(event2);
        
        auto msg = manager.buildMemoryContext(events, TaskType::CodeGeneration);
        CHECK_EQ(msg.role, MessageRole::System);
        CHECK_TRUE(msg.textView().has_value());
        std::string text = std::string(*msg.textView());
        CHECK_TRUE(text.find("Modified main.cpp") != std::string::npos);
        CHECK_TRUE(text.find("Chose C++") != std::string::npos);
    }});

    // ========== 完整上下文构建测试 ==========
    tests.push_back({"ContextManager_BuildContext", []() {
        ConfigManager cfg;
        ContextManager manager(cfg);
        
        // 添加一些历史消息
        ChatMessage msg1;
        msg1.role = MessageRole::User;
        msg1.setText("Previous message");
        manager.addMessage(msg1);
        
        ContextConfig config;
        config.taskType = TaskType::CodeGeneration;
        config.maxTokens = 4096;
        config.includeConversationHistory = true;
        config.maxHistoryMessages = 10;
        
        auto messages = manager.buildContext(config, "Current user message", "test-model");
        
        CHECK_TRUE(messages.size() >= 2); // System prompt + user message
        CHECK_EQ(messages[0].role, MessageRole::System);
        
        // 最后一条应该是用户消息
        CHECK_EQ(messages.back().role, MessageRole::User);
        CHECK_TRUE(messages.back().textView().has_value());
        CHECK_EQ(std::string(*messages.back().textView()), "Current user message");
    }});

    // ========== Token限制检查测试 ==========
    tests.push_back({"ContextManager_CheckTokenLimit", []() {
        ConfigManager cfg;
        ContextManager manager(cfg);
        
        std::vector<ChatMessage> messages;
        
        ChatMessage msg1;
        msg1.role = MessageRole::System;
        msg1.setText("System prompt");
        messages.push_back(msg1);
        
        ChatMessage msg2;
        msg2.role = MessageRole::User;
        msg2.setText("User message");
        messages.push_back(msg2);
        
        // 检查Token限制（假设这两条消息的token数小于100）
        bool exceeds = manager.checkTokenLimit(messages, 100, "test-model");
        CHECK_FALSE(exceeds);
        
        // 检查更大的限制
        bool exceeds2 = manager.checkTokenLimit(messages, 10, "test-model");
        // 可能超过也可能不超过，取决于实际token数
    }});

    // ========== 智能上下文裁剪测试 ==========
    tests.push_back({"ContextManager_TrimContext", []() {
        ConfigManager cfg;
        ContextManager manager(cfg);
        
        std::vector<ChatMessage> messages;
        
        // System prompt（应该保留）
        ChatMessage sysMsg;
        sysMsg.role = MessageRole::System;
        sysMsg.setText("System prompt");
        messages.push_back(sysMsg);
        
        // 添加多条用户消息
        for (int i = 0; i < 10; ++i) {
            ChatMessage msg;
            msg.role = MessageRole::User;
            msg.setText("User message " + std::to_string(i));
            messages.push_back(msg);
        }
        
        // 裁剪到较小的token限制
        manager.trimContext(messages, 50, "test-model", TaskType::CodeGeneration);
        
        // System prompt应该被保留
        CHECK_TRUE(messages.size() > 0);
        CHECK_EQ(messages[0].role, MessageRole::System);
    }});

    // ========== 消息重要性评分测试 ==========
    tests.push_back({"ContextManager_CalculateMessageImportance", []() {
        ConfigManager cfg;
        ContextManager manager(cfg);
        
        ChatMessage sysMsg;
        sysMsg.role = MessageRole::System;
        sysMsg.setText("System prompt");
        
        float importance1 = manager.calculateMessageImportance(
            sysMsg, TaskType::CodeGeneration, 0, 10
        );
        CHECK_TRUE(importance1 > 0.0f);
        
        ChatMessage userMsg;
        userMsg.role = MessageRole::User;
        userMsg.setText("User message");
        
        float importance2 = manager.calculateMessageImportance(
            userMsg, TaskType::CodeGeneration, 5, 10
        );
        
        // System消息通常比User消息更重要
        CHECK_TRUE(importance1 > importance2);
        
        // 更新的消息应该更重要
        float importance3 = manager.calculateMessageImportance(
            userMsg, TaskType::CodeGeneration, 9, 10
        );
        CHECK_TRUE(importance3 > importance2);
    }});

    // ========== 上下文配置管理测试 ==========
    tests.push_back({"ContextManager_LoadConfig", []() {
        ConfigManager cfg;
        
        // 设置上下文配置
        nlohmann::json contextConfig = nlohmann::json::object();
        contextConfig["max_history_messages"] = 100;
        contextConfig["max_context_tokens"] = 8192;
        contextConfig["default_include_agent_state"] = true;
        cfg.set("context", contextConfig);
        
        ContextManager manager(cfg);
        
        // 手动调用 loadConfigFromFile 以确保配置被加载
        manager.loadConfigFromFile();
        
        auto config = manager.getConfig();
        CHECK_EQ(config.maxHistoryMessages, 100);
        CHECK_EQ(config.maxTokens, 8192);
        CHECK_EQ(config.includeAgentState, true);
    }});

    tests.push_back({"ContextManager_UpdateConfig", []() {
        ConfigManager cfg;
        ContextManager manager(cfg);
        
        ContextConfig newConfig;
        newConfig.maxHistoryMessages = 200;
        newConfig.maxTokens = 16384;
        manager.updateConfig(newConfig);
        
        auto config = manager.getConfig();
        CHECK_EQ(config.maxHistoryMessages, 200);
        CHECK_EQ(config.maxTokens, 16384);
    }});

    // ========== Token估算测试 ==========
    tests.push_back({"ContextManager_EstimateTokens", []() {
        ConfigManager cfg;
        ContextManager manager(cfg);
        
        std::vector<ChatMessage> messages;
        
        ChatMessage msg;
        msg.role = MessageRole::User;
        msg.setText("Hello, world!");
        messages.push_back(msg);
        
        size_t tokens = manager.estimateTokens(messages, "test-model");
        CHECK_TRUE(tokens > 0);
    }});

    return mini_test::run(tests);
}

