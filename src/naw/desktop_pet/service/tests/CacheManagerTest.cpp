#include "naw/desktop_pet/service/CacheManager.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"
#include "naw/desktop_pet/service/types/ChatMessage.h"

#include <chrono>
#include <functional>
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

// 创建测试用的请求
static ChatRequest createTestRequest(const std::string& modelId, const std::string& content,
                                     std::optional<float> temperature = std::nullopt,
                                     std::optional<uint32_t> maxTokens = std::nullopt) {
    ChatRequest req;
    req.model = modelId;
    ChatMessage msg(MessageRole::User, content);
    req.messages.push_back(msg);
    if (temperature.has_value()) req.temperature = temperature;
    if (maxTokens.has_value()) req.maxTokens = maxTokens;
    return req;
}

// 创建测试用的响应
static ChatResponse createTestResponse(const std::string& content) {
    ChatResponse resp;
    resp.content = content;
    resp.promptTokens = 10;
    resp.completionTokens = 20;
    resp.totalTokens = 30;
    return resp;
}

// 创建测试用的配置管理器（通过引用参数设置）
static void createTestConfigManager(ConfigManager& config) {
    nlohmann::json cfg = nlohmann::json::object();
    cfg["cache"] = nlohmann::json::object();
    cfg["cache"]["enabled"] = true;
    cfg["cache"]["default_ttl_seconds"] = 3600;
    cfg["cache"]["max_entries"] = 1000;
    cfg["cache"]["cleanup_interval_seconds"] = 300;
    config.loadFromString(cfg.dump());
}

