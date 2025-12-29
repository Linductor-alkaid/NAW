#pragma once

#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/ErrorTypes.h"
#include "naw/desktop_pet/service/ModelManager.h"
#include "naw/desktop_pet/service/types/ModelConfig.h"
#include "naw/desktop_pet/service/types/TaskPriority.h"
#include "naw/desktop_pet/service/types/TaskType.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace naw::desktop_pet::service {

/**
 * @brief 模型偏好结构（用于路由表）
 */
struct ModelPreference {
    std::string modelId;
    int priority{0};  // 优先级（数字越小优先级越高）
    float weight{1.0f}; // 权重（用于评分计算）
};

/**
 * @brief 任务上下文结构
 */
struct TaskContext {
    types::TaskType taskType{types::TaskType::CasualChat};
    size_t estimatedTokens{0};  // 预估上下文Token数
    types::TaskPriority priority{types::TaskPriority::Normal};
    std::optional<float> maxCost;  // 最大成本限制（可选）
    bool requiresStreaming{false};  // 是否需要流式响应
    std::optional<std::string> preferredModel;  // 偏好模型（可选）
};

/**
 * @brief 路由决策结构
 */
struct RoutingDecision {
    std::string modelId;
    types::ModelConfig modelConfig;
    float confidence{0.0f};  // 选择置信度（0-1）
    std::string reason;      // 选择原因

    bool isValid() const {
        return !modelId.empty() && confidence > 0.0f;
    }
};

/**
 * @brief 路由历史记录
 */
struct RoutingHistory {
    std::chrono::system_clock::time_point timestamp;
    types::TaskType taskType;
    std::string selectedModel;
    float confidence;
    std::string reason;
};

/**
 * @brief 任务路由器：根据任务类型、上下文大小、优先级等因素智能选择最合适的模型
 */
class TaskRouter {
public:
    TaskRouter(ConfigManager& configManager, ModelManager& modelManager);
    ~TaskRouter() = default;

    // 禁止拷贝/移动
    TaskRouter(const TaskRouter&) = delete;
    TaskRouter& operator=(const TaskRouter&) = delete;
    TaskRouter(TaskRouter&&) = delete;
    TaskRouter& operator=(TaskRouter&&) = delete;

    // ========== 路由表初始化 ==========
    /**
     * @brief 初始化路由表（从配置文件读取）
     * @param err 错误信息输出（可选）
     * @return 是否初始化成功
     */
    bool initializeRoutingTable(ErrorInfo* err = nullptr);

    // ========== 智能路由算法 ==========
    /**
     * @brief 路由任务，选择最合适的模型
     * @param context 任务上下文
     * @return 路由决策
     */
    RoutingDecision routeTask(const TaskContext& context);

    /**
     * @brief 路由任务（简化接口）
     * @param taskType 任务类型
     * @param estimatedTokens 预估Token数
     * @param priority 任务优先级
     * @return 路由决策
     */
    RoutingDecision routeTask(
        types::TaskType taskType,
        size_t estimatedTokens = 0,
        types::TaskPriority priority = types::TaskPriority::Normal
    );

    // ========== 路由决策记录和日志 ==========
    /**
     * @brief 记录路由决策
     * @param decision 路由决策
     */
    void recordDecision(const RoutingDecision& decision);

    /**
     * @brief 获取路由历史（最近N条）
     * @param maxCount 最大记录数
     * @return 路由历史列表
     */
    std::vector<RoutingHistory> getRoutingHistory(size_t maxCount = 100) const;

    /**
     * @brief 清空路由历史
     */
    void clearRoutingHistory();

    /**
     * @brief 获取路由统计信息
     * @return 模型ID到被选中次数的映射
     */
    std::unordered_map<std::string, uint64_t> getRoutingStatistics() const;

private:
    ConfigManager& m_configManager;
    ModelManager& m_modelManager;

    // 路由表：TaskType -> vector<ModelPreference>
    std::unordered_map<types::TaskType, std::vector<ModelPreference>> m_routingTable;

    // 默认模型映射：TaskType -> modelId
    std::unordered_map<types::TaskType, std::string> m_defaultModels;

    // 路由历史（限制大小）
    mutable std::mutex m_historyMutex;
    std::vector<RoutingHistory> m_routingHistory;
    static constexpr size_t kMaxHistorySize = 1000;

    // 路由统计：modelId -> count
    mutable std::mutex m_statsMutex;
    std::unordered_map<std::string, uint64_t> m_routingStats;

    // 内部方法
    /**
     * @brief 计算模型评分
     * @param model 模型配置
     * @param context 任务上下文
     * @return 评分（0-1）
     */
    float calculateModelScore(const types::ModelConfig& model, const TaskContext& context) const;

    /**
     * @brief 检查上下文容量
     * @param model 模型配置
     * @param requiredTokens 所需Token数
     * @return 是否满足容量要求
     */
    bool checkContextCapacity(const types::ModelConfig& model, size_t requiredTokens) const;

    /**
     * @brief 生成路由决策
     * @param model 选定的模型
     * @param score 评分
     * @param context 任务上下文
     * @return 路由决策
     */
    RoutingDecision makeDecision(
        const types::ModelConfig& model,
        float score,
        const TaskContext& context
    ) const;

    /**
     * @brief 获取回退模型
     * @return 回退模型ID（如果配置了）
     */
    std::optional<std::string> getFallbackModel() const;
};

} // namespace naw::desktop_pet::service

