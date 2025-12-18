#pragma once

#include "naw/desktop_pet/service/ErrorTypes.h"
#include "naw/desktop_pet/service/utils/HttpTypes.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>

namespace naw::desktop_pet::service {

class ErrorHandler {
public:
    struct RetryPolicy {
        uint32_t maxRetries{3};
        uint32_t initialDelayMs{1000};
        double backoffMultiplier{2.0};
        uint32_t maxDelayMs{30000};
        bool enableJitter{true};
        std::unordered_map<ErrorType, bool> retryableErrors;

        static RetryPolicy makeDefault();
    };

    enum class LogLevel {
        Error,
        Warning,
        Info,
        Debug
    };

    struct LoggerConfig {
        LogLevel minLevel{LogLevel::Warning};
        bool enabled{true};
    };

    ErrorHandler();
    explicit ErrorHandler(RetryPolicy policy);

    void setRetryPolicy(RetryPolicy policy);
    const RetryPolicy& getRetryPolicy() const;

    void setLoggerConfig(LoggerConfig cfg);
    const LoggerConfig& getLoggerConfig() const;

    // ========== 识别/解析 ==========
    static ErrorType mapHttpStatusToErrorType(int statusCode, const std::string& transportError = "");

    // 从 HttpResponse 生成 ErrorInfo（会尝试解析 body 的 API 错误 JSON）
    static ErrorInfo fromHttpResponse(
        const naw::desktop_pet::service::utils::HttpResponse& resp,
        const std::optional<naw::desktop_pet::service::utils::HttpRequest>& req = std::nullopt);

    // 解析 SiliconFlow/OpenAI 兼容 error JSON。成功返回 ErrorInfo（errorType 可能为 UnknownError）。
    // 失败返回 nullopt（例如非 JSON）。
    static std::optional<ErrorInfo> parseApiErrorJson(
        const nlohmann::json& root,
        int httpStatusCode = 0);

    // ========== 重试策略 ==========
    bool shouldRetry(const ErrorInfo& err, uint32_t attemptCount) const;

    // 返回本次 attempt 的建议延迟（毫秒）
    // - 对 429 优先读取 Retry-After
    // - 其他走指数退避 + jitter
    uint32_t getRetryDelayMs(
        const ErrorInfo& err,
        uint32_t attemptCount,
        const std::optional<naw::desktop_pet::service::utils::HttpResponse>& resp = std::nullopt) const;

    // ========== 日志 ==========
    void log(LogLevel level, const std::string& message, const std::optional<ErrorInfo>& err = std::nullopt) const;

    static const char* logLevelToString(LogLevel level);

    // Retry-After: seconds 或 HTTP-date。解析成功返回延迟秒数（向上取整），否则 nullopt。
    static std::optional<uint32_t> parseRetryAfterSeconds(const std::string& retryAfterValue);

private:
    RetryPolicy m_policy;
    LoggerConfig m_loggerCfg;

    static bool isRetryableByPolicy(const RetryPolicy& policy, ErrorType type);
    uint32_t computeBackoffDelayMs(uint32_t attemptCount) const;
};

} // namespace naw::desktop_pet::service

