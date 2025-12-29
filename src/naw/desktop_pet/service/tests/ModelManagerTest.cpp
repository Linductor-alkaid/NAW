#include "naw/desktop_pet/service/ModelManager.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/types/ModelConfig.h"
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

// ModelHealthStatus 特化
inline std::string toString(ModelHealthStatus v) {
    switch (v) {
        case ModelHealthStatus::Healthy: return "Healthy";
        case ModelHealthStatus::Degraded: return "Degraded";
        case ModelHealthStatus::Unhealthy: return "Unhealthy";
        case ModelHealthStatus::Unknown: return "Unknown";
    }
    return "Unknown";
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

// 创建测试用的模型配置
static ModelConfig createTestModel(const std::string& modelId, TaskType taskType) {
    ModelConfig config;
    config.modelId = modelId;
    config.displayName = "Test Model " + modelId;
    config.supportedTasks = {taskType};
    config.maxContextTokens = 4096;
    config.defaultTemperature = 0.7f;
    config.defaultMaxTokens = 2048;
    config.costPer1kTokens = 0.1f;
    config.maxConcurrentRequests = 10;
    config.supportsStreaming = true;
    config.performanceScore = 0.8f;
    return config;
}

// 创建测试用的ConfigManager（通过引用参数设置）
static void createTestConfigManager(ConfigManager& cfg) {
    nlohmann::json testConfig = nlohmann::json::object();
    testConfig["models"] = nlohmann::json::array();
    
    nlohmann::json model1 = nlohmann::json::object();
    model1["model_id"] = "test/model1";
    model1["display_name"] = "Test Model 1";
    model1["supported_tasks"] = nlohmann::json::array({"CodeGeneration", "CodeAnalysis"});
    model1["max_context_tokens"] = 4096;
    model1["default_temperature"] = 0.7;
    model1["default_max_tokens"] = 2048;
    model1["cost_per_1k_tokens"] = 0.1;
    model1["max_concurrent_requests"] = 10;
    model1["supports_streaming"] = true;
    model1["performance_score"] = 0.8;
    
    testConfig["models"].push_back(model1);
    
    cfg.loadFromString(testConfig.dump());
}

int main() {
    using mini_test::TestCase;

    std::vector<TestCase> tests;

    // ========== 模型配置加载测试 ==========
    tests.push_back({"ModelManager_LoadFromConfig", []() {
        ConfigManager cfg;
        createTestConfigManager(cfg);
        ModelManager manager(cfg);
        
        CHECK_TRUE(manager.loadModelsFromConfig());
        CHECK_TRUE(manager.hasModel("test/model1"));
        
        auto model = manager.getModel("test/model1");
        CHECK_TRUE(model.has_value());
        CHECK_EQ(model->modelId, "test/model1");
        CHECK_EQ(model->supportedTasks.size(), 2);
    }});

    // ========== 模型注册/移除测试 ==========
    tests.push_back({"ModelManager_RegisterModel", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        
        ModelConfig config = createTestModel("test/model2", TaskType::CodeGeneration);
        CHECK_TRUE(manager.registerModel(config));
        CHECK_TRUE(manager.hasModel("test/model2"));
        
        auto model = manager.getModel("test/model2");
        CHECK_TRUE(model.has_value());
        CHECK_EQ(model->modelId, "test/model2");
    }});

    tests.push_back({"ModelManager_UnregisterModel", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        
        ModelConfig config = createTestModel("test/model3", TaskType::CodeAnalysis);
        CHECK_TRUE(manager.registerModel(config));
        CHECK_TRUE(manager.hasModel("test/model3"));
        
        CHECK_TRUE(manager.unregisterModel("test/model3"));
        CHECK_FALSE(manager.hasModel("test/model3"));
    }});

    tests.push_back({"ModelManager_RegisterDuplicate", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        
        ModelConfig config = createTestModel("test/model4", TaskType::TechnicalQnA);
        CHECK_TRUE(manager.registerModel(config));
        
        // 不允许覆盖
        ErrorInfo err;
        CHECK_FALSE(manager.registerModel(config, false, &err));
        
        // 允许覆盖
        CHECK_TRUE(manager.registerModel(config, true));
    }});

    // ========== 模型查询测试 ==========
    tests.push_back({"ModelManager_GetAllModels", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        
        ModelConfig config1 = createTestModel("test/model5", TaskType::CodeGeneration);
        ModelConfig config2 = createTestModel("test/model6", TaskType::CodeAnalysis);
        
        manager.registerModel(config1);
        manager.registerModel(config2);
        
        auto allModels = manager.getAllModels();
        CHECK_EQ(allModels.size(), 2);
    }});

    // ========== 性能统计测试 ==========
    tests.push_back({"ModelManager_RecordRequest", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        
        ModelConfig config = createTestModel("test/model7", TaskType::CodeGeneration);
        manager.registerModel(config);
        
        manager.recordRequest("test/model7", true);
        manager.recordRequest("test/model7", true);
        manager.recordRequest("test/model7", false);
        
        auto stats = manager.getStatistics("test/model7");
        CHECK_TRUE(stats.has_value());
        CHECK_EQ(stats->totalRequests, 3);
        CHECK_EQ(stats->successfulRequests, 2);
        CHECK_EQ(stats->failedRequests, 1);
        
        double successRate = manager.getSuccessRate("test/model7");
        CHECK_TRUE(successRate > 0.66 && successRate < 0.67); // 2/3 ≈ 0.667
    }});

    tests.push_back({"ModelManager_RecordResponseTime", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        
        ModelConfig config = createTestModel("test/model8", TaskType::CodeGeneration);
        manager.registerModel(config);
        
        manager.recordResponseTime("test/model8", 100);
        manager.recordResponseTime("test/model8", 200);
        manager.recordResponseTime("test/model8", 150);
        
        auto stats = manager.getStatistics("test/model8");
        CHECK_TRUE(stats.has_value());
        CHECK_EQ(stats->minResponseTimeMs, 100);
        CHECK_EQ(stats->maxResponseTimeMs, 200);
        CHECK_EQ(stats->getAverageResponseTimeMs(), 150);
    }});

    tests.push_back({"ModelManager_LoadFactor", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        
        ModelConfig config = createTestModel("test/model9", TaskType::CodeGeneration);
        config.maxConcurrentRequests = 10;
        manager.registerModel(config);
        
        // 初始负载为0
        double loadFactor = manager.getLoadFactor("test/model9");
        CHECK_EQ(loadFactor, 0.0);
        
        // 增加并发
        manager.incrementConcurrency("test/model9");
        manager.incrementConcurrency("test/model9");
        loadFactor = manager.getLoadFactor("test/model9");
        CHECK_TRUE(loadFactor > 0.19 && loadFactor < 0.21); // 2/10 = 0.2
        
        // 减少并发
        manager.decrementConcurrency("test/model9");
        loadFactor = manager.getLoadFactor("test/model9");
        CHECK_TRUE(loadFactor > 0.09 && loadFactor < 0.11); // 1/10 = 0.1
    }});

    // ========== 健康状态监控测试 ==========
    tests.push_back({"ModelManager_HealthStatus", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        
        ModelConfig config = createTestModel("test/model10", TaskType::CodeGeneration);
        manager.registerModel(config);
        
        // 初始状态为Unknown
        auto health = manager.getModelHealth("test/model10");
        CHECK_EQ(health, ModelHealthStatus::Unknown);
        
        // 记录一些成功请求
        for (int i = 0; i < 5; ++i) {
            manager.updateModelHealth("test/model10", true, 100);
        }
        
        health = manager.getModelHealth("test/model10");
        CHECK_TRUE(health == ModelHealthStatus::Healthy || health == ModelHealthStatus::Unknown);
        
        // 记录失败请求
        for (int i = 0; i < 5; ++i) {
            manager.updateModelHealth("test/model10", false, 100);
        }
        
        health = manager.getModelHealth("test/model10");
        // 失败率50%，应该是不健康或降级
        CHECK_TRUE(health == ModelHealthStatus::Unhealthy || health == ModelHealthStatus::Degraded);
    }});

    // ========== 按任务类型查询模型测试 ==========
    tests.push_back({"ModelManager_GetModelsForTask", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        
        ModelConfig config1 = createTestModel("test/model11", TaskType::CodeGeneration);
        config1.performanceScore = 0.9f;
        manager.registerModel(config1);
        
        ModelConfig config2 = createTestModel("test/model12", TaskType::CodeGeneration);
        config2.performanceScore = 0.7f;
        manager.registerModel(config2);
        
        ModelConfig config3 = createTestModel("test/model13", TaskType::CodeAnalysis);
        manager.registerModel(config3);
        
        auto models = manager.getModelsForTask(TaskType::CodeGeneration);
        CHECK_EQ(models.size(), 2);
        // 应该按性能评分排序
        CHECK_EQ(models[0].modelId, "test/model11"); // 评分更高
        CHECK_EQ(models[1].modelId, "test/model12");
        
        auto models2 = manager.getModelsForTask(TaskType::CodeAnalysis);
        CHECK_EQ(models2.size(), 1);
        CHECK_EQ(models2[0].modelId, "test/model13");
    }});

    tests.push_back({"ModelManager_GetBestModelForTask", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        
        ModelConfig config1 = createTestModel("test/model14", TaskType::CodeGeneration);
        config1.performanceScore = 0.9f;
        manager.registerModel(config1);
        
        ModelConfig config2 = createTestModel("test/model15", TaskType::CodeGeneration);
        config2.performanceScore = 0.7f;
        manager.registerModel(config2);
        
        auto bestModel = manager.getBestModelForTask(TaskType::CodeGeneration);
        CHECK_TRUE(bestModel.has_value());
        CHECK_EQ(bestModel->modelId, "test/model14");
    }});

    tests.push_back({"ModelManager_ResetStatistics", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        
        ModelConfig config = createTestModel("test/model16", TaskType::CodeGeneration);
        manager.registerModel(config);
        
        manager.recordRequest("test/model16", true);
        manager.recordRequest("test/model16", true);
        
        auto stats = manager.getStatistics("test/model16");
        CHECK_EQ(stats->totalRequests, 2);
        
        manager.resetStatistics("test/model16");
        
        stats = manager.getStatistics("test/model16");
        CHECK_EQ(stats->totalRequests, 0);
    }});

    return mini_test::run(tests);
}

