#pragma once

#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

namespace naw::desktop_pet::service {

/**
 * @brief 缓存管理器：基于请求内容的响应缓存
 *
 * 功能：
 * - 缓存键生成：基于请求内容生成唯一缓存键
 * - 内存缓存：使用 unordered_map 存储缓存条目
 * - 缓存查询：支持缓存命中/未命中查询
 * - TTL管理：支持缓存条目的生存时间管理
 * - 过期清理：定期清理过期缓存条目
 * - LRU淘汰：支持最久未使用淘汰策略
 * - 统计功能：缓存命中率、条目数等统计
 */
class CacheManager {
public:
    /**
     * @brief 缓存键结构
     */
    struct CacheKey {
        std::string modelId;                      // 模型ID
        std::size_t messagesHash;                 // 消息内容的哈希值
        std::optional<float> temperature;         // 温度参数
        std::optional<uint32_t> maxTokens;        // 最大Token数
        std::optional<float> topP;                // Top-p参数
        std::optional<uint32_t> topK;             // Top-k参数
        std::optional<std::string> stop;          // 停止序列
        std::size_t toolsHash;                    // tools数组的哈希值

        bool operator==(const CacheKey& other) const {
            return modelId == other.modelId && messagesHash == other.messagesHash &&
                   temperature == other.temperature && maxTokens == other.maxTokens &&
                   topP == other.topP && topK == other.topK && stop == other.stop &&
                   toolsHash == other.toolsHash;
        }
    };

    /**
     * @brief 缓存键哈希函数对象
     */
    struct CacheKeyHash {
        std::size_t operator()(const CacheKey& key) const {
            std::size_t h1 = std::hash<std::string>{}(key.modelId);
            std::size_t h2 = key.messagesHash;
            std::size_t h3 = key.toolsHash;

            // 组合可选参数的哈希
            std::size_t h4 = 0;
            if (key.temperature.has_value()) {
                h4 ^= std::hash<float>{}(*key.temperature) + 0x9e3779b9 + (h4 << 6) + (h4 >> 2);
            }
            if (key.maxTokens.has_value()) {
                h4 ^= std::hash<uint32_t>{}(*key.maxTokens) + 0x9e3779b9 + (h4 << 6) + (h4 >> 2);
            }
            if (key.topP.has_value()) {
                h4 ^= std::hash<float>{}(*key.topP) + 0x9e3779b9 + (h4 << 6) + (h4 >> 2);
            }
            if (key.topK.has_value()) {
                h4 ^= std::hash<uint32_t>{}(*key.topK) + 0x9e3779b9 + (h4 << 6) + (h4 >> 2);
            }
            if (key.stop.has_value()) {
                h4 ^= std::hash<std::string>{}(*key.stop) + 0x9e3779b9 + (h4 << 6) + (h4 >> 2);
            }

            // 组合所有哈希值
            return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
        }
    };

    /**
     * @brief 缓存条目结构
     */
    struct CacheEntry {
        types::ChatResponse response;                                    // 缓存的响应
        std::chrono::system_clock::time_point timestamp;                 // 存储时间戳
        std::chrono::seconds ttl;                                        // 生存时间
        std::chrono::system_clock::time_point lastAccessTime;            // 最后访问时间（用于LRU）
        uint64_t accessCount{0};                                         // 访问次数

        CacheEntry(const types::ChatResponse& resp, std::chrono::seconds ttlSeconds)
            : response(resp)
            , timestamp(std::chrono::system_clock::now())
            , ttl(ttlSeconds)
            , lastAccessTime(timestamp)
        {}
    };

    /**
     * @brief 缓存统计结构
     */
    struct CacheStatistics {
        uint64_t totalHits{0};           // 总命中数
        uint64_t totalMisses{0};         // 总未命中数
        size_t totalEntries{0};          // 当前缓存条目数
        size_t totalSize{0};             // 当前缓存大小（字节，估算）
        uint64_t evictedEntries{0};      // 被淘汰的条目数

        // 计算命中率
        double getHitRate() const {
            uint64_t total = totalHits + totalMisses;
            if (total == 0) return 0.0;
            return static_cast<double>(totalHits) / static_cast<double>(total);
        }
    };

    explicit CacheManager(ConfigManager& configManager);
    ~CacheManager();

    // 禁止拷贝/移动
    CacheManager(const CacheManager&) = delete;
    CacheManager& operator=(const CacheManager&) = delete;
    CacheManager(CacheManager&&) = delete;
    CacheManager& operator=(CacheManager&&) = delete;

    // ========== 缓存操作 ==========
    /**
     * @brief 生成缓存键
     * @param request 请求对象
     * @return 缓存键
     */
    CacheKey generateKey(const types::ChatRequest& request);

    /**
     * @brief 查询缓存
     * @param key 缓存键
     * @return 缓存的响应，如果未命中或已过期则返回 nullopt
     */
    std::optional<types::ChatResponse> get(const CacheKey& key);

    /**
     * @brief 存储缓存
     * @param key 缓存键
     * @param response 响应对象
     * @param ttl 生存时间（可选，如果为nullopt则使用默认TTL）
     */
    void put(const CacheKey& key, const types::ChatResponse& response,
             std::optional<std::chrono::seconds> ttl = std::nullopt);

    /**
     * @brief 清空所有缓存
     */
    void clear();

    // ========== 统计查询 ==========
    /**
     * @brief 获取缓存统计信息
     */
    CacheStatistics getStatistics() const;

    /**
     * @brief 获取缓存命中率
     * @return 命中率（0-1）
     */
    double getHitRate() const;

    /**
     * @brief 获取当前缓存条目数
     */
    size_t getCacheSize() const;

    // ========== 清理控制 ==========
    /**
     * @brief 清理过期条目
     * @return 清理的条目数
     */
    size_t evictExpired();

    /**
     * @brief 使用LRU策略淘汰最久未使用的条目
     * @param count 要淘汰的条目数
     * @return 实际淘汰的条目数
     */
    size_t evictLRU(size_t count);

private:
    ConfigManager& m_configManager;

    // 缓存存储
    mutable std::mutex m_cacheMutex;
    std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> m_cache;

    // 配置参数
    bool m_enabled{true};
    std::chrono::seconds m_defaultTtl{std::chrono::hours(1)}; // 默认1小时
    size_t m_maxEntries{1000};
    std::chrono::seconds m_cleanupInterval{std::chrono::minutes(5)}; // 默认5分钟

    // 清理线程
    std::atomic<bool> m_running{false};
    std::thread m_cleanupThread;

    // 统计数据
    mutable std::mutex m_statisticsMutex;
    CacheStatistics m_statistics;

    // ========== 内部方法 ==========
    /**
     * @brief 从配置读取参数
     */
    void loadConfiguration();

    /**
     * @brief 检查条目是否过期
     */
    bool isExpired(const CacheEntry& entry) const;

    /**
     * @brief 检查条目是否有效
     */
    bool isValid(const CacheEntry& entry) const;

    /**
     * @brief 清理线程主循环
     */
    void cleanupLoop();

    /**
     * @brief 估算缓存条目大小（字节）
     */
    size_t estimateEntrySize(const CacheEntry& entry) const;

    /**
     * @brief 清理过期条目（无锁版本，调用者必须持有 m_cacheMutex）
     */
    size_t evictExpiredLocked();

    /**
     * @brief 使用LRU策略淘汰最久未使用的条目（无锁版本，调用者必须持有 m_cacheMutex）
     */
    size_t evictLRULocked(size_t count);
};

} // namespace naw::desktop_pet::service

