#include "naw/desktop_pet/service/CacheManager.h"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace naw::desktop_pet::service {

CacheManager::CacheManager(ConfigManager& configManager)
    : m_configManager(configManager)
{
    loadConfiguration();

    // 如果启用缓存，启动清理线程
    if (m_enabled) {
        m_running.store(true);
        m_cleanupThread = std::thread(&CacheManager::cleanupLoop, this);
    }
}

CacheManager::~CacheManager() {
    // 停止清理线程
    if (m_running.load()) {
        m_running.store(false);
        if (m_cleanupThread.joinable()) {
            m_cleanupThread.join();
        }
    }
}

void CacheManager::loadConfiguration() {
    // 读取是否启用缓存
    if (auto v = m_configManager.get("cache.enabled"); v.has_value()) {
        if (v->is_boolean()) {
            m_enabled = v->get<bool>();
        }
    }

    // 读取默认TTL
    if (auto v = m_configManager.get("cache.default_ttl_seconds"); v.has_value()) {
        if (v->is_number_integer()) {
            int val = v->get<int>();
            if (val > 0) {
                m_defaultTtl = std::chrono::seconds(val);
            }
        }
    }

    // 读取最大条目数
    if (auto v = m_configManager.get("cache.max_entries"); v.has_value()) {
        if (v->is_number_unsigned()) {
            m_maxEntries = v->get<size_t>();
        } else if (v->is_number_integer()) {
            int64_t val = v->get<int64_t>();
            if (val > 0) {
                m_maxEntries = static_cast<size_t>(val);
            }
        }
    }

    // 读取清理间隔
    if (auto v = m_configManager.get("cache.cleanup_interval_seconds"); v.has_value()) {
        if (v->is_number_integer()) {
            int val = v->get<int>();
            if (val > 0) {
                m_cleanupInterval = std::chrono::seconds(val);
            }
        }
    }
}

CacheManager::CacheKey CacheManager::generateKey(const types::ChatRequest& request) {
    CacheKey key;
    key.modelId = request.model;

    // 序列化消息列表并计算哈希
    nlohmann::json messagesJson = nlohmann::json::array();
    for (const auto& msg : request.messages) {
        messagesJson.push_back(msg.toJson());
    }
    std::string messagesStr = messagesJson.dump();
    key.messagesHash = std::hash<std::string>{}(messagesStr);

    // 复制可选参数
    key.temperature = request.temperature;
    key.maxTokens = request.maxTokens;
    key.topP = request.topP;
    key.topK = request.topK;
    key.stop = request.stop;

    // 计算tools的哈希
    if (!request.tools.empty()) {
        std::string toolsStr = nlohmann::json(request.tools).dump();
        key.toolsHash = std::hash<std::string>{}(toolsStr);
    } else {
        key.toolsHash = 0;
    }

    return key;
}

std::optional<types::ChatResponse> CacheManager::get(const CacheKey& key) {
    if (!m_enabled) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(m_cacheMutex);

    auto it = m_cache.find(key);
    if (it == m_cache.end()) {
        // 未命中
        {
            std::lock_guard<std::mutex> statLock(m_statisticsMutex);
            m_statistics.totalMisses++;
        }
        return std::nullopt;
    }

    CacheEntry& entry = it->second;

    // 检查是否过期
    if (isExpired(entry)) {
        // 已过期，删除并返回未命中
        m_cache.erase(it);
        {
            std::lock_guard<std::mutex> statLock(m_statisticsMutex);
            m_statistics.totalMisses++;
            m_statistics.totalEntries = m_cache.size();
        }
        return std::nullopt;
    }

    // 命中，更新访问信息
    entry.lastAccessTime = std::chrono::system_clock::now();
    entry.accessCount++;

    // 更新统计
    {
        std::lock_guard<std::mutex> statLock(m_statisticsMutex);
        m_statistics.totalHits++;
    }

    return entry.response;
}

void CacheManager::put(const CacheKey& key, const types::ChatResponse& response,
                       std::optional<std::chrono::seconds> ttl) {
    if (!m_enabled) {
        return;
    }

    // 不缓存流式响应（流式响应不适合缓存）
    // 注意：ChatRequest 中的 stream 字段表示请求是否为流式，但响应本身没有 stream 标记
    // 这里假设所有响应都可以缓存，如果需要区分，可以在 ChatResponse 中添加标记

    std::chrono::seconds actualTtl = ttl.has_value() ? *ttl : m_defaultTtl;

    std::lock_guard<std::mutex> lock(m_cacheMutex);

    // 检查缓存大小限制
    if (m_cache.size() >= m_maxEntries && m_cache.find(key) == m_cache.end()) {
        // 缓存已满且新键不存在，需要淘汰一些条目
        // 优先清理过期条目（使用无锁版本，因为已经持有锁）
        size_t expiredCount = evictExpiredLocked();
        // 如果清理后仍然满，使用LRU淘汰
        if (m_cache.size() >= m_maxEntries) {
            size_t needEvict = m_cache.size() - m_maxEntries + 1; // +1 为新条目留空间
            evictLRULocked(needEvict);
        }
    }

    // 存储或更新缓存条目
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        // 更新现有条目
        it->second.response = response;
        it->second.timestamp = std::chrono::system_clock::now();
        it->second.ttl = actualTtl;
        it->second.lastAccessTime = it->second.timestamp;
    } else {
        // 创建新条目
        m_cache.emplace(key, CacheEntry(response, actualTtl));
    }

    // 更新统计
    {
        std::lock_guard<std::mutex> statLock(m_statisticsMutex);
        m_statistics.totalEntries = m_cache.size();
        // 估算总大小（简化计算）
        size_t entrySize = estimateEntrySize(m_cache.at(key));
        m_statistics.totalSize = 0; // 重新计算总大小
        for (const auto& [k, v] : m_cache) {
            m_statistics.totalSize += estimateEntrySize(v);
        }
    }
}

