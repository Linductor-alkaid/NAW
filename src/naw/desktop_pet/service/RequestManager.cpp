#include "naw/desktop_pet/service/RequestManager.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>
#include <stdexcept>

namespace naw::desktop_pet::service {

RequestManager::RequestManager(ConfigManager& configManager, APIClient& apiClient,
                               ModelManager& modelManager)
    : m_configManager(configManager)
    , m_apiClient(apiClient)
    , m_modelManager(modelManager)
{
    loadConfiguration();
}

RequestManager::~RequestManager() {
    stop();
}

void RequestManager::loadConfiguration() {
    // 读取队列大小限制
    if (auto v = m_configManager.get("request_manager.max_queue_size"); v.has_value()) {
        if (v->is_number_unsigned()) {
            m_maxQueueSize = v->get<size_t>();
        } else if (v->is_number_integer()) {
            int64_t val = v->get<int64_t>();
            if (val > 0) m_maxQueueSize = static_cast<size_t>(val);
        }
    }

    // 读取默认超时时间
    if (auto v = m_configManager.get("request_manager.default_timeout_ms"); v.has_value()) {
        if (v->is_number_integer()) {
            int val = v->get<int>();
            if (val > 0) m_defaultTimeoutMs = val;
        }
    }

    // 初始化统计信息
    {
        std::lock_guard<std::mutex> lock(m_statisticsMutex);
        m_statistics.maxQueueSize = m_maxQueueSize;
    }
    {
        std::lock_guard<std::mutex> lock(m_statisticsMutex);
        m_queueStatistics.maxSize = m_maxQueueSize;
    }
}

void RequestManager::start() {
    if (m_running.load()) {
        return; // 已经在运行
    }

    m_running.store(true);
    m_workerThread = std::thread(&RequestManager::processQueue, this);
}

void RequestManager::stop() {
    if (!m_running.load()) {
        return; // 已经停止
    }

    m_running.store(false);
    m_queueCondition.notify_all(); // 唤醒工作线程

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

std::string RequestManager::generateRequestId() const {
    // 使用时间戳 + 随机数生成唯一ID
    static std::atomic<uint64_t> counter{0};
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now.time_since_epoch())
                         .count();
    uint64_t count = counter.fetch_add(1);

    std::ostringstream oss;
    oss << "req_" << timestamp << "_" << count;
    return oss.str();
}

bool RequestManager::isQueueFull() const {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_requestQueue.size() >= m_maxQueueSize;
}

std::future<types::ChatResponse> RequestManager::enqueueRequest(
    const types::ChatRequest& request,
    types::TaskType taskType,
    types::TaskPriority priority,
    const std::string& modelId) {
    // 检查队列是否已满
    if (isQueueFull()) {
        // 队列满时，创建promise并立即设置错误
        std::promise<types::ChatResponse> promise;
        promise.set_exception(std::make_exception_ptr(
            std::runtime_error("Request queue is full")));
        return promise.get_future();
    }

    // 生成请求ID
    std::string requestId = generateRequestId();

    // 创建RequestItem
    RequestManager::RequestItem item(requestId, request, taskType, priority, modelId);
    std::future<types::ChatResponse> future = item.promise.get_future();

    // 入队
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_requestQueue.push(std::move(item));
        
        // 更新队列统计
        {
            std::lock_guard<std::mutex> statLock(m_statisticsMutex);
            m_queueStatistics.currentSize = m_requestQueue.size();
            m_queueStatistics.totalEnqueued++;
            m_statistics.queueSize = m_requestQueue.size();
        }
    }

    // 通知工作线程
    m_queueCondition.notify_one();

    // 更新统计（总请求数）
    {
        std::lock_guard<std::mutex> lock(m_statisticsMutex);
        m_statistics.totalRequests++;
        m_statistics.requestsPerModel[modelId]++;
    }

    return future;
}

std::optional<RequestManager::RequestItem> RequestManager::dequeueRequest() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_requestQueue.empty()) {
        return std::nullopt;
    }

    RequestItem item = std::move(const_cast<RequestItem&>(m_requestQueue.top()));
    m_requestQueue.pop();

    // 更新队列统计
    {
        std::lock_guard<std::mutex> statLock(m_statisticsMutex);
        m_queueStatistics.currentSize = m_requestQueue.size();
        m_queueStatistics.totalDequeued++;
        m_statistics.queueSize = m_requestQueue.size();
    }

    return item;
}

