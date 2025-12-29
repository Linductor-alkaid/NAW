#include "naw/desktop_pet/service/TaskRouter.h"
#include "naw/desktop_pet/service/ModelManager.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/types/ModelConfig.h"
#include "naw/desktop_pet/service/types/TaskType.h"
#include "naw/desktop_pet/service/types/TaskPriority.h"

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
static ModelConfig createTestModel(
    const std::string& modelId,
    TaskType taskType,
    uint32_t maxContextTokens = 4096,
    float performanceScore = 0.8f,
    float costPer1kTokens = 0.1f
) {
    ModelConfig config;
    config.modelId = modelId;
    config.displayName = "Test Model " + modelId;
    config.supportedTasks = {taskType};
    config.maxContextTokens = maxContextTokens;
    config.defaultTemperature = 0.7f;
    config.defaultMaxTokens = 2048;
    config.costPer1kTokens = costPer1kTokens;
    config.maxConcurrentRequests = 10;
    config.supportsStreaming = true;
    config.performanceScore = performanceScore;
    return config;
}

// 创建测试用的ConfigManager和ModelManager（通过引用参数设置）
static void createTestSetup(ConfigManager& cfg, ModelManager& manager) {
    // 注册几个测试模型
    ModelConfig model1 = createTestModel("test/model1", TaskType::CodeGeneration, 4096, 0.9f, 0.2f);
    ModelConfig model2 = createTestModel("test/model2", TaskType::CodeGeneration, 8192, 0.7f, 0.1f);
    ModelConfig model3 = createTestModel("test/model3", TaskType::CodeAnalysis, 4096, 0.8f, 0.15f);
    
    manager.registerModel(model1);
    manager.registerModel(model2);
    manager.registerModel(model3);
}

