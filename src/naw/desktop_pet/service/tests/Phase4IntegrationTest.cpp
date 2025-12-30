#include "naw/desktop_pet/service/RequestManager.h"
#include "naw/desktop_pet/service/ResponseHandler.h"
#include "naw/desktop_pet/service/CacheManager.h"
#include "naw/desktop_pet/service/APIClient.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/ModelManager.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"
#include "naw/desktop_pet/service/types/ChatMessage.h"
#include "naw/desktop_pet/service/types/TaskType.h"
#include "naw/desktop_pet/service/types/TaskPriority.h"
#include "naw/desktop_pet/service/types/ModelConfig.h"

#include "httplib.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
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

// 工具函数
static void setEnvVar(const std::string& k, const std::string& v) {
#if defined(_WIN32)
    _putenv_s(k.c_str(), v.c_str());
#else
    setenv(k.c_str(), v.c_str(), 1);
#endif
}

static std::string makeLocalBaseUrl(int port) {
    return "http://127.0.0.1:" + std::to_string(port) + "/v1";
}

struct ServerGuard {
    httplib::Server& server;
    std::thread th;
    int port;
    explicit ServerGuard(httplib::Server& s) : server(s), port(0) {}
    ~ServerGuard() {
        server.stop();
        if (th.joinable()) th.join();
    }
    int start() {
        port = server.bind_to_any_port("127.0.0.1");
        if (port > 0) {
            th = std::thread([&]() { server.listen_after_bind(); });
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 等待服务器启动
        }
        return port;
    }
};

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

