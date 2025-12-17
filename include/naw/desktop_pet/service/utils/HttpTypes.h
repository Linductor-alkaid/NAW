#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <functional>
#include <atomic>
#include <vector>
#include <sstream>

#ifdef DELETE
#undef DELETE
#endif

namespace naw::desktop_pet::service::utils {

/**
 * @brief HTTP请求方法枚举
 */
enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE,
    PATCH,
    HEAD,
    OPTIONS
};

/**
 * @brief HTTP请求结构
 */
struct HttpRequest {
    HttpMethod method = HttpMethod::GET;
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
    int timeoutMs = 30000;  // 默认30秒超时
    bool followRedirects = true;
    
    // URL参数（用于GET请求）
    std::map<std::string, std::string> params;
    
    /**
     * @brief 设置请求头
     */
    void setHeader(const std::string& key, const std::string& value) {
        headers[key] = value;
    }
    
    /**
     * @brief 获取请求头
     */
    std::optional<std::string> getHeader(const std::string& key) const {
        auto it = headers.find(key);
        if (it != headers.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    /**
     * @brief 设置URL参数
     */
    void setParam(const std::string& key, const std::string& value) {
        params[key] = value;
    }
    
    /**
     * @brief 构建完整的URL（包含参数）
     */
    std::string buildUrl() const;
};

/**
 * @brief HTTP错误/状态分类
 */
enum class HttpErrorType {
    None,
    Network,       // statusCode == 0 or transport error
    Timeout,       // 408 or超时
    RateLimit,     // 429
    Client,        // 4xx
    Server,        // 5xx
    Unknown
};

struct HttpHeaders {
    // 小写键 -> 多值列表（按插入顺序保留）
    std::map<std::string, std::vector<std::string>> entries;

    static std::string toLower(const std::string& s) {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c){ return std::tolower(c); });
        return out;
    }

    static HttpHeaders parseRaw(const std::string& raw) {
        HttpHeaders h;
        std::istringstream iss(raw);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            auto pos = line.find(':');
            if (pos == std::string::npos) continue;
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            auto trim = [](std::string& s) {
                auto notSpace = [](int ch) { return !std::isspace(ch); };
                s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
                s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
            };
            trim(key); trim(val);
            if (key.empty()) continue;
            auto lk = toLower(key);
            h.entries[lk].push_back(val);
        }
        return h;
    }

    void add(const std::string& key, const std::string& value) {
        entries[toLower(key)].push_back(value);
    }

    bool has(const std::string& key) const {
        return entries.find(toLower(key)) != entries.end();
    }

    std::vector<std::string> getAll(const std::string& key) const {
        auto it = entries.find(toLower(key));
        if (it == entries.end()) return {};
        return it->second;
    }

    std::optional<std::string> getFirst(const std::string& key) const {
        auto vals = getAll(key);
        if (vals.empty()) return std::nullopt;
        return vals.front();
    }

    std::optional<std::string> contentType() const {
        return getFirst("content-type");
    }

    std::optional<long long> contentLength() const {
        auto v = getFirst("content-length");
        if (!v) return std::nullopt;
        try {
            return std::stoll(*v);
        } catch (...) {
            return std::nullopt;
        }
    }

    std::map<std::string, std::string> toFirstValueMap() const {
        std::map<std::string, std::string> m;
        for (const auto& [k, vals] : entries) {
            if (!vals.empty()) m[k] = vals.front();
        }
        return m;
    }
};

/**
 * @brief HTTP响应结构
 */
struct HttpResponse {
    int statusCode = 0;
    std::map<std::string, std::string> headers; // 首值映射，兼容旧接口
    HttpHeaders multiHeaders; // 多值头
    std::string body;
    std::string error;  // 错误信息（如果有）
    
    /**
     * @brief 检查响应是否成功
     */
    bool isSuccess() const {
        return statusCode >= 200 && statusCode < 300;
    }
    
    /**
     * @brief 检查是否为客户端错误
     */
    bool isClientError() const {
        return statusCode >= 400 && statusCode < 500;
    }
    
