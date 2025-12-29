#include "naw/desktop_pet/service/ModelManager.h"

#include "naw/desktop_pet/service/ErrorHandler.h"
#include "naw/desktop_pet/service/types/TaskType.h"

#include <algorithm>
#include <limits>

namespace naw::desktop_pet::service {

ModelManager::ModelManager(ConfigManager& configManager)
    : m_configManager(configManager) {
}

bool ModelManager::loadModelsFromConfig(ErrorInfo* err) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto modelsNode = m_configManager.get("models");
    if (!modelsNode.has_value() || !modelsNode->is_array()) {
        if (err) {
            err->errorType = ErrorType::InvalidRequest;
            err->message = "Config 'models' node is missing or not an array";
        }
        return false;
    }

    std::vector<std::string> errors;
    size_t loadedCount = 0;

    for (const auto& modelJson : *modelsNode) {
        auto config = types::ModelConfig::fromJson(modelJson);
        if (!config.has_value()) {
            errors.push_back("Failed to parse model config from JSON");
            continue;
        }

        std::vector<std::string> validationErrors;
        if (!config->isValid(&validationErrors)) {
            for (const auto& e : validationErrors) {
                errors.push_back("Model " + config->modelId + ": " + e);
            }
            continue;
        }

        // 注册模型（允许覆盖）- 使用内部方法避免死锁
        if (registerModelInternal(*config, true, nullptr)) {
            loadedCount++;
        }
    }

    if (loadedCount == 0 && !errors.empty()) {
        if (err) {
            err->errorType = ErrorType::InvalidRequest;
            err->message = "Failed to load any models. Errors: " + errors[0];
        }
        return false;
    }

    if (!errors.empty() && err) {
        err->errorType = ErrorType::InvalidRequest;
        err->message = "Loaded " + std::to_string(loadedCount) + " models with " +
                       std::to_string(errors.size()) + " errors";
    }

    return loadedCount > 0;
}

bool ModelManager::registerModel(const types::ModelConfig& config, bool allowOverride, ErrorInfo* err) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return registerModelInternal(config, allowOverride, err);
}

bool ModelManager::registerModelInternal(const types::ModelConfig& config, bool allowOverride, ErrorInfo* err) {
    // 注意：调用此方法时，调用者必须已经持有m_mutex锁

    // 验证配置
    std::vector<std::string> validationErrors;
    if (!config.isValid(&validationErrors)) {
        if (err) {
            err->errorType = ErrorType::InvalidRequest;
            err->message = "Invalid model config: " + validationErrors[0];
        }
        return false;
    }

    // 检查是否已存在
    if (m_models.find(config.modelId) != m_models.end() && !allowOverride) {
        if (err) {
            err->errorType = ErrorType::InvalidRequest;
            err->message = "Model " + config.modelId + " already exists";
        }
        return false;
    }

    // 移除旧索引（如果存在）
    if (m_models.find(config.modelId) != m_models.end()) {
        removeFromTaskIndex(config.modelId);
    }

    // 注册模型
    m_models[config.modelId] = config;
    m_healthStatus[config.modelId] = ModelHealthStatus::Unknown;

    // 初始化统计信息（如果不存在）
    if (m_statistics.find(config.modelId) == m_statistics.end()) {
        m_statistics.try_emplace(config.modelId);
    }

    // 更新任务索引
    updateTaskIndex(config.modelId, config);

    return true;
}

bool ModelManager::unregisterModel(const std::string& modelId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_models.find(modelId) == m_models.end()) {
        return false;
    }

    // 从任务索引中移除
    removeFromTaskIndex(modelId);

    // 移除模型
    m_models.erase(modelId);
    m_healthStatus.erase(modelId);
    m_statistics.erase(modelId);

    return true;
}

std::optional<types::ModelConfig> ModelManager::getModel(const std::string& modelId) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_models.find(modelId);
    if (it == m_models.end()) {
        return std::nullopt;
    }

    return it->second;
}

std::vector<types::ModelConfig> ModelManager::getAllModels() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<types::ModelConfig> result;
    result.reserve(m_models.size());

    for (const auto& [modelId, config] : m_models) {
        result.push_back(config);
    }

    return result;
}

bool ModelManager::hasModel(const std::string& modelId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_models.find(modelId) != m_models.end();
}

ModelHealthStatus ModelManager::getModelHealth(const std::string& modelId) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_healthStatus.find(modelId);
    if (it == m_healthStatus.end()) {
        return ModelHealthStatus::Unknown;
    }

    return it->second;
}

