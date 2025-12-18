#include "naw/desktop_pet/service/ErrorHandler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>

namespace naw::desktop_pet::service {

static uint64_t nowEpochMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

static std::string trimCopy(std::string s) {
    auto notSpace = [](int ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

ErrorHandler::RetryPolicy ErrorHandler::RetryPolicy::makeDefault() {
    RetryPolicy p;
    // 默认：这里给一个总体默认，具体错误类型的可重试在 map 中控制。
    p.maxRetries = 3;
    p.initialDelayMs = 1000;
    p.backoffMultiplier = 2.0;
    p.maxDelayMs = 30000;
    p.enableJitter = true;
    p.retryableErrors = {
        {ErrorType::NetworkError, true},
        {ErrorType::TimeoutError, true},
        {ErrorType::RateLimitError, true},
        {ErrorType::ServerError, true},
        {ErrorType::InvalidRequest, false},
        {ErrorType::UnknownError, false},
    };
    return p;
}

ErrorHandler::ErrorHandler()
    : m_policy(RetryPolicy::makeDefault())
{
    m_loggerCfg = LoggerConfig{};
}

ErrorHandler::ErrorHandler(RetryPolicy policy)
    : m_policy(std::move(policy))
{
    m_loggerCfg = LoggerConfig{};
}

void ErrorHandler::setRetryPolicy(RetryPolicy policy) {
    m_policy = std::move(policy);
}

const ErrorHandler::RetryPolicy& ErrorHandler::getRetryPolicy() const {
    return m_policy;
}

void ErrorHandler::setLoggerConfig(LoggerConfig cfg) {
    m_loggerCfg = cfg;
}

const ErrorHandler::LoggerConfig& ErrorHandler::getLoggerConfig() const {
    return m_loggerCfg;
}

const char* ErrorHandler::logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Error: return "ERROR";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Info: return "INFO";
        default: return "DEBUG";
    }
}

ErrorType ErrorHandler::mapHttpStatusToErrorType(int statusCode, const std::string& transportError) {
    if (statusCode == 0) {
        // 兼容当前 HttpClient：statusCode==0 表示传输层失败；若文案含 timeout 则归为 Timeout
        std::string low = transportError;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (low.find("timeout") != std::string::npos) return ErrorType::TimeoutError;
        return ErrorType::NetworkError;
    }
    if (statusCode == 408) return ErrorType::TimeoutError;
    if (statusCode == 429) return ErrorType::RateLimitError;
    if (statusCode >= 500 && statusCode < 600) return ErrorType::ServerError;
    if (statusCode >= 400 && statusCode < 500) return ErrorType::InvalidRequest;
    return ErrorType::UnknownError;
}

std::optional<ErrorInfo> ErrorHandler::parseApiErrorJson(const nlohmann::json& root, int httpStatusCode) {
    // OpenAI/SiliconFlow 兼容：{"error": {"message": "...", "type": "...", "code": "...", "param": ...}}
    if (!root.is_object()) return std::nullopt;
    if (!root.contains("error")) return std::nullopt;
    const auto& e = root.at("error");
    if (!e.is_object()) return std::nullopt;

    ErrorInfo info;
    info.errorCode = httpStatusCode;
    info.timestamp = std::chrono::system_clock::now();
    info.errorType = mapHttpStatusToErrorType(httpStatusCode);

    std::string msg;
    if (e.contains("message") && e.at("message").is_string()) {
        msg = e.at("message").get<std::string>();
    } else if (e.contains("error") && e.at("error").is_string()) {
        msg = e.at("error").get<std::string>();
    }
    if (msg.empty()) msg = "API error";
    info.message = msg;

    // 详情：保留原始 error 对象
    info.details = e;

    // 尝试依据 type/code 字段细分（如果 httpStatusCode==0 或未分类）
    try {
        const auto typeStr = e.contains("type") && e.at("type").is_string() ? e.at("type").get<std::string>() : "";
        const auto codeStr = e.contains("code") && e.at("code").is_string() ? e.at("code").get<std::string>() : "";

        if (!typeStr.empty()) {
            if (typeStr.find("rate") != std::string::npos || typeStr.find("Rate") != std::string::npos) {
                info.errorType = ErrorType::RateLimitError;
            }
            if (typeStr.find("timeout") != std::string::npos || typeStr.find("Timeout") != std::string::npos) {
                info.errorType = ErrorType::TimeoutError;
            }
        }
        if (!codeStr.empty()) {
            if (codeStr.find("rate") != std::string::npos || codeStr.find("Rate") != std::string::npos) {
                info.errorType = ErrorType::RateLimitError;
            }
        }
    } catch (...) {
        // ignore
    }

    return info;
}

ErrorInfo ErrorHandler::fromHttpResponse(
    const naw::desktop_pet::service::utils::HttpResponse& resp,
    const std::optional<naw::desktop_pet::service::utils::HttpRequest>& req) {

    ErrorInfo info;
    info.timestamp = std::chrono::system_clock::now();
    info.errorCode = resp.statusCode;
    info.errorType = mapHttpStatusToErrorType(resp.statusCode, resp.error);

    // message：优先 API error.message -> resp.error -> resp.body(截断) -> fallback
    std::string message;
    std::optional<nlohmann::json> parsed;
    if (resp.isJson()) {
        std::string errMsg;
        auto j = resp.asJson(&errMsg);
        if (j.has_value()) {
            parsed = j.value();
            auto apiInfo = parseApiErrorJson(j.value(), resp.statusCode);
            if (apiInfo.has_value()) {
                info = apiInfo.value();
            }
        }
    }

    if (!info.message.empty()) {
        message = info.message;
    } else if (!resp.error.empty()) {
        message = resp.error;
    } else if (!resp.body.empty()) {
        message = resp.body.substr(0, 256);
    } else {
        message = "HTTP request failed";
    }
    info.message = message;

    // details：如果已解析出 API error，则已设置；否则放入响应片段
    if (!info.details.has_value()) {
        nlohmann::json d;
        d["http_status"] = resp.statusCode;
        if (!resp.error.empty()) d["transport_error"] = resp.error;
        if (parsed.has_value()) {
            d["body_json"] = parsed.value();
        } else if (!resp.body.empty()) {
            d["body_snippet"] = resp.body.substr(0, 1024);
        }
        info.details = d;
    }

    // context：可选记录请求信息（避免泄露敏感头；这里仅记录 method/url，不记录 Authorization）
    if (req.has_value()) {
        std::map<std::string, std::string> ctx;
        ctx["url"] = req->url;
        ctx["method"] = std::to_string(static_cast<int>(req->method));
        info.context = std::move(ctx);
    }

    return info;
}

bool ErrorHandler::isRetryableByPolicy(const RetryPolicy& policy, ErrorType type) {
    auto it = policy.retryableErrors.find(type);
    if (it == policy.retryableErrors.end()) return false;
    return it->second;
}

bool ErrorHandler::shouldRetry(const ErrorInfo& err, uint32_t attemptCount) const {
    if (!isRetryableByPolicy(m_policy, err.errorType)) return false;

    // 针对不同错误类型的默认次数建议（覆盖 policy.maxRetries 的“统一上限”）
    uint32_t cap = m_policy.maxRetries;
    switch (err.errorType) {
        case ErrorType::NetworkError: cap = std::min<uint32_t>(cap, 3); break;
        case ErrorType::TimeoutError: cap = std::min<uint32_t>(cap, 2); break;
        case ErrorType::ServerError: cap = std::min<uint32_t>(cap, 2); break;
        case ErrorType::RateLimitError: cap = std::max<uint32_t>(cap, 5); break; // 429 默认允许更高
        case ErrorType::InvalidRequest: cap = 0; break;
        default: break;
    }
    return attemptCount < cap;
}

uint32_t ErrorHandler::computeBackoffDelayMs(uint32_t attemptCount) const {
    // attemptCount: 0 表示第一次重试前的延迟
    const double scaled =
        static_cast<double>(m_policy.initialDelayMs) * std::pow(m_policy.backoffMultiplier, static_cast<double>(attemptCount));
    const double clamped = std::min(scaled, static_cast<double>(m_policy.maxDelayMs));

    double withJitter = clamped;
    if (m_policy.enableJitter) {
        // ±20% jitter
        const double jitterRange = clamped * 0.2;
        const double randomFactor = (std::rand() % 200 - 100) / 100.0; // -1..1
        withJitter += jitterRange * randomFactor;
    }

    if (withJitter < 0.0) withJitter = 0.0;
    return static_cast<uint32_t>(withJitter);
}

std::optional<uint32_t> ErrorHandler::parseRetryAfterSeconds(const std::string& retryAfterValue) {
    const auto v = trimCopy(retryAfterValue);
    if (v.empty()) return std::nullopt;

    // 1) integer seconds
    bool allDigits = !v.empty() && std::all_of(v.begin(), v.end(), [](unsigned char c){ return std::isdigit(c); });
    if (allDigits) {
        try {
            const auto sec = std::stoll(v);
            if (sec < 0) return std::nullopt;
            return static_cast<uint32_t>(sec);
        } catch (...) {
            return std::nullopt;
        }
    }

    // 2) HTTP-date: IMF-fixdate, e.g. "Sun, 06 Nov 1994 08:49:37 GMT"
    // 这里只做最常见格式解析；失败则返回 nullopt。
    std::tm tm{};
    std::istringstream iss(v);
    iss.imbue(std::locale::classic());
    iss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S GMT");
    if (!iss.fail()) {
        // timegm 在 MSVC 不可用；用 _mkgmtime
        #if defined(_WIN32)
        const auto when = _mkgmtime(&tm);
        #else
        const auto when = timegm(&tm);
        #endif
        if (when <= 0) return std::nullopt;
        const auto now = std::time(nullptr);
        if (now <= 0) return std::nullopt;
        const auto delta = when - now;
        if (delta <= 0) return static_cast<uint32_t>(0);
        return static_cast<uint32_t>(delta);
    }

    return std::nullopt;
}

uint32_t ErrorHandler::getRetryDelayMs(
    const ErrorInfo& err,
    uint32_t attemptCount,
    const std::optional<naw::desktop_pet::service::utils::HttpResponse>& resp) const {

    // 429: Retry-After 优先
    if (err.errorType == ErrorType::RateLimitError && resp.has_value()) {
        auto ra = resp->getHeader("Retry-After");
        if (ra.has_value()) {
            auto sec = parseRetryAfterSeconds(*ra);
            if (sec.has_value()) {
                // 给一个最小 1s 的保护，避免 0 造成紧急重试（除非服务明确要求 0）
                const uint64_t ms = static_cast<uint64_t>(*sec) * 1000ULL;
                return static_cast<uint32_t>(std::min<uint64_t>(ms, std::numeric_limits<uint32_t>::max()));
            }
        }
        // 没有 Retry-After 时：适当增加限流等待基准
        const auto base = std::max<uint32_t>(m_policy.initialDelayMs, 2000U);
        ErrorHandler tmp(*this);
        tmp.m_policy.initialDelayMs = base;
        return tmp.computeBackoffDelayMs(attemptCount);
    }

    // ServerError：倾向固定延迟（按照 TODO），但仍允许 backoffMultiplier=1 即固定
    if (err.errorType == ErrorType::ServerError) {
        // 固定 1000ms 起步（不使用指数），并有上限
        const uint32_t fixed = std::min<uint32_t>(1000U, m_policy.maxDelayMs);
        (void)attemptCount;
        return fixed;
    }

    return computeBackoffDelayMs(attemptCount);
}

void ErrorHandler::log(LogLevel level, const std::string& message, const std::optional<ErrorInfo>& err) const {
    if (!m_loggerCfg.enabled) return;
    if (static_cast<int>(level) > static_cast<int>(LogLevel::Debug)) return;

    auto levelRank = [](LogLevel lv) {
        switch (lv) {
            case LogLevel::Error: return 0;
            case LogLevel::Warning: return 1;
            case LogLevel::Info: return 2;
            default: return 3;
        }
    };
    if (levelRank(level) > levelRank(m_loggerCfg.minLevel)) return;

    // 结构化输出到 stderr：timestamp + level + message + optional error json
    std::ostringstream oss;
    oss << "[" << nowEpochMs() << "] "
        << logLevelToString(level) << " "
        << message;
    if (err.has_value()) {
        oss << " " << err->toString();
    }
    oss << "\n";
    std::fwrite(oss.str().c_str(), 1, oss.str().size(), stderr);
    std::fflush(stderr);
}

} // namespace naw::desktop_pet::service