    /**
     * @brief 检查是否为服务器错误
     */
    bool isServerError() const {
        return statusCode >= 500 && statusCode < 600;
    }
    
    /**
     * @brief 获取响应头（首值，不区分大小写）
     */
    std::optional<std::string> getHeader(const std::string& key) const {
        auto lk = HttpHeaders::toLower(key);
        auto it = headers.find(lk);
        if (it != headers.end()) {
            return it->second;
        }
        // 回退多值表的首值
        return multiHeaders.getFirst(key);
    }
    
    /**
     * @brief 获取Content-Type
     */
    std::optional<std::string> getContentType() const {
        return multiHeaders.contentType();
    }
    
    /**
     * @brief 检查是否为JSON响应
     */
    bool isJson() const {
        auto contentType = getContentType();
        if (!contentType.has_value()) {
            return false;
        }
        const std::string& ct = contentType.value();
        return ct.find("application/json") != std::string::npos;
    }
};

/**
 * @brief 连接池配置
 */
struct ConnectionPoolConfig {
    size_t maxConnections = 10;           // 最大连接数
    size_t maxConnectionsPerHost = 5;     // 每个主机的最大连接数
    std::chrono::milliseconds idleTimeout{30000};  // 空闲超时（30秒）
    std::chrono::milliseconds connectionTimeout{10000};  // 连接超时（10秒）
};

/**
 * @brief 重试配置
 */
struct RetryConfig {
    int maxRetries = 3;                              // 最大重试次数
    std::chrono::milliseconds initialDelay{1000};     // 初始延迟（1秒）
    double backoffMultiplier = 2.0;                  // 退避倍数
    std::chrono::milliseconds maxDelay{30000};        // 最大延迟（30秒）
    bool enableJitter = true;                         // 是否启用随机抖动
    bool retryOnRateLimit = true;                     // 是否对429重试
    bool retryOnServerError = true;                   // 是否对5xx重试
    // 可选自定义退避回调：入参 attempt，返回延迟；如返回 <0 则使用默认
    std::function<std::chrono::milliseconds(int)> customBackoff;
    // 可选重试日志回调：入参 attempt、HttpResponse
    std::function<void(int, const HttpResponse&)> retryLogger;
    
    /**
     * @brief 计算重试延迟
     * @param attempt 当前重试次数（从0开始）
     * @return 延迟时间（毫秒）
     */
    std::chrono::milliseconds getRetryDelay(int attempt) const {
        const double scaled = static_cast<double>(initialDelay.count()) *
                              std::pow(backoffMultiplier, attempt);
        const auto clamped = std::min(scaled,
                                      static_cast<double>(maxDelay.count()));

        double withJitter = clamped;
        if (enableJitter) {
            const double jitterRange = clamped * 0.2;  // ±20%
            const double randomFactor = (std::rand() % 200 - 100) / 100.0; // -1..1
            withJitter += jitterRange * randomFactor;
        }

        const auto millis =
            static_cast<std::chrono::milliseconds::rep>(std::max(0.0, withJitter));
        return std::chrono::milliseconds{millis};
    }
};

/**
 * @brief 重试统计
 */
struct RetryStatsSnapshot {
    int totalAttempts{0};
    int totalRetries{0};
    int totalSuccessAfterRetry{0};
};

struct RetryStats {
    std::atomic<int> totalAttempts{0};
    std::atomic<int> totalRetries{0};
    std::atomic<int> totalSuccessAfterRetry{0};

    RetryStats() = default;
    RetryStats(const RetryStats&) = delete;
    RetryStats& operator=(const RetryStats&) = delete;

    RetryStatsSnapshot snapshot() const {
        RetryStatsSnapshot snap;
        snap.totalAttempts = totalAttempts.load(std::memory_order_relaxed);
        snap.totalRetries = totalRetries.load(std::memory_order_relaxed);
        snap.totalSuccessAfterRetry = totalSuccessAfterRetry.load(std::memory_order_relaxed);
        return snap;
    }
};

} // namespace naw::desktop_pet::service::utils
