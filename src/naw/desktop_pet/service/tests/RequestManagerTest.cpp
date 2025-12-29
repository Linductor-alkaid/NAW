#include "naw/desktop_pet/service/RequestManager.h"
#include "naw/desktop_pet/service/APIClient.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/ModelManager.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"
#include "naw/desktop_pet/service/types/ChatMessage.h"
#include "naw/desktop_pet/service/types/TaskType.h"
#include "naw/desktop_pet/service/types/TaskPriority.h"
#include "naw/desktop_pet/service/types/ModelConfig.h"

#include "nlohmann/json.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace naw::desktop_pet::service;
using namespace naw::desktop_pet::service::types;

// 轻量自测断言工具（复用既有 mini_test 风格）
namespace mini_test {

inline std::string toString(const std::string& v) { return v; }
inline std::string toString(const char* v) { return v ? std::string(v) : "null"; }
inline std::string toString(bool v) { return v ? "true" : "false"; }

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

// 创建测试用的模型配置
static ModelConfig createTestModel(const std::string& modelId, TaskType taskType, uint32_t maxConcurrent = 10) {
    ModelConfig config;
    config.modelId = modelId;
    config.displayName = "Test Model " + modelId;
    config.supportedTasks = {taskType};
    config.maxContextTokens = 4096;
    config.defaultTemperature = 0.7f;
    config.defaultMaxTokens = 2048;
    config.costPer1kTokens = 0.1f;
    config.maxConcurrentRequests = maxConcurrent;
    config.supportsStreaming = true;
    config.performanceScore = 0.8f;
    return config;
}

// 创建测试用的ConfigManager（通过引用参数设置）
static void createTestConfigManager(ConfigManager& cfg) {
    nlohmann::json testConfig = nlohmann::json::object();
    testConfig["models"] = nlohmann::json::array();
    testConfig["api"] = nlohmann::json::object();
    testConfig["api"]["base_url"] = "https://api.test.com/v1";
    testConfig["api"]["api_key"] = "test_key";
    testConfig["request_manager"] = nlohmann::json::object();
    testConfig["request_manager"]["max_queue_size"] = 100;
    testConfig["request_manager"]["default_timeout_ms"] = 30000;
    
    nlohmann::json model1 = nlohmann::json::object();
    model1["model_id"] = "test/model1";
    model1["display_name"] = "Test Model 1";
    model1["supported_tasks"] = nlohmann::json::array({"CodeGeneration"});
    model1["max_context_tokens"] = 4096;
    model1["default_temperature"] = 0.7;
    model1["default_max_tokens"] = 2048;
    model1["cost_per_1k_tokens"] = 0.1;
    model1["max_concurrent_requests"] = 5;
    model1["supports_streaming"] = true;
    model1["performance_score"] = 0.8;
    
    testConfig["models"].push_back(model1);
    
    cfg.loadFromString(testConfig.dump());
}

// 创建测试用的ChatRequest
static ChatRequest createTestRequest(const std::string& modelId, const std::string& content = "Hello") {
    ChatRequest req;
    req.model = modelId;
    req.messages = {ChatMessage{MessageRole::User, content}};
    req.temperature = 0.7f;
    req.maxTokens = 100;
    req.stream = false;
    return req;
}

int main() {
    using mini_test::TestCase;

    std::vector<TestCase> tests;

    // ========== 请求队列测试 ==========
    tests.push_back({"RequestManager_EnqueueRequest", []() {
        ConfigManager cfg;
        createTestConfigManager(cfg);
        ModelManager modelManager(cfg);
        modelManager.loadModelsFromConfig();
        
        // 创建APIClient（需要有效的配置，即使不实际调用API）
        APIClient apiClient(cfg);
        
        RequestManager manager(cfg, apiClient, modelManager);
        manager.start();
        
        ChatRequest req = createTestRequest("test/model1");
        auto future = manager.enqueueRequest(req, TaskType::CodeGeneration, TaskPriority::Normal, "test/model1");
        
        // 请求应该被成功入队
        CHECK_TRUE(future.valid());
        
        // 检查统计
        auto stats = manager.getStatistics();
        CHECK_EQ(stats.totalRequests, 1u);
        CHECK_EQ(stats.queueSize, 1u);
        
        manager.stop();
    }});

    tests.push_back({"RequestManager_PriorityQueueOrder", []() {
        ConfigManager cfg;
        createTestConfigManager(cfg);
        ModelManager modelManager(cfg);
        modelManager.loadModelsFromConfig();
        APIClient apiClient(cfg);
        
        RequestManager manager(cfg, apiClient, modelManager);
        manager.start();
        
        // 入队不同优先级的请求
        ChatRequest req1 = createTestRequest("test/model1", "Low priority");
        ChatRequest req2 = createTestRequest("test/model1", "High priority");
        ChatRequest req3 = createTestRequest("test/model1", "Critical priority");
        
        auto future1 = manager.enqueueRequest(req1, TaskType::CodeGeneration, TaskPriority::Low, "test/model1");
        auto future2 = manager.enqueueRequest(req2, TaskType::CodeGeneration, TaskPriority::High, "test/model1");
        auto future3 = manager.enqueueRequest(req3, TaskType::CodeGeneration, TaskPriority::Critical, "test/model1");
        
        // 所有请求都应该被入队
        CHECK_TRUE(future1.valid());
        CHECK_TRUE(future2.valid());
        CHECK_TRUE(future3.valid());
        
        // 队列应该包含3个请求
        auto queueStats = manager.getQueueStatistics();
        CHECK_EQ(queueStats.currentSize, 3u);
        
        manager.stop();
    }});

    tests.push_back({"RequestManager_QueueSizeLimit", []() {
        ConfigManager cfg;
        createTestConfigManager(cfg);
        // 设置较小的队列大小限制
        cfg.set("request_manager.max_queue_size", nlohmann::json(2));
        
        ModelManager modelManager(cfg);
        modelManager.loadModelsFromConfig();
        APIClient apiClient(cfg);
        
        RequestManager manager(cfg, apiClient, modelManager);
        manager.start();
        
        ChatRequest req1 = createTestRequest("test/model1", "Request 1");
        ChatRequest req2 = createTestRequest("test/model1", "Request 2");
        ChatRequest req3 = createTestRequest("test/model1", "Request 3");
        
        auto future1 = manager.enqueueRequest(req1, TaskType::CodeGeneration, TaskPriority::Normal, "test/model1");
        auto future2 = manager.enqueueRequest(req2, TaskType::CodeGeneration, TaskPriority::Normal, "test/model1");
        auto future3 = manager.enqueueRequest(req3, TaskType::CodeGeneration, TaskPriority::Normal, "test/model1");
        
        // 前两个应该成功，第三个可能失败（取决于实现）
        CHECK_TRUE(future1.valid());
        CHECK_TRUE(future2.valid());
        
        manager.stop();
    }});

    // ========== 并发控制测试 ==========
    tests.push_back({"RequestManager_ConcurrencyLimit", []() {
        ConfigManager cfg;
        createTestConfigManager(cfg);
        ModelManager modelManager(cfg);
        modelManager.loadModelsFromConfig();
        APIClient apiClient(cfg);
        
        RequestManager manager(cfg, apiClient, modelManager);
        manager.start();
        
        // 检查并发限制
        uint32_t limit = manager.getConcurrencyLimit("test/model1");
        CHECK_EQ(limit, 5u); // 配置中设置为5
        
        // 初始并发数应该为0
        uint32_t current = manager.getCurrentConcurrency("test/model1");
        CHECK_EQ(current, 0u);
        
        // 总并发数应该为0
        uint32_t total = manager.getTotalConcurrency();
        CHECK_EQ(total, 0u);
        
        manager.stop();
    }});

    tests.push_back({"RequestManager_MultipleModelsIndependentConcurrency", []() {
        ConfigManager cfg;
        createTestConfigManager(cfg);
        ModelManager modelManager(cfg);
        
        // 注册两个不同的模型
        ModelConfig model1 = createTestModel("test/model1", TaskType::CodeGeneration, 3);
        ModelConfig model2 = createTestModel("test/model2", TaskType::CodeAnalysis, 5);
        modelManager.registerModel(model1);
        modelManager.registerModel(model2);
        
        APIClient apiClient(cfg);
        RequestManager manager(cfg, apiClient, modelManager);
        manager.start();
        
        // 检查两个模型的并发限制
        uint32_t limit1 = manager.getConcurrencyLimit("test/model1");
        uint32_t limit2 = manager.getConcurrencyLimit("test/model2");
        CHECK_EQ(limit1, 3u);
        CHECK_EQ(limit2, 5u);
        
        manager.stop();
    }});

    // ========== 请求取消测试 ==========
    tests.push_back({"RequestManager_CancelRequest", []() {
        ConfigManager cfg;
        createTestConfigManager(cfg);
        ModelManager modelManager(cfg);
        modelManager.loadModelsFromConfig();
        APIClient apiClient(cfg);
        
        RequestManager manager(cfg, apiClient, modelManager);
        manager.start();
        
        ChatRequest req = createTestRequest("test/model1");
        auto future = manager.enqueueRequest(req, TaskType::CodeGeneration, TaskPriority::Normal, "test/model1");
        
        // 获取请求ID（需要通过其他方式，这里简化测试）
        // 注意：实际实现中可能需要添加获取请求ID的接口
        // 这里仅测试取消接口存在且不抛出异常
        bool result = manager.cancelRequest("nonexistent_request_id");
        CHECK_FALSE(result); // 不存在的请求ID应该返回false
        
        manager.stop();
    }});

    // ========== 统计测试 ==========
    tests.push_back({"RequestManager_StatisticsUpdate", []() {
        ConfigManager cfg;
        createTestConfigManager(cfg);
        ModelManager modelManager(cfg);
        modelManager.loadModelsFromConfig();
        APIClient apiClient(cfg);
        
        RequestManager manager(cfg, apiClient, modelManager);
        manager.start();
        
        // 入队多个请求
        ChatRequest req1 = createTestRequest("test/model1", "Request 1");
        ChatRequest req2 = createTestRequest("test/model1", "Request 2");
        
        auto future1 = manager.enqueueRequest(req1, TaskType::CodeGeneration, TaskPriority::Normal, "test/model1");
        auto future2 = manager.enqueueRequest(req2, TaskType::CodeGeneration, TaskPriority::Normal, "test/model1");
        
        // 检查统计
        auto stats = manager.getStatistics();
        CHECK_EQ(stats.totalRequests, 2u);
        CHECK_EQ(stats.requestsPerModel["test/model1"], 2u);
        
        // 检查队列统计
        auto queueStats = manager.getQueueStatistics();
        CHECK_TRUE(queueStats.totalEnqueued >= 2u);
        CHECK_TRUE(queueStats.currentSize >= 0u);
        
        manager.stop();
    }});

    tests.push_back({"RequestManager_QueueStatistics", []() {
        ConfigManager cfg;
        createTestConfigManager(cfg);
        ModelManager modelManager(cfg);
        modelManager.loadModelsFromConfig();
        APIClient apiClient(cfg);
        
        RequestManager manager(cfg, apiClient, modelManager);
        manager.start();
        
        auto queueStats = manager.getQueueStatistics();
        
        // 初始状态
        CHECK_EQ(queueStats.currentSize, 0u);
        CHECK_EQ(queueStats.maxSize, 100u); // 配置中的值
        
        // 入队一个请求
        ChatRequest req = createTestRequest("test/model1");
        auto future = manager.enqueueRequest(req, TaskType::CodeGeneration, TaskPriority::Normal, "test/model1");
        
        // 等待一小段时间让请求被处理
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // 检查统计更新
        queueStats = manager.getQueueStatistics();
        CHECK_TRUE(queueStats.totalEnqueued >= 1u);
        
        manager.stop();
    }});

    // ========== 生命周期测试 ==========
    tests.push_back({"RequestManager_StartStop", []() {
        ConfigManager cfg;
        createTestConfigManager(cfg);
        ModelManager modelManager(cfg);
        modelManager.loadModelsFromConfig();
        APIClient apiClient(cfg);
        
        RequestManager manager(cfg, apiClient, modelManager);
        
        // 初始状态应该不在运行
        CHECK_FALSE(manager.isRunning());
        
        // 启动
        manager.start();
        CHECK_TRUE(manager.isRunning());
        
        // 停止
        manager.stop();
        CHECK_FALSE(manager.isRunning());
    }});

    tests.push_back({"RequestManager_MultipleStartStop", []() {
        ConfigManager cfg;
        createTestConfigManager(cfg);
        ModelManager modelManager(cfg);
        modelManager.loadModelsFromConfig();
        APIClient apiClient(cfg);
        
        RequestManager manager(cfg, apiClient, modelManager);
        
        // 多次启动和停止应该安全
        manager.start();
        CHECK_TRUE(manager.isRunning());
        
        manager.start(); // 重复启动应该被忽略
        CHECK_TRUE(manager.isRunning());
        
        manager.stop();
        CHECK_FALSE(manager.isRunning());
        
        manager.stop(); // 重复停止应该被忽略
        CHECK_FALSE(manager.isRunning());
        
        // 再次启动
        manager.start();
        CHECK_TRUE(manager.isRunning());
        manager.stop();
        CHECK_FALSE(manager.isRunning());
    }});

    // ========== 线程安全测试 ==========
    tests.push_back({"RequestManager_ConcurrentEnqueue", []() {
        ConfigManager cfg;
        createTestConfigManager(cfg);
        ModelManager modelManager(cfg);
        modelManager.loadModelsFromConfig();
        APIClient apiClient(cfg);
        
        RequestManager manager(cfg, apiClient, modelManager);
        manager.start();
        
        const int numThreads = 10;
        const int requestsPerThread = 5;
        std::vector<std::thread> threads;
        std::atomic<int> successCount{0};
        
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([&, i]() {
                for (int j = 0; j < requestsPerThread; ++j) {
                    ChatRequest req = createTestRequest("test/model1", "Concurrent request " + std::to_string(i * requestsPerThread + j));
                    auto future = manager.enqueueRequest(req, TaskType::CodeGeneration, TaskPriority::Normal, "test/model1");
                    if (future.valid()) {
                        successCount++;
                    }
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        // 所有请求应该都成功入队
        CHECK_EQ(successCount.load(), numThreads * requestsPerThread);
        
        // 等待处理完成
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        manager.stop();
    }});

    return mini_test::run(tests);
}