void ModelManager::updateModelHealth(const std::string& modelId, bool success, uint32_t responseTimeMs) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 记录请求和响应时间（使用内部方法避免死锁）
    recordRequestInternal(modelId, success);
    recordResponseTimeInternal(modelId, responseTimeMs);

    // 更新健康状态
    updateHealthStatusInternal(modelId);
}

void ModelManager::recordRequest(const std::string& modelId, bool success) {
    std::lock_guard<std::mutex> lock(m_mutex);
    recordRequestInternal(modelId, success);
}

void ModelManager::recordRequestInternal(const std::string& modelId, bool success) {
    // 注意：调用此方法时，调用者必须已经持有m_mutex锁
    auto& stats = m_statistics[modelId];
    stats.totalRequests++;
    if (success) {
        stats.successfulRequests++;
    } else {
        stats.failedRequests++;
    }
}

void ModelManager::recordResponseTime(const std::string& modelId, uint32_t responseTimeMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    recordResponseTimeInternal(modelId, responseTimeMs);
}

void ModelManager::recordResponseTimeInternal(const std::string& modelId, uint32_t responseTimeMs) {
    // 注意：调用此方法时，调用者必须已经持有m_mutex锁
    auto& stats = m_statistics[modelId];
    stats.totalResponseTimeMs += responseTimeMs;
    stats.responseTimeRecordCount++;  // 增加响应时间记录计数
    if (responseTimeMs < stats.minResponseTimeMs) {
        stats.minResponseTimeMs = responseTimeMs;
    }
    if (responseTimeMs > stats.maxResponseTimeMs) {
        stats.maxResponseTimeMs = responseTimeMs;
    }
}

double ModelManager::getSuccessRate(const std::string& modelId) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_statistics.find(modelId);
    if (it == m_statistics.end()) {
        return 0.0;
    }

    return it->second.getSuccessRate();
}

double ModelManager::getLoadFactor(const std::string& modelId) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto modelIt = m_models.find(modelId);
    if (modelIt == m_models.end()) {
        return 1.0; // 模型不存在，视为满载
    }

    auto statsIt = m_statistics.find(modelId);
    if (statsIt == m_statistics.end()) {
        return 0.0; // 无统计信息，视为无负载
    }

    uint32_t maxConcurrency = modelIt->second.maxConcurrentRequests;
    if (maxConcurrency == 0) {
        return 0.0; // 无并发限制，视为无负载
    }

    uint32_t currentConcurrency = statsIt->second.currentConcurrency.load();
    return std::min(1.0, static_cast<double>(currentConcurrency) / static_cast<double>(maxConcurrency));
}

std::optional<ModelStatistics> ModelManager::getStatistics(const std::string& modelId) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_statistics.find(modelId);
    if (it == m_statistics.end()) {
        return std::nullopt;
    }

    return ModelStatistics::fromInternal(it->second);
}

std::unordered_map<std::string, ModelStatistics> ModelManager::getAllStatistics() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::unordered_map<std::string, ModelStatistics> result;
    for (const auto& [modelId, internal] : m_statistics) {
        result[modelId] = ModelStatistics::fromInternal(internal);
    }
    return result;
}

void ModelManager::resetStatistics(const std::string& modelId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (modelId.empty()) {
        // 重置所有统计
        for (auto& [id, stats] : m_statistics) {
            stats.totalRequests = 0;
            stats.successfulRequests = 0;
            stats.failedRequests = 0;
            stats.totalResponseTimeMs = 0;
            stats.responseTimeRecordCount = 0;
            stats.minResponseTimeMs = UINT32_MAX;
            stats.maxResponseTimeMs = 0;
            stats.currentConcurrency.store(0);
        }
    } else {
        // 重置指定模型
        auto it = m_statistics.find(modelId);
        if (it != m_statistics.end()) {
            auto& stats = it->second;
            stats.totalRequests = 0;
            stats.successfulRequests = 0;
            stats.failedRequests = 0;
            stats.totalResponseTimeMs = 0;
            stats.responseTimeRecordCount = 0;
            stats.minResponseTimeMs = UINT32_MAX;
            stats.maxResponseTimeMs = 0;
            stats.currentConcurrency.store(0);
        }
    }
}

void ModelManager::incrementConcurrency(const std::string& modelId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto& stats = m_statistics[modelId];
    stats.currentConcurrency++;
}

void ModelManager::decrementConcurrency(const std::string& modelId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_statistics.find(modelId);
    if (it != m_statistics.end()) {
        auto& stats = it->second;
        if (stats.currentConcurrency.load() > 0) {
            stats.currentConcurrency--;
        }
    }
}