void CacheManager::clear() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_cache.clear();

    // 重置统计
    {
        std::lock_guard<std::mutex> statLock(m_statisticsMutex);
        m_statistics = CacheStatistics{};
    }
}

CacheManager::CacheStatistics CacheManager::getStatistics() const {
    std::lock_guard<std::mutex> statLock(m_statisticsMutex);
    CacheStatistics stats = m_statistics;

    // 更新当前条目数
    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        stats.totalEntries = m_cache.size();
    }

    return stats;
}

double CacheManager::getHitRate() const {
    std::lock_guard<std::mutex> statLock(m_statisticsMutex);
    return m_statistics.getHitRate();
}

size_t CacheManager::getCacheSize() const {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    return m_cache.size();
}

size_t CacheManager::evictExpired() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    return evictExpiredLocked();
}

size_t CacheManager::evictExpiredLocked() {
    size_t evictedCount = 0;
    auto it = m_cache.begin();
    while (it != m_cache.end()) {
        if (isExpired(it->second)) {
            it = m_cache.erase(it);
            evictedCount++;
        } else {
            ++it;
        }
    }

    // 更新统计
    {
        std::lock_guard<std::mutex> statLock(m_statisticsMutex);
        m_statistics.evictedEntries += evictedCount;
        m_statistics.totalEntries = m_cache.size();
        // 重新计算总大小
        m_statistics.totalSize = 0;
        for (const auto& [k, v] : m_cache) {
            m_statistics.totalSize += estimateEntrySize(v);
        }
    }

    return evictedCount;
}

size_t CacheManager::evictLRU(size_t count) {
    if (count == 0) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_cacheMutex);
    return evictLRULocked(count);
}

size_t CacheManager::evictLRULocked(size_t count) {
    if (count == 0 || m_cache.empty()) {
        return 0;
    }

    // 将条目按最后访问时间排序
    std::vector<std::pair<CacheKey, std::chrono::system_clock::time_point>> entries;
    entries.reserve(m_cache.size());
    for (const auto& [key, entry] : m_cache) {
        entries.emplace_back(key, entry.lastAccessTime);
    }

    // 按最后访问时间排序（最早的在前）
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    // 删除最久未使用的条目
    size_t evictedCount = 0;
    size_t toEvict = std::min(count, entries.size());
    for (size_t i = 0; i < toEvict; ++i) {
        m_cache.erase(entries[i].first);
        evictedCount++;
    }

    // 更新统计
    {
        std::lock_guard<std::mutex> statLock(m_statisticsMutex);
        m_statistics.evictedEntries += evictedCount;
        m_statistics.totalEntries = m_cache.size();
        // 重新计算总大小
        m_statistics.totalSize = 0;
        for (const auto& [k, v] : m_cache) {
            m_statistics.totalSize += estimateEntrySize(v);
        }
    }

    return evictedCount;
}

bool CacheManager::isExpired(const CacheEntry& entry) const {
    auto now = std::chrono::system_clock::now();
    auto expiryTime = entry.timestamp + entry.ttl;
    return now >= expiryTime;
}

bool CacheManager::isValid(const CacheEntry& entry) const {
    return !isExpired(entry);
}

void CacheManager::cleanupLoop() {
    while (m_running.load()) {
        // 等待清理间隔或收到停止信号
        auto deadline = std::chrono::steady_clock::now() + m_cleanupInterval;
        while (m_running.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (!m_running.load()) {
            break;
        }

        // 执行清理
        evictExpired();
    }
}

size_t CacheManager::estimateEntrySize(const CacheEntry& entry) const {
    // 估算缓存条目的大小（字节）
    // 这是一个简化的估算，实际大小可能更大
    size_t size = 0;

    // CacheKey 大小（估算）
    size += sizeof(CacheKey);
    size += entry.response.content.size();
    size += entry.response.toolCalls.size() * 256; // 每个工具调用估算256字节

    // 时间戳和统计
    size += sizeof(entry.timestamp);
    size += sizeof(entry.ttl);
    size += sizeof(entry.lastAccessTime);
    size += sizeof(entry.accessCount);

    return size;
}

} // namespace naw::desktop_pet::service

