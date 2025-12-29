#pragma once

#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/ErrorTypes.h"
#include "naw/desktop_pet/service/types/ModelConfig.h"
#include "naw/desktop_pet/service/types/TaskType.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace naw::desktop_pet::service {

/**
 * @brief 模型健康状态枚举
 */
enum class ModelHealthStatus {
    Healthy,    // 健康
    Degraded,   // 降级（性能下降但可用）
    Unhealthy,  // 不健康（不可用）
    Unknown     // 未知状态
};

/**
 * @brief 模型性能统计结构（内部使用，包含atomic）
 */
struct ModelStatisticsInternal {
    uint64_t totalRequests{0};
    uint64_t successfulRequests{0};
    uint64_t failedRequests{0};
    uint64_t totalResponseTimeMs{0};
    uint64_t responseTimeRecordCount{0};  // 响应时间记录数量
    uint32_t minResponseTimeMs{UINT32_MAX};
    uint32_t maxResponseTimeMs{0};
    std::atomic<uint32_t> currentConcurrency{0};

    // 计算字段
    double getSuccessRate() const {
        if (totalRequests == 0) return 0.0;
        return static_cast<double>(successfulRequests) / static_cast<double>(totalRequests);
    }

    uint32_t getAverageResponseTimeMs() const {
        // 使用responseTimeRecordCount而不是totalRequests，因为响应时间可能独立记录
        if (responseTimeRecordCount == 0) return 0;
        return static_cast<uint32_t>(totalResponseTimeMs / responseTimeRecordCount);
    }
};

/**
 * @brief 模型性能统计结构（可复制的快照）
 */
struct ModelStatistics {
    uint64_t totalRequests{0};
    uint64_t successfulRequests{0};
    uint64_t failedRequests{0};
    uint64_t totalResponseTimeMs{0};
    uint64_t responseTimeRecordCount{0};  // 响应时间记录数量
    uint32_t minResponseTimeMs{UINT32_MAX};
    uint32_t maxResponseTimeMs{0};
    uint32_t currentConcurrency{0};  // 快照值，非atomic

    // 计算字段
    double getSuccessRate() const {
        if (totalRequests == 0) return 0.0;
        return static_cast<double>(successfulRequests) / static_cast<double>(totalRequests);
    }

    uint32_t getAverageResponseTimeMs() const {
        // 使用responseTimeRecordCount而不是totalRequests，因为响应时间可能独立记录
        if (responseTimeRecordCount == 0) return 0;
        return static_cast<uint32_t>(totalResponseTimeMs / responseTimeRecordCount);
    }

    // 从内部结构创建快照
    static ModelStatistics fromInternal(const ModelStatisticsInternal& internal) {
        ModelStatistics snapshot;
        snapshot.totalRequests = internal.totalRequests;
        snapshot.successfulRequests = internal.successfulRequests;
        snapshot.failedRequests = internal.failedRequests;
        snapshot.totalResponseTimeMs = internal.totalResponseTimeMs;
        snapshot.responseTimeRecordCount = internal.responseTimeRecordCount;
        snapshot.minResponseTimeMs = internal.minResponseTimeMs;
        snapshot.maxResponseTimeMs = internal.maxResponseTimeMs;
        snapshot.currentConcurrency = internal.currentConcurrency.load();
        return snapshot;
    }
};

/**
 * @brief 模型管理器：管理所有可用模型及其配置、能力映射、健康状态和性能统计
 */
class ModelManager {
public:
    explicit ModelManager(ConfigManager& configManager);
    ~ModelManager() = default;

    // 禁止拷贝/移动
    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;
    ModelManager(ModelManager&&) = delete;
    ModelManager& operator=(ModelManager&&) = delete;

    // ========== 模型配置加载 ==========
    /**
     * @brief 从配置文件加载模型列表
     * @param err 错误信息输出（可选）
     * @return 是否加载成功
     */
    bool loadModelsFromConfig(ErrorInfo* err = nullptr);

    // ========== 模型注册和管理 ==========
    /**
     * @brief 注册模型
     * @param config 模型配置
     * @param allowOverride 是否允许覆盖已存在的模型
     * @param err 错误信息输出（可选）
     * @return 是否注册成功
     */
    bool registerModel(const types::ModelConfig& config, bool allowOverride = false, ErrorInfo* err = nullptr);

    /**
     * @brief 移除模型
     * @param modelId 模型ID
     * @return 是否移除成功
     */
    bool unregisterModel(const std::string& modelId);

    /**
     * @brief 获取模型配置
     * @param modelId 模型ID
     * @return 模型配置（如果存在）
     */
    std::optional<types::ModelConfig> getModel(const std::string& modelId) const;