bool RequestManager::checkConcurrencyLimit(const std::string& modelId) const {
    // 获取模型配置
    auto modelConfig = m_modelManager.getModel(modelId);
    if (!modelConfig.has_value()) {
        return false; // 模型不存在
    }

    uint32_t maxConcurrent = modelConfig->maxConcurrentRequests;
    if (maxConcurrent == 0) {
        return true; // 无限制
    }

    // 获取当前并发数
    uint32_t current = getCurrentConcurrency(modelId);
    return current < maxConcurrent;
}

uint32_t RequestManager::getCurrentConcurrency(const std::string& modelId) const {
    std::lock_guard<std::mutex> lock(m_concurrencyMutex);
    auto it = m_modelConcurrency.find(modelId);
    if (it == m_modelConcurrency.end()) {
        return 0;
    }
    return it->second.load();
}

uint32_t RequestManager::getTotalConcurrency() const {
    return m_totalConcurrency.load();
}

uint32_t RequestManager::getConcurrencyLimit(const std::string& modelId) const {
    auto modelConfig = m_modelManager.getModel(modelId);
    if (!modelConfig.has_value()) {
        return 0;
    }
    return modelConfig->maxConcurrentRequests;
}

bool RequestManager::acquireConcurrencySlot(const std::string& modelId) {
    // 检查并发限制
    if (!checkConcurrencyLimit(modelId)) {
        return false;
    }

    // 增加并发计数
    {
        std::lock_guard<std::mutex> lock(m_concurrencyMutex);
        m_modelConcurrency[modelId]++;
        m_totalConcurrency++;
    }

    return true;
}

void RequestManager::releaseConcurrencySlot(const std::string& modelId) {
    std::lock_guard<std::mutex> lock(m_concurrencyMutex);
    auto it = m_modelConcurrency.find(modelId);
    if (it != m_modelConcurrency.end()) {
        uint32_t current = it->second.load();
        if (current > 0) {
            it->second--;
            m_totalConcurrency--;
        }
    }
}

bool RequestManager::cancelRequest(const std::string& requestId) {
    // 查找正在处理的请求
    {
        std::lock_guard<std::mutex> lock(m_activeRequestsMutex);
        auto it = m_activeCancels.find(requestId);
        if (it != m_activeCancels.end()) {
            // 设置取消标志
            it->second->store(true);
            return true;
        }
    }

    // 如果不在处理中，可能在队列中，尝试从队列中移除
    // 注意：由于priority_queue的限制，需要重建队列来移除特定项
    // 这里简化处理：标记为已取消，在出队时检查
    // 更高效的实现可以使用额外的map来跟踪队列中的请求
    // 为了简化，这里只处理正在处理的请求的取消

    return false;
}

void RequestManager::dispatchRequest(RequestItem& item) {
    // 检查取消标志
    if (item.cancelToken.cancelled && item.cancelToken.cancelled->load()) {
        item.promise.set_exception(std::make_exception_ptr(
            std::runtime_error("Request cancelled")));
        updateStatisticsOnCancel(item.modelId);
        return;
    }

    // 记录开始时间
    auto startTime = std::chrono::system_clock::now();

    // 注册取消令牌
    {
        std::lock_guard<std::mutex> lock(m_activeRequestsMutex);
        m_activeCancels[item.requestId] = item.cancelToken.cancelled;
    }

    // 更新统计（请求开始）
    updateStatisticsOnStart(item.modelId);

    try {
        // 判断是否为流式请求
        bool isStream = item.request.stream.value_or(false);

        if (isStream) {
            // 流式请求处理（简化版：聚合后返回）
            // 注意：完整的流式处理应该在ResponseHandler中实现
            // 这里先使用非流式方式作为占位
            auto future = m_apiClient.chatAsync(item.request, &item.cancelToken);
            auto response = future.get();
            
            // 计算响应时间
            auto endTime = std::chrono::system_clock::now();
            auto responseTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                endTime - startTime).count();

            item.promise.set_value(response);
            updateStatisticsOnComplete(item.modelId, static_cast<uint32_t>(responseTime));
        } else {
            // 非流式请求
            auto future = m_apiClient.chatAsync(item.request, &item.cancelToken);
            auto response = future.get();
            
            // 计算响应时间
            auto endTime = std::chrono::system_clock::now();
            auto responseTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                endTime - startTime).count();

            item.promise.set_value(response);
            updateStatisticsOnComplete(item.modelId, static_cast<uint32_t>(responseTime));
        }
    } catch (const APIClient::ApiClientError& e) {
        // API客户端错误
        auto endTime = std::chrono::system_clock::now();
        auto responseTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();

        // 检查是否为取消
        if (item.cancelToken.cancelled && item.cancelToken.cancelled->load()) {
            item.promise.set_exception(std::make_exception_ptr(
                std::runtime_error("Request cancelled")));
            updateStatisticsOnCancel(item.modelId);
        } else {
            // 创建错误响应
            types::ChatResponse errorResponse;
            errorResponse.content = std::string("Error: ") + e.what();
            item.promise.set_value(errorResponse);
            updateStatisticsOnFailure(item.modelId);
        }
    } catch (const std::exception& e) {
        // 其他异常
        auto endTime = std::chrono::system_clock::now();
        
        if (item.cancelToken.cancelled && item.cancelToken.cancelled->load()) {
            item.promise.set_exception(std::make_exception_ptr(
                std::runtime_error("Request cancelled")));
            updateStatisticsOnCancel(item.modelId);
        } else {
            types::ChatResponse errorResponse;
            errorResponse.content = std::string("Error: ") + e.what();
            item.promise.set_value(errorResponse);
            updateStatisticsOnFailure(item.modelId);
        }
    }

    // 移除取消令牌注册
    {
        std::lock_guard<std::mutex> lock(m_activeRequestsMutex);
        m_activeCancels.erase(item.requestId);
    }
}

