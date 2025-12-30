#include "naw/desktop_pet/service/ToolCallContext.h"

#include <sstream>

namespace naw::desktop_pet::service {

// ========== ToolCallHistory ==========

nlohmann::json ToolCallHistory::toJson() const {
    nlohmann::json j;
    j["tool_call_id"] = toolCallId;
    j["tool_name"] = toolName;
    j["arguments"] = arguments;
    if (result.has_value()) {
        j["result"] = result.value();
    }
    if (error.has_value()) {
        j["error"] = error.value();
    }
    j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()
    ).count();
    j["execution_time_ms"] = executionTimeMs;
    j["success"] = success;
    return j;
}

// ========== CallChain ==========

nlohmann::json CallChain::toJson() const {
    nlohmann::json j;
    j["conversation_id"] = conversationId;
    j["tool_calls"] = nlohmann::json::array();
    for (const auto& toolCall : toolCalls) {
        j["tool_calls"].push_back(toolCall.toJson());
    }
    j["start_time"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        startTime.time_since_epoch()
    ).count();
    j["end_time"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime.time_since_epoch()
    ).count();
    return j;
}

// ========== ToolCallContext ==========

ToolCallContext::ToolCallContext(bool enableCache, int cacheTTLMs)
    : m_cacheEnabled(enableCache)
    , m_cacheTTLMs(cacheTTLMs)
{
}

void ToolCallContext::recordToolCall(const FunctionCallResult& result, const nlohmann::json& arguments) {
    std::lock_guard<std::mutex> lock(m_mutex);

    ToolCallHistory history;
    history.toolCallId = result.toolCallId;
    history.toolName = result.toolName;
    history.arguments = arguments;
    history.result = result.result;
    history.error = result.error;
    history.timestamp = std::chrono::system_clock::now();
    history.executionTimeMs = result.executionTimeMs;
    history.success = result.success;

    m_history.push_back(history);

    // 将工具调用添加到所有活动的调用链中
    for (auto& [convId, chain] : m_callChains) {
        // 如果调用链还未结束（endTime == startTime 表示未结束），添加到调用链
        if (chain.endTime == chain.startTime) {
            chain.toolCalls.push_back(history);
            chain.endTime = std::chrono::system_clock::now();
        }
    }

    // 如果缓存启用且执行成功，缓存结果
    if (m_cacheEnabled && result.success && result.result.has_value()) {
        cacheResultUnlocked(result.toolName, arguments, result.result.value());
    }
}

std::vector<ToolCallHistory> ToolCallContext::getHistory() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_history;
}

std::vector<ToolCallHistory> ToolCallContext::getHistoryByTool(const std::string& toolName) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ToolCallHistory> filtered;
    for (const auto& history : m_history) {
        if (history.toolName == toolName) {
            filtered.push_back(history);
        }
    }
    return filtered;
}

void ToolCallContext::clearHistory() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_history.clear();
}

void ToolCallContext::startCallChain(const std::string& conversationId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    CallChain chain;
    chain.conversationId = conversationId;
    chain.startTime = std::chrono::system_clock::now();
    chain.endTime = chain.startTime;
    m_callChains[conversationId] = chain;
}

void ToolCallContext::endCallChain(const std::string& conversationId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_callChains.find(conversationId);
    if (it != m_callChains.end()) {
        it->second.endTime = std::chrono::system_clock::now();
    }
}

std::optional<CallChain> ToolCallContext::getCallChain(const std::string& conversationId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_callChains.find(conversationId);
    if (it != m_callChains.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<CallChain> ToolCallContext::getAllCallChains() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<CallChain> chains;
    chains.reserve(m_callChains.size());
    for (const auto& [convId, chain] : m_callChains) {
        chains.push_back(chain);
    }
    return chains;
}

void ToolCallContext::clearCallChains() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callChains.clear();
}

std::optional<nlohmann::json> ToolCallContext::getCachedResult(
    const std::string& toolName,
    const nlohmann::json& arguments
) const {
    if (!m_cacheEnabled) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // 清理过期缓存（在const方法中，因为m_cache是mutable的）
    cleanupExpiredCacheUnlocked();

    std::string cacheKey = generateCacheKey(toolName, arguments);
    auto it = m_cache.find(cacheKey);
    if (it != m_cache.end()) {
        // 检查是否过期
        if (!isCacheEntryExpired(it->second)) {
            return std::make_optional(it->second.result);
        } else {
            // 移除过期项
            m_cache.erase(it);
        }
    }

    return std::nullopt;
}

void ToolCallContext::cacheResult(
    const std::string& toolName,
    const nlohmann::json& arguments,
    const nlohmann::json& result
) {
    if (!m_cacheEnabled) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    cacheResultUnlocked(toolName, arguments, result);
}

void ToolCallContext::cacheResultUnlocked(
    const std::string& toolName,
    const nlohmann::json& arguments,
    const nlohmann::json& result
) {
    std::string cacheKey = generateCacheKey(toolName, arguments);
    CacheEntry entry;
    entry.result = result;
    entry.timestamp = std::chrono::system_clock::now();
    m_cache[cacheKey] = entry;
}

void ToolCallContext::clearCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();
}

void ToolCallContext::setCacheEnabled(bool enable) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cacheEnabled = enable;
    if (!enable) {
        m_cache.clear();
    }
}

bool ToolCallContext::isCacheEnabled() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cacheEnabled;
}

std::string ToolCallContext::generateCacheKey(
    const std::string& toolName,
    const nlohmann::json& arguments
) {
    // 使用工具名和参数的JSON字符串作为缓存键
    std::ostringstream oss;
    oss << toolName << ":" << arguments.dump();
    return oss.str();
}

bool ToolCallContext::isCacheEntryExpired(const CacheEntry& entry) const {
    if (m_cacheTTLMs <= 0) {
        return false; // 永不过期
    }

    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - entry.timestamp);
    return elapsed.count() >= m_cacheTTLMs;
}

void ToolCallContext::cleanupExpiredCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    cleanupExpiredCacheUnlocked();
}

void ToolCallContext::cleanupExpiredCacheUnlocked() const {
    if (m_cacheTTLMs <= 0) {
        return; // 永不过期，无需清理
    }

    auto it = m_cache.begin();
    while (it != m_cache.end()) {
        if (isCacheEntryExpired(it->second)) {
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace naw::desktop_pet::service

