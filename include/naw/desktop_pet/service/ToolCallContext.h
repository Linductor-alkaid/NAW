#pragma once

#include "naw/desktop_pet/service/FunctionCallingHandler.h"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"

namespace naw::desktop_pet::service {

/**
 * @brief 工具调用历史记录
 */
struct ToolCallHistory {
    std::string toolCallId;                                    // 工具调用ID
    std::string toolName;                                      // 工具名称
    nlohmann::json arguments;                                  // 工具参数
    std::optional<nlohmann::json> result;                      // 执行结果（成功时）
    std::optional<std::string> error;                          // 错误信息（失败时）
    std::chrono::system_clock::time_point timestamp;          // 调用时间戳
    double executionTimeMs{0.0};                               // 执行时间（毫秒）
    bool success{false};                                       // 是否成功

    /**
     * @brief 转换为JSON格式
     */
    nlohmann::json toJson() const;
};

/**
 * @brief 调用链记录（关联对话ID和工具调用列表）
 */
struct CallChain {
    std::string conversationId;                                // 对话ID
    std::vector<ToolCallHistory> toolCalls;                   // 工具调用列表
    std::chrono::system_clock::time_point startTime;          // 开始时间
    std::chrono::system_clock::time_point endTime;            // 结束时间（最后一个工具调用完成时间）

    /**
     * @brief 转换为JSON格式
     */
    nlohmann::json toJson() const;
};

/**
 * @brief 工具调用上下文管理器
 *
 * 提供工具调用历史记录、调用链追踪和结果缓存功能。
 */
class ToolCallContext {
public:
    /**
     * @brief 构造函数
     * @param enableCache 是否启用结果缓存（默认true）
     * @param cacheTTLMs 缓存TTL（毫秒），0表示永不过期（默认300000，即5分钟）
     */
    explicit ToolCallContext(bool enableCache = true, int cacheTTLMs = 300000);
    ~ToolCallContext() = default;

    // ========== 工具调用历史记录 ==========

    /**
     * @brief 记录工具调用
     * @param result 工具调用结果
     * @param arguments 工具调用参数
     */
    void recordToolCall(const FunctionCallResult& result, const nlohmann::json& arguments);

    /**
     * @brief 获取所有工具调用历史
     * @return 历史记录列表
     */
    std::vector<ToolCallHistory> getHistory() const;

    /**
     * @brief 按工具名称获取历史记录
     * @param toolName 工具名称
     * @return 匹配的历史记录列表
     */
    std::vector<ToolCallHistory> getHistoryByTool(const std::string& toolName) const;

    /**
     * @brief 清除历史记录
     */
    void clearHistory();

    // ========== 调用链追踪 ==========

    /**
     * @brief 开始新的调用链
     * @param conversationId 对话ID
     */
    void startCallChain(const std::string& conversationId);

    /**
     * @brief 结束调用链
     * @param conversationId 对话ID
     */
    void endCallChain(const std::string& conversationId);

    /**
     * @brief 获取调用链
     * @param conversationId 对话ID
     * @return 调用链（如果存在）
     */
    std::optional<CallChain> getCallChain(const std::string& conversationId) const;

    /**
     * @brief 获取所有调用链
     * @return 调用链列表
     */
    std::vector<CallChain> getAllCallChains() const;

    /**
     * @brief 清除调用链
     */
    void clearCallChains();

    // ========== 结果缓存 ==========

    /**
     * @brief 获取缓存的工具调用结果
     * @param toolName 工具名称
     * @param arguments 工具参数（用于生成缓存键）
     * @return 如果缓存命中返回结果，否则返回 std::nullopt
     */
    std::optional<nlohmann::json> getCachedResult(
        const std::string& toolName,
        const nlohmann::json& arguments
    ) const;

    /**
     * @brief 缓存工具调用结果
     * @param toolName 工具名称
     * @param arguments 工具参数
     * @param result 执行结果
     */
    void cacheResult(
        const std::string& toolName,
        const nlohmann::json& arguments,
        const nlohmann::json& result
    );

    /**
     * @brief 清除缓存
     */
    void clearCache();

    /**
     * @brief 启用/禁用缓存
     * @param enable 是否启用
     */
    void setCacheEnabled(bool enable);

    /**
     * @brief 检查缓存是否启用
     * @return 如果启用返回 true
     */
    bool isCacheEnabled() const;

private:
    mutable std::mutex m_mutex;                                // 保护所有数据的互斥锁
    std::vector<ToolCallHistory> m_history;                    // 工具调用历史记录
    std::unordered_map<std::string, CallChain> m_callChains;  // 调用链映射（对话ID -> 调用链）

    // 结果缓存
    struct CacheEntry {
        nlohmann::json result;                                  // 缓存的结果
        std::chrono::system_clock::time_point timestamp;       // 缓存时间戳
    };
    mutable std::unordered_map<std::string, CacheEntry> m_cache; // 缓存（键：工具名+参数哈希，mutable以支持const方法中的清理）
    bool m_cacheEnabled;                                       // 是否启用缓存
    int m_cacheTTLMs;                                          // 缓存TTL（毫秒）

    /**
     * @brief 生成缓存键
     * @param toolName 工具名称
     * @param arguments 工具参数
     * @return 缓存键（字符串）
     */
    static std::string generateCacheKey(
        const std::string& toolName,
        const nlohmann::json& arguments
    );

    /**
     * @brief 检查缓存项是否过期
     * @param entry 缓存项
     * @return 如果过期返回 true
     */
    bool isCacheEntryExpired(const CacheEntry& entry) const;

    /**
     * @brief 清理过期的缓存项
     */
    void cleanupExpiredCache();

    /**
     * @brief 缓存结果（不加锁版本，内部使用）
     */
    void cacheResultUnlocked(
        const std::string& toolName,
        const nlohmann::json& arguments,
        const nlohmann::json& result
    );

    /**
     * @brief 清理过期缓存（不加锁版本，内部使用）
     * @note 虽然是const方法，但会修改缓存，需要在已持有锁的情况下调用
     */
    void cleanupExpiredCacheUnlocked() const;
};

} // namespace naw::desktop_pet::service

