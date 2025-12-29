#pragma once

#include "naw/desktop_pet/service/APIClient.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/ErrorTypes.h"
#include "naw/desktop_pet/service/ModelManager.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"
#include "naw/desktop_pet/service/types/TaskPriority.h"
#include "naw/desktop_pet/service/types/TaskType.h"
#include "naw/desktop_pet/service/utils/HttpClient.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace naw::desktop_pet::service {

/**
 * @brief 请求管理器：管理请求队列、并发控制和请求调度
 *
 * 功能：
 * - 优先级队列：按优先级和时间戳排序
 * - 并发控制：按模型限制并发数
 * - 请求调度：工作线程处理队列请求
 * - 超时管理：请求超时检测和处理
 * - 取消机制：支持取消队列中和处理中的请求
 * - 统计功能：请求统计和性能监控
 */
class RequestManager {
public:
    /**
     * @brief 请求项结构
     */
    struct RequestItem {
        std::string requestId;                              // 请求ID，唯一标识
        types::ChatRequest request;                         // 请求对象
        types::TaskType taskType;                           // 任务类型
        types::TaskPriority priority;                       // 任务优先级
        std::string modelId;                                // 选定的模型ID
        std::chrono::system_clock::time_point timestamp;   // 提交时间
        std::promise<types::ChatResponse> promise;         // 用于异步返回结果
        utils::HttpClient::CancelToken cancelToken;        // 取消令牌

        RequestItem(std::string id, types::ChatRequest req, types::TaskType type,
                    types::TaskPriority prio, std::string model)
            : requestId(std::move(id))
            , request(std::move(req))
            , taskType(type)
            , priority(prio)
            , modelId(std::move(model))
            , timestamp(std::chrono::system_clock::now())
            , cancelToken({std::make_shared<std::atomic<bool>>(false)})
        {}
    };

    /**
     * @brief 优先级比较器（用于priority_queue，较小的优先级更高）
     */
    struct CompareRequestPriority {
        bool operator()(const RequestItem& a, const RequestItem& b) const {
            // 优先级：Critical > High > Normal > Low
            auto rank = [](types::TaskPriority p) -> int {
                switch (p) {
                    case types::TaskPriority::Critical: return 0;
                    case types::TaskPriority::High: return 1;
                    case types::TaskPriority::Normal: return 2;
                    case types::TaskPriority::Low: return 3;
                }
                return 2;
            };

            int rankA = rank(a.priority);
            int rankB = rank(b.priority);
            if (rankA != rankB) {
                // 优先级不同：较小的rank（更高优先级）排在前面
                return rankA > rankB;
            }
            // 同优先级按时间戳排序（FIFO，较早的排在前面）
            return a.timestamp > b.timestamp;
        }
    };

    /**
     * @brief 请求统计结构
     */
    struct RequestStatistics {
        uint64_t totalRequests{0};           // 总请求数
        uint64_t completedRequests{0};       // 完成请求数
        uint64_t failedRequests{0};          // 失败请求数
        uint64_t cancelledRequests{0};       // 取消请求数
        uint64_t totalResponseTimeMs{0};     // 总响应时间（毫秒）
        uint64_t responseTimeRecordCount{0}; // 响应时间记录数量
        uint32_t minResponseTimeMs{UINT32_MAX}; // 最小响应时间
        uint32_t maxResponseTimeMs{0};       // 最大响应时间
        std::unordered_map<std::string, uint64_t> requestsPerModel; // 按模型的请求数统计
        size_t queueSize{0};                 // 当前队列大小
        size_t maxQueueSize{0};              // 最大队列大小

        // 计算平均响应时间
        uint32_t getAverageResponseTimeMs() const {
            if (responseTimeRecordCount == 0) return 0;
            return static_cast<uint32_t>(totalResponseTimeMs / responseTimeRecordCount);
        }
    };

    /**
     * @brief 队列统计结构
     */
    struct QueueStatistics {
        size_t currentSize{0};               // 当前队列大小
        size_t maxSize{0};                   // 最大队列大小
        uint64_t totalEnqueued{0};           // 总入队数
        uint64_t totalDequeued{0};           // 总出队数
    };

    explicit RequestManager(ConfigManager& configManager, APIClient& apiClient,
                            ModelManager& modelManager);
    ~RequestManager();

    // 禁止拷贝/移动
    RequestManager(const RequestManager&) = delete;
    RequestManager& operator=(const RequestManager&) = delete;
    RequestManager(RequestManager&&) = delete;
    RequestManager& operator=(RequestManager&&) = delete;