void RequestManager::processQueue() {
    while (m_running.load()) {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        
        // 等待队列非空或停止信号
        m_queueCondition.wait(lock, [this] {
            return !m_requestQueue.empty() || !m_running.load();
        });

        // 如果停止且队列为空，退出
        if (!m_running.load() && m_requestQueue.empty()) {
            break;
        }

        // 如果队列为空，继续等待
        if (m_requestQueue.empty()) {
            continue;
        }

        // 取出顶部请求
        RequestItem itemToProcess = std::move(const_cast<RequestItem&>(m_requestQueue.top()));
        m_requestQueue.pop();
        
        // 更新队列统计
        {
            std::lock_guard<std::mutex> statLock(m_statisticsMutex);
            m_queueStatistics.currentSize = m_requestQueue.size();
            m_queueStatistics.totalDequeued++;
            m_statistics.queueSize = m_requestQueue.size();
        }

        lock.unlock();

        // 尝试获取并发槽位
        if (!acquireConcurrencySlot(itemToProcess.modelId)) {
            // 无法获取槽位，重新入队
            std::unique_lock<std::mutex> reLock(m_queueMutex);
            m_requestQueue.push(std::move(itemToProcess));
            
            // 更新队列统计
            {
                std::lock_guard<std::mutex> statLock(m_statisticsMutex);
                m_queueStatistics.currentSize = m_requestQueue.size();
                m_statistics.queueSize = m_requestQueue.size();
            }
            
            reLock.unlock();
            // 短暂休眠后重试，等待并发槽位释放
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // 在单独的线程中处理请求，避免阻塞队列处理循环
        std::thread([this](RequestManager::RequestItem item) {
            dispatchRequest(item);
            releaseConcurrencySlot(item.modelId);
            // 通知队列条件变量，可能有新的请求可以处理
            m_queueCondition.notify_one();
        }, std::move(itemToProcess)).detach();
    }
}

void RequestManager::updateStatisticsOnStart(const std::string& modelId) {
    std::lock_guard<std::mutex> lock(m_statisticsMutex);
    // 请求开始时不需要更新统计（已在enqueueRequest中更新）
}

void RequestManager::updateStatisticsOnComplete(const std::string& modelId,
                                                uint32_t responseTimeMs) {
    std::lock_guard<std::mutex> lock(m_statisticsMutex);
    m_statistics.completedRequests++;
    m_statistics.totalResponseTimeMs += responseTimeMs;
    m_statistics.responseTimeRecordCount++;
    
    if (responseTimeMs < m_statistics.minResponseTimeMs) {
        m_statistics.minResponseTimeMs = responseTimeMs;
    }
    if (responseTimeMs > m_statistics.maxResponseTimeMs) {
        m_statistics.maxResponseTimeMs = responseTimeMs;
    }
}

void RequestManager::updateStatisticsOnFailure(const std::string& modelId) {
    std::lock_guard<std::mutex> lock(m_statisticsMutex);
    m_statistics.failedRequests++;
}

void RequestManager::updateStatisticsOnCancel(const std::string& modelId) {
    std::lock_guard<std::mutex> lock(m_statisticsMutex);
    m_statistics.cancelledRequests++;
}

RequestManager::RequestStatistics RequestManager::getStatistics() const {
    std::lock_guard<std::mutex> lock(m_statisticsMutex);
    return m_statistics;
}

RequestManager::QueueStatistics RequestManager::getQueueStatistics() const {
    std::lock_guard<std::mutex> lock(m_statisticsMutex);
    return m_queueStatistics;
}

} // namespace naw::desktop_pet::service