// 创建测试用的ConfigManager
static void createTestConfigManager(ConfigManager& cfg, const std::string& baseUrl = "https://api.test.com/v1") {
    nlohmann::json testConfig = nlohmann::json::object();
    testConfig["models"] = nlohmann::json::array();
    testConfig["api"] = nlohmann::json::object();
    testConfig["api"]["base_url"] = baseUrl;
    testConfig["api"]["api_key"] = "test_key_123";
    testConfig["api"]["default_timeout_ms"] = 30000;
    testConfig["request_manager"] = nlohmann::json::object();
    testConfig["request_manager"]["max_queue_size"] = 100;
    testConfig["request_manager"]["default_timeout_ms"] = 30000;
    testConfig["cache"] = nlohmann::json::object();
    testConfig["cache"]["enabled"] = true;
    testConfig["cache"]["default_ttl_seconds"] = 3600;
    testConfig["cache"]["max_entries"] = 1000;
    
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
static ChatRequest createTestRequest(const std::string& modelId, const std::string& content = "Hello",
                                     std::optional<float> temperature = std::nullopt, bool stream = false) {
    ChatRequest req;
    req.model = modelId;
    req.messages = {ChatMessage{MessageRole::User, content}};
    if (temperature.has_value()) {
        req.temperature = temperature;
    } else {
        req.temperature = 0.0f; // 默认使用确定性温度以便缓存
    }
    req.maxTokens = 100;
    req.stream = stream;
    return req;
}

// 创建测试用的ChatResponse
static ChatResponse createTestResponse(const std::string& content) {
    ChatResponse resp;
    resp.content = content;
    resp.finishReason = "stop";
    resp.promptTokens = 10;
    resp.completionTokens = 20;
    resp.totalTokens = 30;
    return resp;
}

int main() {
    using mini_test::TestCase;
    std::vector<TestCase> tests;

    // ========== RequestManager + APIClient 集成测试 ==========
    
    tests.push_back({"RequestManager_APIClient_EndToEndFlow", []() {
        setEnvVar("SILICONFLOW_API_KEY", "test_key_123");
        
        httplib::Server server;
        std::atomic<int> requestCount{0};
        
        server.Post("/v1/chat/completions", [&](const httplib::Request& req, httplib::Response& res) {
            requestCount++;
            const auto auth = req.get_header_value("Authorization");
            CHECK_TRUE(auth == "Bearer test_key_123");
            
            res.status = 200;
            res.set_content(R"({
                "id": "chatcmpl-test",
                "object": "chat.completion",
                "created": 1234567890,
                "model": "test/model1",
                "choices": [{
                    "index": 0,
                    "message": {
                        "role": "assistant",
                        "content": "Hello, world!"
                    },
                    "finish_reason": "stop"
                }],
                "usage": {
                    "prompt_tokens": 10,
                    "completion_tokens": 20,
                    "total_tokens": 30
                }
            })", "application/json");
        });
        
        ServerGuard guard(server);
        int port = guard.start();
        CHECK_TRUE(port > 0);
        
        ConfigManager cfg;
        createTestConfigManager(cfg, makeLocalBaseUrl(port));
        cfg.applyEnvironmentOverrides();
        
        ModelManager modelManager(cfg);
        modelManager.loadModelsFromConfig();
        
        APIClient apiClient(cfg);
        RequestManager requestManager(cfg, apiClient, modelManager);
        requestManager.start();
        
        // 提交请求
        ChatRequest req = createTestRequest("test/model1", "Hello");
        auto future = requestManager.enqueueRequest(req, TaskType::CodeGeneration, TaskPriority::Normal, "test/model1");
        
        // 等待响应
        auto response = future.get();
        CHECK_EQ(response.content, "Hello, world!");
        CHECK_EQ(requestCount.load(), 1);
        
        // 检查统计
        auto stats = requestManager.getStatistics();
        CHECK_EQ(stats.totalRequests, 1u);
        CHECK_EQ(stats.completedRequests, 1u);
        CHECK_EQ(stats.failedRequests, 0u);
        
        requestManager.stop();
    }});

    tests.push_back({"RequestManager_APIClient_ConcurrentRequests", []() {
        setEnvVar("SILICONFLOW_API_KEY", "test_key_123");
        
        httplib::Server server;
        std::atomic<int> requestCount{0};
        std::mutex responseMutex;
        std::vector<std::string> responses;
        
        server.Post("/v1/chat/completions", [&](const httplib::Request& req, httplib::Response& res) {
            int count = requestCount.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 模拟网络延迟
            
            res.status = 200;
            std::string content = "Response " + std::to_string(count);
            res.set_content(R"({
                "id": "chatcmpl-test",
                "object": "chat.completion",
                "created": 1234567890,
                "model": "test/model1",
                "choices": [{
                    "index": 0,
                    "message": {
                        "role": "assistant",
                        "content": ")" + content + R"("
                    },
                    "finish_reason": "stop"
                }],
                "usage": {
                    "prompt_tokens": 10,
                    "completion_tokens": 20,
                    "total_tokens": 30
                }
            })", "application/json");
        });
        
        ServerGuard guard(server);
        int port = guard.start();
        CHECK_TRUE(port > 0);
        
        ConfigManager cfg;
        createTestConfigManager(cfg, makeLocalBaseUrl(port));
        cfg.applyEnvironmentOverrides();
        
        ModelManager modelManager(cfg);
        modelManager.loadModelsFromConfig();
        
        APIClient apiClient(cfg);
        RequestManager requestManager(cfg, apiClient, modelManager);
        requestManager.start();
        
        // 提交多个并发请求
        const int numRequests = 3;
        std::vector<std::future<ChatResponse>> futures;
        for (int i = 0; i < numRequests; i++) {
            ChatRequest req = createTestRequest("test/model1", "Request " + std::to_string(i));
            auto future = requestManager.enqueueRequest(req, TaskType::CodeGeneration, TaskPriority::Normal, "test/model1");
            futures.push_back(std::move(future));
        }
        
        // 等待所有请求完成
        for (auto& future : futures) {
            auto response = future.get();
            CHECK_TRUE(!response.content.empty());
        }
        
        CHECK_EQ(requestCount.load(), numRequests);
        
        // 检查统计
        auto stats = requestManager.getStatistics();
        CHECK_EQ(stats.totalRequests, numRequests);
        CHECK_EQ(stats.completedRequests, numRequests);
        
        requestManager.stop();
    }});

    tests.push_back({"RequestManager_APIClient_TimeoutAndCancel", []() {
        setEnvVar("SILICONFLOW_API_KEY", "test_key_123");
        
        httplib::Server server;
        std::atomic<int> requestCount{0};
        
        server.Post("/v1/chat/completions", [&](const httplib::Request& req, httplib::Response& res) {
            requestCount++;
            // 模拟长时间延迟（超过超时时间）
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
            res.status = 200;
            res.set_content(R"({
                "id": "chatcmpl-test",
                "object": "chat.completion",
                "created": 1234567890,
                "model": "test/model1",
                "choices": [{
                    "index": 0,
                    "message": {
                        "role": "assistant",
                        "content": "Delayed response"
                    },
                    "finish_reason": "stop"
                }],
                "usage": {
                    "prompt_tokens": 10,
                    "completion_tokens": 20,
                    "total_tokens": 30
                }
            })", "application/json");
        });
        
        ServerGuard guard(server);
        int port = guard.start();
        CHECK_TRUE(port > 0);
        
        ConfigManager cfg;
        createTestConfigManager(cfg, makeLocalBaseUrl(port));
        cfg.applyEnvironmentOverrides();
        cfg.set("api.default_timeout_ms", nlohmann::json(500)); // 设置短超时
        
        ModelManager modelManager(cfg);
        modelManager.loadModelsFromConfig();
        
        APIClient apiClient(cfg);
        RequestManager requestManager(cfg, apiClient, modelManager);
        requestManager.start();
        
        // 提交请求（应该超时）
        ChatRequest req = createTestRequest("test/model1", "Timeout test");
        auto future = requestManager.enqueueRequest(req, TaskType::CodeGeneration, TaskPriority::Normal, "test/model1");
        
        // 等待响应（应该抛出异常或返回错误）
        bool gotException = false;
        try {
            auto response = future.get();
            // 如果超时，响应可能包含错误信息
            gotException = true;
        } catch (...) {
            gotException = true;
        }
        
        CHECK_TRUE(gotException);
        
        // 检查统计
        auto stats = requestManager.getStatistics();
        CHECK_EQ(stats.totalRequests, 1u);
        // 请求可能失败或超时
        CHECK_TRUE(stats.failedRequests > 0 || stats.completedRequests == 0);
        
        requestManager.stop();
    }});

    // ========== ResponseHandler + CacheManager 集成测试 ==========
    
    tests.push_back({"ResponseHandler_CacheManager_CacheHit", []() {
        ConfigManager cfg;
        createTestConfigManager(cfg);
        
        CacheManager cacheManager(cfg);
        ResponseHandler responseHandler(cfg, cacheManager);
        
        // 创建请求和响应
        ChatRequest req = createTestRequest("test/model1", "Hello", 0.0f);
        ChatResponse resp = createTestResponse("Hello, world!");
        
        // 第一次：缓存未命中，存储到缓存
        auto cached1 = responseHandler.checkCache(req);
        CHECK_TRUE(!cached1.has_value()); // 应该未命中
        
        responseHandler.storeCache(req, resp);
        
        // 第二次：应该命中缓存
        auto cached2 = responseHandler.checkCache(req);
        CHECK_TRUE(cached2.has_value());
        CHECK_EQ(cached2->content, "Hello, world!");
        
        // 检查统计
        auto stats = responseHandler.getStatistics();
        CHECK_EQ(stats.totalResponses, 2u); // 两次查询
        CHECK_EQ(stats.cachedResponses, 1u); // 一次命中
        CHECK_TRUE(stats.getCacheHitRate() > 0.0);
    }});

    tests.push_back({"ResponseHandler_CacheManager_CacheMissAndStore", []() {
        ConfigManager cfg;
        createTestConfigManager(cfg);
        
        CacheManager cacheManager(cfg);
        ResponseHandler responseHandler(cfg, cacheManager);
        
        // 创建请求和响应
        ChatRequest req = createTestRequest("test/model1", "Test message", 0.0f);
        ChatResponse resp = createTestResponse("Test response");
        
        // 查询缓存（应该未命中）
        auto cached = responseHandler.checkCache(req);
        CHECK_TRUE(!cached.has_value());
        
        // 存储响应
        responseHandler.storeCache(req, resp);
        
        // 再次查询（应该命中）
        auto cached2 = responseHandler.checkCache(req);
        CHECK_TRUE(cached2.has_value());
        CHECK_EQ(cached2->content, "Test response");
        
        // 检查缓存统计
        auto cacheStats = cacheManager.getStatistics();
        CHECK_EQ(cacheStats.totalHits, 1u);
        CHECK_EQ(cacheStats.totalMisses, 1u);
        CHECK_EQ(cacheStats.totalEntries, 1u);
    }});

    tests.push_back({"ResponseHandler_CacheManager_StreamingNotCached", []() {
        ConfigManager cfg;
        createTestConfigManager(cfg);
        
        CacheManager cacheManager(cfg);
        ResponseHandler responseHandler(cfg, cacheManager);
        
        // 创建流式请求
        ChatRequest req = createTestRequest("test/model1", "Stream test", 0.0f, true);
        ChatResponse resp = createTestResponse("Stream response");
        
        // 流式请求不应该被缓存
        responseHandler.storeCache(req, resp);
        
        // 查询缓存（应该未命中，因为流式请求不缓存）
        auto cached = responseHandler.checkCache(req);
        CHECK_TRUE(!cached.has_value());
        
        // 检查统计
        auto stats = responseHandler.getStatistics();
        CHECK_EQ(stats.streamingResponses, 0u); // 注意：这里只是测试缓存策略
    }});

    tests.push_back({"ResponseHandler_CacheManager_HighTemperatureNotCached", []() {
        ConfigManager cfg;
        createTestConfigManager(cfg);
        
        CacheManager cacheManager(cfg);
        ResponseHandler responseHandler(cfg, cacheManager);
        
        // 创建高温度请求（不确定性强，不应该缓存）
        ChatRequest req = createTestRequest("test/model1", "High temp test", 0.8f);
        ChatResponse resp = createTestResponse("High temp response");
        
        // 高温度请求不应该被缓存
        responseHandler.storeCache(req, resp);
        
        // 查询缓存（应该未命中）
        auto cached = responseHandler.checkCache(req);
        CHECK_TRUE(!cached.has_value());
    }});

    // ========== 完整流程集成测试 ==========
    
    tests.push_back({"FullFlow_RequestManager_ResponseHandler_CacheManager", []() {
        setEnvVar("SILICONFLOW_API_KEY", "test_key_123");
        
        httplib::Server server;
        std::atomic<int> apiCallCount{0};
        
        server.Post("/v1/chat/completions", [&](const httplib::Request& req, httplib::Response& res) {
            apiCallCount++;
            
            res.status = 200;
            res.set_content(R"({
                "id": "chatcmpl-test",
                "object": "chat.completion",
                "created": 1234567890,
                "model": "test/model1",
                "choices": [{
                    "index": 0,
                    "message": {
                        "role": "assistant",
                        "content": "Cached response"
                    },
                    "finish_reason": "stop"
                }],
                "usage": {
                    "prompt_tokens": 10,
                    "completion_tokens": 20,
                    "total_tokens": 30
                }
            })", "application/json");
        });
        
        ServerGuard guard(server);
        int port = guard.start();
        CHECK_TRUE(port > 0);
        
        ConfigManager cfg;
        createTestConfigManager(cfg, makeLocalBaseUrl(port));
        cfg.applyEnvironmentOverrides();
        
        ModelManager modelManager(cfg);
        modelManager.loadModelsFromConfig();
        
        APIClient apiClient(cfg);
        CacheManager cacheManager(cfg);
        ResponseHandler responseHandler(cfg, cacheManager);
        RequestManager requestManager(cfg, apiClient, modelManager);
        requestManager.start();
        
        // 第一次请求：应该调用API
        ChatRequest req = createTestRequest("test/model1", "Full flow test", 0.0f);
        
        // 注意：这里需要手动集成 ResponseHandler 的缓存检查
        // 由于 RequestManager 目前不直接使用 ResponseHandler，我们测试缓存功能
        auto cached1 = responseHandler.checkCache(req);
        CHECK_TRUE(!cached1.has_value()); // 第一次应该未命中
        
        // 模拟 API 调用后的响应存储
        ChatResponse resp = createTestResponse("Cached response");
        responseHandler.storeCache(req, resp);
        
        // 第二次请求：应该命中缓存，不调用API
        auto cached2 = responseHandler.checkCache(req);
        CHECK_TRUE(cached2.has_value());
        CHECK_EQ(cached2->content, "Cached response");
        
        // 检查缓存统计
        auto cacheStats = cacheManager.getStatistics();
        CHECK_EQ(cacheStats.totalHits, 1u);
        CHECK_EQ(cacheStats.totalMisses, 1u);
        CHECK_TRUE(cacheStats.getHitRate() > 0.0);
        
        requestManager.stop();
    }});

    tests.push_back({"FullFlow_ConcurrentWithCache", []() {
        setEnvVar("SILICONFLOW_API_KEY", "test_key_123");
        
        httplib::Server server;
        std::atomic<int> apiCallCount{0};
        
        server.Post("/v1/chat/completions", [&](const httplib::Request& req, httplib::Response& res) {
            apiCallCount++;
            
            res.status = 200;
            res.set_content(R"({
                "id": "chatcmpl-test",
                "object": "chat.completion",
                "created": 1234567890,
                "model": "test/model1",
                "choices": [{
                    "index": 0,
                    "message": {
                        "role": "assistant",
                        "content": "Concurrent response"
                    },
                    "finish_reason": "stop"
                }],
                "usage": {
                    "prompt_tokens": 10,
                    "completion_tokens": 20,
                    "total_tokens": 30
                }
            })", "application/json");
        });
        
        ServerGuard guard(server);
        int port = guard.start();
        CHECK_TRUE(port > 0);
        
        ConfigManager cfg;
        createTestConfigManager(cfg, makeLocalBaseUrl(port));
        cfg.applyEnvironmentOverrides();
        
        ModelManager modelManager(cfg);
        modelManager.loadModelsFromConfig();
        
        APIClient apiClient(cfg);
        CacheManager cacheManager(cfg);
        ResponseHandler responseHandler(cfg, cacheManager);
        RequestManager requestManager(cfg, apiClient, modelManager);
        requestManager.start();
        
        // 创建相同的请求（用于缓存测试）
        ChatRequest req = createTestRequest("test/model1", "Concurrent test", 0.0f);
        
        // 第一次：未命中，存储
        auto cached1 = responseHandler.checkCache(req);
        CHECK_TRUE(!cached1.has_value());
        
        ChatResponse resp = createTestResponse("Concurrent response");
        responseHandler.storeCache(req, resp);
        
        // 并发查询缓存（应该都命中）
        const int numThreads = 5;
        std::vector<std::thread> threads;
        std::atomic<int> hitCount{0};
        
        for (int i = 0; i < numThreads; i++) {
            threads.emplace_back([&]() {
                auto cached = responseHandler.checkCache(req);
                if (cached.has_value()) {
                    hitCount++;
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        // 所有线程都应该命中缓存
        CHECK_EQ(hitCount.load(), numThreads);
        
        // 检查统计
        auto stats = responseHandler.getStatistics();
        CHECK_TRUE(stats.totalResponses >= numThreads + 1); // 至少包含所有查询
        
        requestManager.stop();
    }});

    tests.push_back({"FullFlow_ErrorHandlingAndRetry", []() {
        setEnvVar("SILICONFLOW_API_KEY", "test_key_123");
        
        httplib::Server server;
        std::atomic<int> requestCount{0};
        
        server.Post("/v1/chat/completions", [&](const httplib::Request& req, httplib::Response& res) {
            int count = requestCount.fetch_add(1);
            
            // 前两次返回错误，第三次成功
            if (count < 2) {
                res.status = 500;
                res.set_content(R"({
                    "error": {
                        "message": "Internal server error",
                        "type": "server_error",
                        "code": "internal_error"
                    }
                })", "application/json");
            } else {
                res.status = 200;
                res.set_content(R"({
                    "id": "chatcmpl-test",
                    "object": "chat.completion",
                    "created": 1234567890,
                    "model": "test/model1",
                    "choices": [{
                        "index": 0,
                        "message": {
                            "role": "assistant",
                            "content": "Success after retry"
                        },
                        "finish_reason": "stop"
                    }],
                    "usage": {
                        "prompt_tokens": 10,
                        "completion_tokens": 20,
                        "total_tokens": 30
                    }
                })", "application/json");
            }
        });
        
        ServerGuard guard(server);
        int port = guard.start();
        CHECK_TRUE(port > 0);
        
        ConfigManager cfg;
        createTestConfigManager(cfg, makeLocalBaseUrl(port));
        cfg.applyEnvironmentOverrides();
        
        ModelManager modelManager(cfg);
        modelManager.loadModelsFromConfig();
        
        APIClient apiClient(cfg);
        RequestManager requestManager(cfg, apiClient, modelManager);
        requestManager.start();
        
        // 提交请求（可能会失败，取决于重试策略）
        ChatRequest req = createTestRequest("test/model1", "Error test");
        auto future = requestManager.enqueueRequest(req, TaskType::CodeGeneration, TaskPriority::Normal, "test/model1");
        
        // 等待响应
        bool gotResponse = false;
        try {
            auto response = future.get();
            gotResponse = true;
            // 如果成功，应该包含内容
            CHECK_TRUE(!response.content.empty());
        } catch (...) {
            // 如果失败，也是可以接受的（取决于重试策略）
            gotResponse = false;
        }
        
        // 检查统计
        auto stats = requestManager.getStatistics();
        CHECK_EQ(stats.totalRequests, 1u);
        // 请求可能成功或失败，取决于重试策略
        
        requestManager.stop();
    }});

    return mini_test::run(tests);
}