int main() {
    using mini_test::TestCase;

    std::vector<TestCase> tests;

    // ========== 路由表初始化测试 ==========
    tests.push_back({"TaskRouter_InitializeRoutingTable", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        createTestSetup(cfg, manager);
        
        // 设置路由配置
        nlohmann::json routingConfig = nlohmann::json::object();
        routingConfig["default_model_per_task"] = nlohmann::json::object();
        routingConfig["default_model_per_task"]["CodeGeneration"] = "test/model1";
        routingConfig["fallback_model"] = "test/model1";
        cfg.set("routing", routingConfig);
        
        TaskRouter router(cfg, manager);
        CHECK_TRUE(router.initializeRoutingTable());
    }});

    // ========== 任务类型匹配测试 ==========
    tests.push_back({"TaskRouter_RouteTask_TypeMatch", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        createTestSetup(cfg, manager);
        TaskRouter router(cfg, manager);
        router.initializeRoutingTable();
        
        TaskContext context;
        context.taskType = TaskType::CodeGeneration;
        context.estimatedTokens = 1000;
        context.priority = TaskPriority::Normal;
        
        RoutingDecision decision = router.routeTask(context);
        CHECK_TRUE(decision.isValid());
        CHECK_TRUE(decision.modelId == "test/model1" || decision.modelId == "test/model2");
        CHECK_TRUE(decision.confidence > 0.0f);
    }});

    // ========== 上下文容量检查测试 ==========
    tests.push_back({"TaskRouter_ContextCapacity", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        createTestSetup(cfg, manager);
        TaskRouter router(cfg, manager);
        router.initializeRoutingTable();
        
        TaskContext context;
        context.taskType = TaskType::CodeGeneration;
        context.estimatedTokens = 5000; // 超过model1的4096，但不超过model2的8192
        context.priority = TaskPriority::Normal;
        
        RoutingDecision decision = router.routeTask(context);
        CHECK_TRUE(decision.isValid());
        // 应该选择model2（容量更大）
        CHECK_EQ(decision.modelId, "test/model2");
    }});

    // ========== 模型评分计算测试 ==========
    tests.push_back({"TaskRouter_ModelScoring", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        createTestSetup(cfg, manager);
        TaskRouter router(cfg, manager);
        router.initializeRoutingTable();
        
        TaskContext context;
        context.taskType = TaskType::CodeGeneration;
        context.estimatedTokens = 2000;
        context.priority = TaskPriority::Normal;
        
        RoutingDecision decision = router.routeTask(context);
        CHECK_TRUE(decision.isValid());
        CHECK_TRUE(decision.confidence > 0.0f && decision.confidence <= 1.0f);
        // model1性能评分更高，应该被选中
        CHECK_EQ(decision.modelId, "test/model1");
    }});

    // ========== 负载均衡测试 ==========
    tests.push_back({"TaskRouter_LoadBalancing", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        createTestSetup(cfg, manager);
        TaskRouter router(cfg, manager);
        router.initializeRoutingTable();
        
        // 增加model1的负载
        for (int i = 0; i < 8; ++i) {
            manager.incrementConcurrency("test/model1");
        }
        
        TaskContext context;
        context.taskType = TaskType::CodeGeneration;
        context.estimatedTokens = 2000;
        context.priority = TaskPriority::Normal;
        
        RoutingDecision decision = router.routeTask(context);
        CHECK_TRUE(decision.isValid());
        // model1负载高，应该选择model2
        CHECK_EQ(decision.modelId, "test/model2");
        
        // 清理
        for (int i = 0; i < 8; ++i) {
            manager.decrementConcurrency("test/model1");
        }
    }});

    // ========== 成本优化测试 ==========
    tests.push_back({"TaskRouter_CostOptimization", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        createTestSetup(cfg, manager);
        TaskRouter router(cfg, manager);
        router.initializeRoutingTable();
        
        TaskContext context;
        context.taskType = TaskType::CodeGeneration;
        context.estimatedTokens = 2000;
        context.priority = TaskPriority::Low; // 低优先级，应该优先考虑成本
        
        RoutingDecision decision = router.routeTask(context);
        CHECK_TRUE(decision.isValid());
        // model2成本更低，应该被选中
        CHECK_EQ(decision.modelId, "test/model2");
    }});

    // ========== 路由决策生成测试 ==========
    tests.push_back({"TaskRouter_RoutingDecision", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        createTestSetup(cfg, manager);
        TaskRouter router(cfg, manager);
        router.initializeRoutingTable();
        
        TaskContext context;
        context.taskType = TaskType::CodeGeneration;
        context.estimatedTokens = 2000;
        context.priority = TaskPriority::Normal;
        
        RoutingDecision decision = router.routeTask(context);
        CHECK_TRUE(decision.isValid());
        CHECK_FALSE(decision.modelId.empty());
        CHECK_FALSE(decision.reason.empty());
        CHECK_TRUE(decision.confidence > 0.0f);
    }});

    // ========== 流式支持测试 ==========
    tests.push_back({"TaskRouter_StreamingSupport", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        createTestSetup(cfg, manager);
        TaskRouter router(cfg, manager);
        router.initializeRoutingTable();
        
        TaskContext context;
        context.taskType = TaskType::CodeGeneration;
        context.estimatedTokens = 2000;
        context.priority = TaskPriority::Normal;
        context.requiresStreaming = true;
        
        RoutingDecision decision = router.routeTask(context);
        CHECK_TRUE(decision.isValid());
        CHECK_TRUE(decision.modelConfig.supportsStreaming);
    }});

    // ========== 成本限制测试 ==========
    tests.push_back({"TaskRouter_CostLimit", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        createTestSetup(cfg, manager);
        TaskRouter router(cfg, manager);
        router.initializeRoutingTable();
        
        TaskContext context;
        context.taskType = TaskType::CodeGeneration;
        context.estimatedTokens = 2000;
        context.priority = TaskPriority::Normal;
        context.maxCost = 0.05f; // 最大成本0.05
        
        RoutingDecision decision = router.routeTask(context);
        CHECK_TRUE(decision.isValid());
        // model2成本更低，应该被选中
        CHECK_EQ(decision.modelId, "test/model2");
    }});

    // ========== 路由决策记录测试 ==========
    tests.push_back({"TaskRouter_RecordDecision", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        createTestSetup(cfg, manager);
        TaskRouter router(cfg, manager);
        router.initializeRoutingTable();
        
        TaskContext context;
        context.taskType = TaskType::CodeGeneration;
        context.estimatedTokens = 2000;
        context.priority = TaskPriority::Normal;
        
        RoutingDecision decision = router.routeTask(context);
        router.recordDecision(decision);
        
        auto history = router.getRoutingHistory(10);
        CHECK_EQ(history.size(), 1);
        CHECK_EQ(history[0].selectedModel, decision.modelId);
        
        auto stats = router.getRoutingStatistics();
        CHECK_EQ(stats[decision.modelId], 1);
    }});

    // ========== 回退模型测试 ==========
    tests.push_back({"TaskRouter_FallbackModel", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        createTestSetup(cfg, manager);
        
        // 设置回退模型
        cfg.set("routing.fallback_model", nlohmann::json("test/model1"));
        
        TaskRouter router(cfg, manager);
        router.initializeRoutingTable();
        
        TaskContext context;
        context.taskType = TaskType::CasualChat; // 没有模型支持这个任务
        context.estimatedTokens = 1000;
        context.priority = TaskPriority::Normal;
        
        RoutingDecision decision = router.routeTask(context);
        // 应该使用回退模型
        CHECK_TRUE(decision.isValid());
        CHECK_EQ(decision.modelId, "test/model1");
        CHECK_TRUE(decision.confidence < 0.5f); // 回退模型置信度较低
    }});

    // ========== 简化接口测试 ==========
    tests.push_back({"TaskRouter_SimplifiedInterface", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        createTestSetup(cfg, manager);
        TaskRouter router(cfg, manager);
        router.initializeRoutingTable();
        
        RoutingDecision decision = router.routeTask(
            TaskType::CodeGeneration,
            2000,
            TaskPriority::Normal
        );
        
        CHECK_TRUE(decision.isValid());
        CHECK_FALSE(decision.modelId.empty());
    }});

    // ========== 清空历史测试 ==========
    tests.push_back({"TaskRouter_ClearHistory", []() {
        ConfigManager cfg;
        ModelManager manager(cfg);
        createTestSetup(cfg, manager);
        TaskRouter router(cfg, manager);
        router.initializeRoutingTable();
        
        TaskContext context;
        context.taskType = TaskType::CodeGeneration;
        context.estimatedTokens = 2000;
        context.priority = TaskPriority::Normal;
        
        RoutingDecision decision = router.routeTask(context);
        router.recordDecision(decision);
        
        CHECK_EQ(router.getRoutingHistory().size(), 1);
        
        router.clearRoutingHistory();
        CHECK_EQ(router.getRoutingHistory().size(), 0);
    }});

    return mini_test::run(tests);
}