int main() {
    using namespace mini_test;

    std::vector<TestCase> tests;

    // ========== Cache Key Generation Tests ==========
    tests.push_back({"CacheKey_Generate_SameRequestSameKey", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);

        ChatRequest req1 = createTestRequest("model1", "Hello");
        ChatRequest req2 = createTestRequest("model1", "Hello");

        CacheManager::CacheKey key1 = cache.generateKey(req1);
        CacheManager::CacheKey key2 = cache.generateKey(req2);

        CHECK_TRUE(key1 == key2);
    }});

    tests.push_back({"CacheKey_Generate_DifferentRequestDifferentKey", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);

        ChatRequest req1 = createTestRequest("model1", "Hello");
        ChatRequest req2 = createTestRequest("model1", "World");

        CacheManager::CacheKey key1 = cache.generateKey(req1);
        CacheManager::CacheKey key2 = cache.generateKey(req2);

        CHECK_FALSE(key1 == key2);
    }});

    tests.push_back({"CacheKey_Generate_DifferentModelDifferentKey", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);

        ChatRequest req1 = createTestRequest("model1", "Hello");
        ChatRequest req2 = createTestRequest("model2", "Hello");

        CacheManager::CacheKey key1 = cache.generateKey(req1);
        CacheManager::CacheKey key2 = cache.generateKey(req2);

        CHECK_FALSE(key1 == key2);
    }});

    tests.push_back({"CacheKey_Generate_ParameterChangeChangesKey", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);

        ChatRequest req1 = createTestRequest("model1", "Hello", 0.7f, 100u);
        ChatRequest req2 = createTestRequest("model1", "Hello", 0.8f, 100u);
        ChatRequest req3 = createTestRequest("model1", "Hello", 0.7f, 200u);

        CacheManager::CacheKey key1 = cache.generateKey(req1);
        CacheManager::CacheKey key2 = cache.generateKey(req2);
        CacheManager::CacheKey key3 = cache.generateKey(req3);

        CHECK_FALSE(key1 == key2); // temperature 不同
        CHECK_FALSE(key1 == key3); // maxTokens 不同
        CHECK_FALSE(key2 == key3); // 都不同
    }});

    // ========== Cache Storage and Query Tests ==========
    tests.push_back({"Cache_StoreAndRetrieve_Basic", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);

        ChatRequest req = createTestRequest("model1", "Hello");
        ChatResponse resp = createTestResponse("Hi there!");

        CacheManager::CacheKey key = cache.generateKey(req);

        // 存储
        cache.put(key, resp);

        // 检索
        auto cached = cache.get(key);
        CHECK_TRUE(cached.has_value());
        CHECK_EQ(cached->content, "Hi there!");
    }});

    tests.push_back({"Cache_Query_MissReturnsNullopt", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);

        ChatRequest req = createTestRequest("model1", "Hello");
        CacheManager::CacheKey key = cache.generateKey(req);

        // 未存储，应该返回 nullopt
        auto cached = cache.get(key);
        CHECK_FALSE(cached.has_value());
    }});

    tests.push_back({"Cache_TTL_ExpiredEntryNotReturned", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);

        ChatRequest req = createTestRequest("model1", "Hello");
        ChatResponse resp = createTestResponse("Hi there!");
        CacheManager::CacheKey key = cache.generateKey(req);

        // 使用很短的TTL存储（1秒）
        cache.put(key, resp, std::chrono::seconds(1));

        // 立即查询应该命中
        auto cached1 = cache.get(key);
        CHECK_TRUE(cached1.has_value());

        // 等待过期
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));

        // 再次查询应该未命中（已过期）
        auto cached2 = cache.get(key);
        CHECK_FALSE(cached2.has_value());
    }});

    tests.push_back({"Cache_Update_SameKeyUpdatesEntry", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);

        ChatRequest req = createTestRequest("model1", "Hello");
        CacheManager::CacheKey key = cache.generateKey(req);

        ChatResponse resp1 = createTestResponse("Response 1");
        ChatResponse resp2 = createTestResponse("Response 2");

        cache.put(key, resp1);
        cache.put(key, resp2); // 更新

        auto cached = cache.get(key);
        CHECK_TRUE(cached.has_value());
        CHECK_EQ(cached->content, "Response 2"); // 应该是新的响应
    }});

    // ========== Cache Eviction Tests ==========
    tests.push_back({"Cache_EvictExpired_ExpiredEntriesRemoved", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);

        ChatRequest req1 = createTestRequest("model1", "Hello1");
        ChatRequest req2 = createTestRequest("model1", "Hello2");
        ChatRequest req3 = createTestRequest("model1", "Hello3");

        CacheManager::CacheKey key1 = cache.generateKey(req1);
        CacheManager::CacheKey key2 = cache.generateKey(req2);
        CacheManager::CacheKey key3 = cache.generateKey(req3);

        // 存储三个条目，其中两个使用很短的TTL
        cache.put(key1, createTestResponse("Resp1"), std::chrono::seconds(1));
        cache.put(key2, createTestResponse("Resp2"), std::chrono::seconds(1));
        cache.put(key3, createTestResponse("Resp3"), std::chrono::hours(1)); // 不过期

        CHECK_EQ(cache.getCacheSize(), 3u);

        // 等待过期
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));

        // 清理过期条目
        size_t evicted = cache.evictExpired();
        CHECK_EQ(evicted, 2u);
        CHECK_EQ(cache.getCacheSize(), 1u);

        // 验证未过期的条目还在
        auto cached = cache.get(key3);
        CHECK_TRUE(cached.has_value());
    }});

    tests.push_back({"Cache_LRU_LeastRecentlyUsedEvicted", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);

        // 设置较小的最大条目数
        nlohmann::json cfg = nlohmann::json::object();
        cfg["cache"] = nlohmann::json::object();
        cfg["cache"]["enabled"] = true;
        cfg["cache"]["default_ttl_seconds"] = 3600;
        cfg["cache"]["max_entries"] = 3; // 只允许3个条目
        cfg["cache"]["cleanup_interval_seconds"] = 300;
        ConfigManager testConfig;
        testConfig.loadFromString(cfg.dump());
        CacheManager testCache(testConfig);

        ChatRequest req1 = createTestRequest("model1", "Hello1");
        ChatRequest req2 = createTestRequest("model1", "Hello2");
        ChatRequest req3 = createTestRequest("model1", "Hello3");
        ChatRequest req4 = createTestRequest("model1", "Hello4");

        CacheManager::CacheKey key1 = testCache.generateKey(req1);
        CacheManager::CacheKey key2 = testCache.generateKey(req2);
        CacheManager::CacheKey key3 = testCache.generateKey(req3);
        CacheManager::CacheKey key4 = testCache.generateKey(req4);

        // 存储3个条目
        testCache.put(key1, createTestResponse("Resp1"));
        testCache.put(key2, createTestResponse("Resp2"));
        testCache.put(key3, createTestResponse("Resp3"));

        CHECK_EQ(testCache.getCacheSize(), 3u);

        // 访问 key2 和 key3，使 key1 成为最久未使用的
        testCache.get(key2);
        testCache.get(key3);

        // 添加第4个条目，应该触发LRU淘汰
        testCache.put(key4, createTestResponse("Resp4"));

        // key1 应该被淘汰（最久未使用）
        CHECK_FALSE(testCache.get(key1).has_value());
        // key2, key3, key4 应该还在
        CHECK_TRUE(testCache.get(key2).has_value());
        CHECK_TRUE(testCache.get(key3).has_value());
        CHECK_TRUE(testCache.get(key4).has_value());
    }});

    tests.push_back({"Cache_Clear_AllEntriesRemoved", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);

        ChatRequest req1 = createTestRequest("model1", "Hello1");
        ChatRequest req2 = createTestRequest("model1", "Hello2");

        CacheManager::CacheKey key1 = cache.generateKey(req1);
        CacheManager::CacheKey key2 = cache.generateKey(req2);

        cache.put(key1, createTestResponse("Resp1"));
        cache.put(key2, createTestResponse("Resp2"));

        CHECK_EQ(cache.getCacheSize(), 2u);

        cache.clear();

        CHECK_EQ(cache.getCacheSize(), 0u);
        CHECK_FALSE(cache.get(key1).has_value());
        CHECK_FALSE(cache.get(key2).has_value());
    }});

    // ========== Cache Statistics Tests ==========
    tests.push_back({"Cache_Statistics_HitRateCorrect", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);

        ChatRequest req1 = createTestRequest("model1", "Hello1");
        ChatRequest req2 = createTestRequest("model1", "Hello2");

        CacheManager::CacheKey key1 = cache.generateKey(req1);
        CacheManager::CacheKey key2 = cache.generateKey(req2);

        // 存储一个条目
        cache.put(key1, createTestResponse("Resp1"));

        // 命中
        cache.get(key1);
        // 未命中
        cache.get(key2);
        // 再次命中
        cache.get(key1);

        auto stats = cache.getStatistics();
        CHECK_EQ(stats.totalHits, 2u);
        CHECK_EQ(stats.totalMisses, 1u);
        CHECK_TRUE(stats.getHitRate() > 0.66); // 2/3 ≈ 0.67
        CHECK_TRUE(stats.getHitRate() < 0.68);
    }});

    tests.push_back({"Cache_Statistics_StatsUpdatedCorrectly", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);

        ChatRequest req = createTestRequest("model1", "Hello");
        CacheManager::CacheKey key = cache.generateKey(req);

        auto stats1 = cache.getStatistics();
        CHECK_EQ(stats1.totalEntries, 0u);
        CHECK_EQ(stats1.totalHits, 0u);
        CHECK_EQ(stats1.totalMisses, 0u);

        cache.put(key, createTestResponse("Resp1"));
        cache.get(key); // 命中

        auto stats2 = cache.getStatistics();
        CHECK_EQ(stats2.totalEntries, 1u);
        CHECK_EQ(stats2.totalHits, 1u);
        CHECK_EQ(stats2.totalMisses, 0u);
    }});

    tests.push_back({"Cache_Statistics_StatsResetAfterClear", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);

        ChatRequest req = createTestRequest("model1", "Hello");
        CacheManager::CacheKey key = cache.generateKey(req);

        cache.put(key, createTestResponse("Resp1"));
        cache.get(key);
        cache.get(key);

        auto stats1 = cache.getStatistics();
        CHECK_TRUE(stats1.totalHits > 0);

        cache.clear();

        auto stats2 = cache.getStatistics();
        CHECK_EQ(stats2.totalHits, 0u);
        CHECK_EQ(stats2.totalMisses, 0u);
        CHECK_EQ(stats2.totalEntries, 0u);
    }});

    // ========== Thread Safety Tests ==========
    tests.push_back({"Cache_ThreadSafety_ConcurrentReadWrite", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);

        const int numThreads = 10;
        const int opsPerThread = 100;
        std::vector<std::thread> threads;

        // 启动多个线程进行并发读写
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([&cache, i, opsPerThread]() {
                for (int j = 0; j < opsPerThread; ++j) {
                    std::string content = "Thread" + std::to_string(i) + "_Op" + std::to_string(j);
                    ChatRequest req = createTestRequest("model1", content);
                    CacheManager::CacheKey key = cache.generateKey(req);
                    ChatResponse resp = createTestResponse("Response to " + content);

                    cache.put(key, resp);
                    auto cached = cache.get(key);
                    // 不检查结果，只确保不崩溃
                }
            });
        }

        // 等待所有线程完成
        for (auto& t : threads) {
            t.join();
        }

        // 验证缓存仍然可用
        CHECK_TRUE(cache.getCacheSize() > 0);
    }});

    // ========== Configuration Tests ==========
    tests.push_back({"Cache_Config_DisabledCacheNotStored", []() {
        ConfigManager config;
        nlohmann::json cfg = nlohmann::json::object();
        cfg["cache"] = nlohmann::json::object();
        cfg["cache"]["enabled"] = false;
        config.loadFromString(cfg.dump());

        CacheManager cache(config);

        ChatRequest req = createTestRequest("model1", "Hello");
        CacheManager::CacheKey key = cache.generateKey(req);
        ChatResponse resp = createTestResponse("Hi");

        cache.put(key, resp);

        // 禁用缓存时，get 应该返回 nullopt
        auto cached = cache.get(key);
        CHECK_FALSE(cached.has_value());
    }});

    tests.push_back({"Cache_Config_CustomTTLApplied", []() {
        ConfigManager config;
        nlohmann::json cfg = nlohmann::json::object();
        cfg["cache"] = nlohmann::json::object();
        cfg["cache"]["enabled"] = true;
        cfg["cache"]["default_ttl_seconds"] = 2; // 2秒TTL
        cfg["cache"]["max_entries"] = 1000;
        cfg["cache"]["cleanup_interval_seconds"] = 300;
        config.loadFromString(cfg.dump());

        CacheManager cache(config);

        ChatRequest req = createTestRequest("model1", "Hello");
        CacheManager::CacheKey key = cache.generateKey(req);
        ChatResponse resp = createTestResponse("Hi");

        // 使用默认TTL存储
        cache.put(key, resp);

        // 立即查询应该命中
        CHECK_TRUE(cache.get(key).has_value());

        // 等待过期
        std::this_thread::sleep_for(std::chrono::milliseconds(2100));

        // 应该已过期
        CHECK_FALSE(cache.get(key).has_value());
    }});

    return run(tests);
}

