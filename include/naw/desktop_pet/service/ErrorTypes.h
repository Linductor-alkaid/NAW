#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "nlohmann/json.hpp"

namespace naw::desktop_pet::service {

/**
 * @brief 统一错误类型
 */
enum class ErrorType {
    NetworkError,     // 网络错误（连接失败、DNS等）
    RateLimitError,   // 限流错误（429）
    InvalidRequest,   // 请求错误（400/401/403等）
    ServerError,      // 服务器错误（5xx）
    TimeoutError,     // 超时（408 或本地超时）
    UnknownError      // 未知错误
};

/**
 * @brief 错误严重程度
 */
enum class ErrorSeverity {
    Critical,
    Warning,
    Info
};

/**
 * @brief 结构化错误信息
 */
struct ErrorInfo {
    ErrorType errorType{ErrorType::UnknownError};
    int errorCode{0}; // HTTP status 或内部错误码（0 表示无/未知）
    std::string message;
    std::optional<nlohmann::json> details;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
    std::optional<std::map<std::string, std::string>> context;

    static const char* errorTypeToString(ErrorType t) {
        switch (t) {
            case ErrorType::NetworkError: return "NetworkError";
            case ErrorType::RateLimitError: return "RateLimitError";
            case ErrorType::InvalidRequest: return "InvalidRequest";
            case ErrorType::ServerError: return "ServerError";
            case ErrorType::TimeoutError: return "TimeoutError";
            default: return "UnknownError";
        }
    }

    static const char* severityToString(ErrorSeverity s) {
        switch (s) {
            case ErrorSeverity::Critical: return "Critical";
            case ErrorSeverity::Warning: return "Warning";
            default: return "Info";
        }
    }

    static ErrorSeverity defaultSeverity(ErrorType t) {
        switch (t) {
            case ErrorType::InvalidRequest:
                return ErrorSeverity::Warning;
            case ErrorType::RateLimitError:
            case ErrorType::NetworkError:
            case ErrorType::TimeoutError:
            case ErrorType::ServerError:
                return ErrorSeverity::Warning;
            default:
                return ErrorSeverity::Info;
        }
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["error_type"] = errorTypeToString(errorType);
        j["error_code"] = errorCode;
        j["message"] = message;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count();
        j["timestamp_ms"] = ms;
        if (details.has_value()) j["details"] = details.value();
        if (context.has_value()) j["context"] = context.value();
        return j;
    }

    std::string toString() const {
        // JSON 作为统一字符串化输出，便于日志/调试
        return toJson().dump();
    }
};

} // namespace naw::desktop_pet::service

