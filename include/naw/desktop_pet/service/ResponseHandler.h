#pragma once

#include "naw/desktop_pet/service/APIClient.h"
#include "naw/desktop_pet/service/CacheManager.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/ErrorTypes.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"

#include <atomic>
#include <functional>
#include <istream>
#include <optional>
#include <string>

namespace naw::desktop_pet::service {

/**
 * @brief 响应处理器：统一处理 API 响应
 *
 * 功能：
 * - 流式响应处理：解析 SSE 数据流、提取增量内容、管理流式回调
 * - 响应验证：验证 JSON 格式、检查必需字段、验证响应内容
 * - 缓存集成：与 CacheManager 集成，实现缓存查询和存储
 * - 响应统计：收集响应处理统计数据
 */
class ResponseHandler {
public:
    /**
     * @brief 流式回调接口
     */
    struct StreamCallbacks {
        std::function<void(std::string_view)> onTextDelta;
        std::function<void(const APIClient::ToolCallDelta&)> onToolCallDelta;
        std::function<void(const types::ChatResponse&)> onComplete;
        std::function<void(const ErrorInfo&)> onError;
    };

    /**
     * @brief 响应统计结构
     */
    struct ResponseStatistics {
        uint64_t totalResponses{0};           // 总响应数
        uint64_t successfulResponses{0};      // 成功响应数
        uint64_t failedResponses{0};          // 失败响应数
        uint64_t cachedResponses{0};          // 缓存命中数
        uint64_t totalResponseSize{0};        // 总响应大小（字节）
        uint64_t streamingResponses{0};       // 流式响应数

        /**
         * @brief 计算平均响应大小
         */
        uint64_t getAverageResponseSize() const {
            if (totalResponses == 0) return 0;
            return totalResponseSize / totalResponses;
        }

        /**
         * @brief 计算缓存命中率
         */
        double getCacheHitRate() const {
            uint64_t total = totalResponses;
            if (total == 0) return 0.0;
            return static_cast<double>(cachedResponses) / static_cast<double>(total);
        }
    };

    explicit ResponseHandler(ConfigManager& configManager, CacheManager& cacheManager);
    ~ResponseHandler() = default;

    // 禁止拷贝/移动
    ResponseHandler(const ResponseHandler&) = delete;
    ResponseHandler& operator=(const ResponseHandler&) = delete;
    ResponseHandler(ResponseHandler&&) = delete;
    ResponseHandler& operator=(ResponseHandler&&) = delete;

    // ========== 流式响应处理 ==========
    /**
     * @brief 处理流式响应
     * @param stream 输入流
     * @param callbacks 回调函数
     */
    void handleStreamResponse(std::istream& stream, StreamCallbacks callbacks);

    // ========== 响应验证 ==========
    /**
     * @brief 验证 JSON 响应
     * @param json JSON 对象
     * @param error 错误信息（可选）
     * @return 是否有效
     */
    bool validateResponse(const nlohmann::json& json, ErrorInfo* error = nullptr);

    /**
     * @brief 验证 ChatResponse 响应
     * @param response 响应对象
     * @param error 错误信息（可选）
     * @return 是否有效
     */
    bool validateResponse(const types::ChatResponse& response, ErrorInfo* error = nullptr);

    // ========== 缓存集成 ==========
    /**
     * @brief 查询缓存
     * @param request 请求对象
     * @return 缓存的响应，如果未命中则返回 nullopt
     */
    std::optional<types::ChatResponse> checkCache(const types::ChatRequest& request);

    /**
     * @brief 存储响应到缓存
     * @param request 请求对象
     * @param response 响应对象
     */
    void storeCache(const types::ChatRequest& request, const types::ChatResponse& response);

    // ========== 统计查询 ==========
    /**
     * @brief 获取响应统计信息
     */
    ResponseStatistics getStatistics() const;

    /**
     * @brief 获取缓存命中率
     * @return 命中率（0-1）
     */
    double getCacheHitRate() const;

private:
    ConfigManager& m_configManager;
    CacheManager& m_cacheManager;

    // 配置参数
    bool m_cacheEnabled{true};
    bool m_cacheToolCalls{false};
    float m_cacheTemperatureThreshold{0.01f};

    // 统计数据（使用原子操作保证线程安全）
    mutable std::mutex m_statisticsMutex;
    ResponseStatistics m_statistics;

    // ========== 内部方法 ==========
    /**
     * @brief 从配置读取参数
     */
    void loadConfiguration();

    /**
     * @brief 判断是否应该缓存
     */
    bool shouldCache(const types::ChatRequest& request) const;

    /**
     * @brief 验证 JSON 格式
     */
    bool validateJsonFormat(const std::string& jsonStr, ErrorInfo* error = nullptr) const;

    /**
     * @brief 验证响应结构
     */
    bool validateResponseStructure(const nlohmann::json& json, ErrorInfo* error = nullptr) const;

    /**
     * @brief 检查必需字段
     */
    bool checkRequiredFields(const nlohmann::json& json, ErrorInfo* error = nullptr) const;

    /**
     * @brief 验证响应内容
     */
    bool validateResponseContent(const types::ChatResponse& response, ErrorInfo* error = nullptr) const;

    /**
     * @brief 更新统计信息
     */
    void updateStatistics(const types::ChatResponse& response, bool isSuccess, bool isCached, bool isStreaming);

    /**
     * @brief 估算响应大小（字节）
     */
    size_t estimateResponseSize(const types::ChatResponse& response) const;
};

} // namespace naw::desktop_pet::service