    // ========== 生命周期管理 ==========
    /**
     * @brief 启动请求管理器（启动工作线程）
     */
    void start();

    /**
     * @brief 停止请求管理器（停止工作线程，等待队列处理完成）
     */
    void stop();

    /**
     * @brief 检查是否正在运行
     */
    bool isRunning() const { return m_running.load(); }

    // ========== 请求提交 ==========
    /**
     * @brief 提交请求到队列
     * @param request 请求对象
     * @param taskType 任务类型
     * @param priority 任务优先级
     * @param modelId 选定的模型ID
     * @return future，用于获取异步结果
     */
    std::future<types::ChatResponse> enqueueRequest(
        const types::ChatRequest& request,
        types::TaskType taskType,
        types::TaskPriority priority,
        const std::string& modelId);

    // ========== 请求取消 ==========
    /**
     * @brief 取消请求
     * @param requestId 请求ID
     * @return 是否成功取消（请求不存在返回false）
     */
    bool cancelRequest(const std::string& requestId);

    // ========== 并发控制查询 ==========
    /**
     * @brief 获取指定模型的当前并发数
     */
    uint32_t getCurrentConcurrency(const std::string& modelId) const;

    /**
     * @brief 获取全局总并发数
     */
    uint32_t getTotalConcurrency() const;

    /**
     * @brief 获取指定模型的并发限制
     */
    uint32_t getConcurrencyLimit(const std::string& modelId) const;

    // ========== 统计查询 ==========
    /**
     * @brief 获取请求统计信息
     */
    RequestStatistics getStatistics() const;

    /**
     * @brief 获取队列统计信息
     */
    QueueStatistics getQueueStatistics() const;

private:
    ConfigManager& m_configManager;
    APIClient& m_apiClient;
    ModelManager& m_modelManager;

    // 运行状态
    std::atomic<bool> m_running{false};
    std::thread m_workerThread;

    // 请求队列（优先级队列）
    mutable std::mutex m_queueMutex;
    std::priority_queue<RequestItem, std::vector<RequestItem>, CompareRequestPriority> m_requestQueue;
    std::condition_variable m_queueCondition;

    // 队列配置
    size_t m_maxQueueSize{1000};
    int m_defaultTimeoutMs{30000};

    // 并发控制（按模型）
    mutable std::mutex m_concurrencyMutex;
    std::unordered_map<std::string, std::atomic<uint32_t>> m_modelConcurrency;
    std::atomic<uint32_t> m_totalConcurrency{0};

    // 正在处理的请求（用于取消）
    mutable std::mutex m_activeRequestsMutex;
    std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>> m_activeCancels;

    // 统计数据
    mutable std::mutex m_statisticsMutex;
    RequestStatistics m_statistics;
    QueueStatistics m_queueStatistics;

    // ========== 内部方法 ==========
    /**
     * @brief 生成唯一请求ID
     */
    std::string generateRequestId() const;

    /**
     * @brief 从队列取出请求（线程安全）
     */
    std::optional<RequestItem> dequeueRequest(); // RequestItem is a nested type

    /**
     * @brief 检查队列是否已满
     */
    bool isQueueFull() const;

    /**
     * @brief 检查并发限制
     */
    bool checkConcurrencyLimit(const std::string& modelId) const;

    /**
     * @brief 获取并发槽位
     */
    bool acquireConcurrencySlot(const std::string& modelId);

    /**
     * @brief 释放并发槽位
     */
    void releaseConcurrencySlot(const std::string& modelId);

    /**
     * @brief 队列处理循环（工作线程主循环）
     */
    void processQueue();

    /**
     * @brief 分发请求到API客户端
     */
    void dispatchRequest(RequestItem& item);

    /**
     * @brief 更新统计信息（请求开始）
     */
    void updateStatisticsOnStart(const std::string& modelId);

    /**
     * @brief 更新统计信息（请求完成）
     */
    void updateStatisticsOnComplete(const std::string& modelId, uint32_t responseTimeMs);

    /**
     * @brief 更新统计信息（请求失败）
     */
    void updateStatisticsOnFailure(const std::string& modelId);

    /**
     * @brief 更新统计信息（请求取消）
     */
    void updateStatisticsOnCancel(const std::string& modelId);

    /**
     * @brief 从配置读取参数
     */
    void loadConfiguration();
};

} // namespace naw::desktop_pet::service

