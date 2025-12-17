#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>

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
 * @brief HTTP响应结构
 */
struct HttpResponse {
    int statusCode = 0;
    std::map<std::string, std::string> headers;
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
     * @brief 获取响应头
     */
    std::optional<std::string> getHeader(const std::string& key) const {
        // 不区分大小写查找
        for (const auto& [k, v] : headers) {
            if (k.size() == key.size() && 
                std::equal(k.begin(), k.end(), key.begin(), 
                          [](char a, char b) { 
                              return std::tolower(a) == std::tolower(b); 
                          })) {
                return v;
            }
        }
        return std::nullopt;
    }
    
    /**
     * @brief 获取Content-Type
     */
    std::optional<std::string> getContentType() const {
        return getHeader("Content-Type");
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

} // namespace naw::desktop_pet::service::utils
