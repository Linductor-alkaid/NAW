#pragma once

#include "HttpTypes.h"
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// 前向声明，避免包含整个httplib.h头文件
// 实际使用时会在cpp文件中包含
namespace httplib {
    class Client;
}

namespace naw::desktop_pet::service::utils {

/**
 * @brief HTTP客户端封装类
 * 
 * 提供同步和异步HTTP请求功能，支持连接池、重试机制等。
 * 基于cpp-httplib实现。
 */
class HttpClient {
public:
    /**
     * @brief 构造函数
     * @param baseUrl 基础URL（可选，如果提供则所有请求都会基于此URL）
     */
    explicit HttpClient(const std::string& baseUrl = "");
    
    /**
     * @brief 析构函数
     */
    ~HttpClient();
    
    // 禁止拷贝
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    
    // 允许移动
    HttpClient(HttpClient&&) noexcept;
    HttpClient& operator=(HttpClient&&) noexcept;
    
    /**
     * @brief 设置默认请求头
     */
    void setDefaultHeader(const std::string& key, const std::string& value);
    
    /**
     * @brief 设置连接池配置
     */
    void setConnectionPoolConfig(const ConnectionPoolConfig& config);
    
    /**
     * @brief 设置重试配置
     */
    void setRetryConfig(const RetryConfig& config);
    
    /**
     * @brief 设置超时时间（毫秒）
     */
    void setTimeout(int timeoutMs);
    
    /**
     * @brief 设置是否跟随重定向
     */
    void setFollowRedirects(bool follow);
    
    /**
     * @brief 设置SSL验证（仅HTTPS）
     */
    void setSSLVerification(bool verify);
    
    /**
     * @brief 设置CA证书路径（仅HTTPS）
     */
    void setCACertPath(const std::string& path);
    
    // ========== 同步请求方法 ==========
    
    /**
     * @brief GET请求
     */
    HttpResponse get(const std::string& path, 
                    const std::map<std::string, std::string>& params = {},
                    const std::map<std::string, std::string>& headers = {});
    
    /**
     * @brief POST请求
     */
    HttpResponse post(const std::string& path,
                     const std::string& body = "",
                     const std::string& contentType = "application/json",
                     const std::map<std::string, std::string>& headers = {});
    
    /**
     * @brief POST请求（JSON）
     */
    HttpResponse postJson(const std::string& path,
                         const std::string& jsonBody,
                         const std::map<std::string, std::string>& headers = {});
    
    /**
     * @brief PUT请求
     */
    HttpResponse put(const std::string& path,
                    const std::string& body = "",
                    const std::string& contentType = "application/json",
                    const std::map<std::string, std::string>& headers = {});
    
    /**
     * @brief DELETE请求
     */
    HttpResponse deleteRequest(const std::string& path,
                              const std::map<std::string, std::string>& headers = {});
    
    /**
     * @brief 通用请求方法
     */
    HttpResponse execute(const HttpRequest& request);
    
    // ========== 异步请求方法 ==========
    
    /**
     * @brief 异步GET请求
     */
    std::future<HttpResponse> getAsync(const std::string& path,
                                      const std::map<std::string, std::string>& params = {},
                                      const std::map<std::string, std::string>& headers = {});
    
    /**
     * @brief 异步POST请求
     */
    std::future<HttpResponse> postAsync(const std::string& path,
                                       const std::string& body = "",
                                       const std::string& contentType = "application/json",
                                       const std::map<std::string, std::string>& headers = {});
    
    /**
     * @brief 异步POST请求（JSON）
     */
    std::future<HttpResponse> postJsonAsync(const std::string& path,
                                           const std::string& jsonBody,
                                           const std::map<std::string, std::string>& headers = {});
    
    /**
     * @brief 异步通用请求方法
     */
    std::future<HttpResponse> executeAsync(const HttpRequest& request);
    
    // ========== 连接池统计 ==========
    
    /**
     * @brief 获取活跃连接数
     */
    size_t getActiveConnections() const;
    
    /**
     * @brief 获取总连接数
     */
    size_t getTotalConnections() const;
    
    /**
     * @brief 获取连接复用率
     */
    double getConnectionReuseRate() const;

private:
    /**
     * @brief 创建或获取httplib客户端
     */
    std::shared_ptr<httplib::Client> getOrCreateClient(const std::string& url);
    
    /**
     * @brief 执行请求（带重试）
     */
    HttpResponse executeWithRetry(const HttpRequest& request);
    
    /**
     * @brief 执行单次请求（不重试）
     */
    HttpResponse executeOnce(const HttpRequest& request);
    
    /**
     * @brief 转换HttpMethod到字符串
     */
    static std::string methodToString(HttpMethod method);
    
    /**
     * @brief 构建完整URL
     */
    std::string buildFullUrl(const std::string& path) const;
    
    /**
     * @brief 判断错误是否可重试
     */
    bool isRetryableError(const HttpResponse& response) const;
    
    /**
     * @brief 合并请求头
     */
    std::map<std::string, std::string> mergeHeaders(
        const std::map<std::string, std::string>& requestHeaders) const;
    
    // 成员变量
    std::string m_baseUrl;
    std::map<std::string, std::string> m_defaultHeaders;
    ConnectionPoolConfig m_poolConfig;
    RetryConfig m_retryConfig;
    int m_timeoutMs;
    bool m_followRedirects;
    bool m_sslVerification;
    std::string m_caCertPath;
    
    // 连接池管理（简化实现，实际可以使用更复杂的连接池）
    mutable std::mutex m_clientMutex;
    std::map<std::string, std::shared_ptr<httplib::Client>> m_clientPool;
    
    // 统计信息
    mutable std::mutex m_statsMutex;
    size_t m_activeConnections = 0;
    size_t m_totalConnections = 0;
    size_t m_reusedConnections = 0;
};

} // namespace naw::desktop_pet::service::utils