    /**
     * @brief 获取所有已注册的模型
     * @return 模型配置列表
     */
    std::vector<types::ModelConfig> getAllModels() const;

    /**
     * @brief 检查模型是否存在
     * @param modelId 模型ID
     * @return 是否存在
     */
    bool hasModel(const std::string& modelId) const;

    // ========== 模型健康状态监控 ==========
    /**
     * @brief 获取模型健康状态
     * @param modelId 模型ID
     * @return 健康状态
     */
    ModelHealthStatus getModelHealth(const std::string& modelId) const;

    /**
     * @brief 更新模型健康状态（基于错误率、响应时间等）
     * @param modelId 模型ID
     * @param success 请求是否成功
     * @param responseTimeMs 响应时间（毫秒）
     */
    void updateModelHealth(const std::string& modelId, bool success, uint32_t responseTimeMs);

    // ========== 模型性能统计 ==========
    /**
     * @brief 记录请求
     * @param modelId 模型ID
     * @param success 是否成功
     */
    void recordRequest(const std::string& modelId, bool success);

    /**
     * @brief 记录响应时间
     * @param modelId 模型ID
     * @param responseTimeMs 响应时间（毫秒）
     */
    void recordResponseTime(const std::string& modelId, uint32_t responseTimeMs);

    /**
     * @brief 获取成功率
     * @param modelId 模型ID
     * @return 成功率（0-1）
     */
    double getSuccessRate(const std::string& modelId) const;

    /**
     * @brief 获取负载因子
     * @param modelId 模型ID
     * @return 负载因子（0-1），0表示无负载，1表示满载
     */
    double getLoadFactor(const std::string& modelId) const;

    /**
     * @brief 获取模型统计信息
     * @param modelId 模型ID
     * @return 统计信息（如果模型存在）
     */
    std::optional<ModelStatistics> getStatistics(const std::string& modelId) const;

    /**
     * @brief 获取所有模型的统计信息
     * @return 模型ID到统计信息的映射
     */
    std::unordered_map<std::string, ModelStatistics> getAllStatistics() const;

    /**
     * @brief 重置统计信息
     * @param modelId 模型ID（如果为空，重置所有模型）
     */
    void resetStatistics(const std::string& modelId = "");

    /**
     * @brief 增加并发计数（请求开始时调用）
     * @param modelId 模型ID
     */
    void incrementConcurrency(const std::string& modelId);

    /**
     * @brief 减少并发计数（请求结束时调用）
     * @param modelId 模型ID
     */
    void decrementConcurrency(const std::string& modelId);

    // ========== 按任务类型查询模型 ==========
    /**
     * @brief 获取支持指定任务的所有模型
     * @param taskType 任务类型
     * @return 模型配置列表（按性能评分排序）
     */
    std::vector<types::ModelConfig> getModelsForTask(types::TaskType taskType) const;

    /**
     * @brief 获取支持指定任务的最佳模型（可选，也可放在TaskRouter中）
     * @param taskType 任务类型
     * @param filterUnhealthy 是否过滤不健康的模型
     * @return 最佳模型配置（如果存在）
     */
    std::optional<types::ModelConfig> getBestModelForTask(
        types::TaskType taskType,
        bool filterUnhealthy = true
    ) const;

private:
    ConfigManager& m_configManager;
    mutable std::mutex m_mutex;

    // 模型存储：modelId -> ModelConfig
    std::unordered_map<std::string, types::ModelConfig> m_models;

    // 任务类型到模型列表的反向索引：TaskType -> vector<modelId>
    std::unordered_map<types::TaskType, std::vector<std::string>> m_taskToModels;

    // 模型健康状态：modelId -> HealthStatus
    std::unordered_map<std::string, ModelHealthStatus> m_healthStatus;

    // 模型统计信息：modelId -> ModelStatisticsInternal
    std::unordered_map<std::string, ModelStatisticsInternal> m_statistics;

    // 健康状态更新参数
    static constexpr uint32_t kMaxConsecutiveFailures = 3;      // 最大连续失败次数
    static constexpr uint32_t kResponseTimeThresholdMs = 10000;  // 响应时间阈值（10秒）
    static constexpr double kFailureRateThreshold = 0.5;         // 失败率阈值（50%）

    // 内部方法（不需要加锁的版本，调用者必须已经持有m_mutex锁）
    bool registerModelInternal(const types::ModelConfig& config, bool allowOverride, ErrorInfo* err);
    void recordRequestInternal(const std::string& modelId, bool success);
    void recordResponseTimeInternal(const std::string& modelId, uint32_t responseTimeMs);
    void updateTaskIndex(const std::string& modelId, const types::ModelConfig& config);
    void removeFromTaskIndex(const std::string& modelId);
    void updateHealthStatusInternal(const std::string& modelId);
};

} // namespace naw::desktop_pet::service