std::vector<types::ModelConfig> ModelManager::getModelsForTask(types::TaskType taskType) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<types::ModelConfig> result;

    auto it = m_taskToModels.find(taskType);
    if (it == m_taskToModels.end()) {
        return result;
    }

    // 获取所有支持该任务的模型
    for (const auto& modelId : it->second) {
        auto modelIt = m_models.find(modelId);
        if (modelIt != m_models.end()) {
            result.push_back(modelIt->second);
        }
    }

    // 按性能评分排序（降序）
    std::sort(result.begin(), result.end(),
              [](const types::ModelConfig& a, const types::ModelConfig& b) {
                  return a.performanceScore > b.performanceScore;
              });

    return result;
}

std::optional<types::ModelConfig> ModelManager::getBestModelForTask(
    types::TaskType taskType,
    bool filterUnhealthy
) const {
    auto models = getModelsForTask(taskType);
    if (models.empty()) {
        return std::nullopt;
    }

    // 过滤不健康的模型
    if (filterUnhealthy) {
        std::vector<types::ModelConfig> healthyModels;
        for (const auto& model : models) {
            auto health = getModelHealth(model.modelId);
            // 允许Unknown状态，因为新注册的模型可能还没有足够的统计数据
            if (health == ModelHealthStatus::Healthy || 
                health == ModelHealthStatus::Degraded ||
                health == ModelHealthStatus::Unknown) {
                healthyModels.push_back(model);
            }
        }
        if (healthyModels.empty()) {
            return std::nullopt;
        }
        models = std::move(healthyModels);
    }

    // 返回性能评分最高的模型（getModelsForTask已经按性能评分排序）
    return models[0];
}

void ModelManager::updateTaskIndex(const std::string& modelId, const types::ModelConfig& config) {
    // 为每个支持的任务类型添加模型ID到索引
    for (auto taskType : config.supportedTasks) {
        auto& modelList = m_taskToModels[taskType];
        // 检查是否已存在
        if (std::find(modelList.begin(), modelList.end(), modelId) == modelList.end()) {
            modelList.push_back(modelId);
        }
    }
}

void ModelManager::removeFromTaskIndex(const std::string& modelId) {
    // 从所有任务类型的索引中移除模型ID
    for (auto& [taskType, modelList] : m_taskToModels) {
        modelList.erase(
            std::remove(modelList.begin(), modelList.end(), modelId),
            modelList.end()
        );
    }

    // 清理空的任务类型条目
    auto it = m_taskToModels.begin();
    while (it != m_taskToModels.end()) {
        if (it->second.empty()) {
            it = m_taskToModels.erase(it);
        } else {
            ++it;
        }
    }
}

void ModelManager::updateHealthStatusInternal(const std::string& modelId) {
    auto statsIt = m_statistics.find(modelId);
    if (statsIt == m_statistics.end()) {
        m_healthStatus[modelId] = ModelHealthStatus::Unknown;
        return;
    }

    const auto& stats = statsIt->second;

    // 计算失败率
    double failureRate = 0.0;
    if (stats.totalRequests > 0) {
        failureRate = static_cast<double>(stats.failedRequests) / static_cast<double>(stats.totalRequests);
    }

    // 计算平均响应时间
    uint32_t avgResponseTime = stats.getAverageResponseTimeMs();

    // 判断健康状态
    // 1. 如果失败率超过阈值，标记为不健康
    if (failureRate > kFailureRateThreshold) {
        m_healthStatus[modelId] = ModelHealthStatus::Unhealthy;
        return;
    }

    // 2. 如果平均响应时间超过阈值，标记为降级
    if (avgResponseTime > kResponseTimeThresholdMs) {
        m_healthStatus[modelId] = ModelHealthStatus::Degraded;
        return;
    }

    // 3. 检查最近连续失败次数（需要跟踪最近N次请求的状态，这里简化处理）
    // 如果总失败数较多但失败率未超阈值，可能是降级状态
    if (stats.failedRequests > kMaxConsecutiveFailures && failureRate > 0.2) {
        m_healthStatus[modelId] = ModelHealthStatus::Degraded;
        return;
    }

    // 4. 如果请求数较少，保持未知状态
    if (stats.totalRequests < 3) {
        m_healthStatus[modelId] = ModelHealthStatus::Unknown;
        return;
    }

    // 5. 其他情况视为健康
    m_healthStatus[modelId] = ModelHealthStatus::Healthy;
}

} // namespace naw::desktop_pet::service

