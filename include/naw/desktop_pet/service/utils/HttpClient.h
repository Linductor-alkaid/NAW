#pragma once

#include "HttpTypes.h"
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>
#include <chrono>

// 前向声明，避免包含整个httplib.h头文件
// 实际使用时会在cpp文件中包含
namespace httplib {
    class Client;
}

namespace naw::desktop_pet::service::utils {

/**
 * @brief HTTP客户端封装类
 * 
 * 提供同步HTTP请求功能，支持基础重试；提供基于内部线程池的异步接口。
 * 基于cpp-httplib实现。连接池统计暂未提供。
 *
 * 使用示例（手工自检）：
 * ```
 * HttpClient client("https://httpbin.org");
 * auto resp = client.get("/get", {{"hello", "world"}});
 * if (resp.isSuccess()) {
 *     // 预期statusCode=200，body包含query参数
 * }
 * // 异步示例
 * auto fut = client.getAsync("/get", {{"q", "async"}});
 * auto asyncResp = fut.get();
 * // 同样可检查 asyncResp.isSuccess()
 *
 * // 异步POST示例
 * auto futPost = client.postAsync("/post", R"({"ping":true})");
 * auto postResp = futPost.get();
 * // postResp.statusCode 预期 200（依赖目标服务）
 *
 * // 简易自检步骤：
 * // 1) 编译后运行可执行，调用以上代码指向 https://httpbin.org
 * // 2) 预期 GET/POST 返回 200，body 含传入参数；网络异常时 error 字符串非空。
 * // 3) 可调用 getRetryStats() 观察重试计数，或在网络不通时触发重试。
 * ```
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
    
    // 禁止拷贝与移动（含mutex，不可安全移动）
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    HttpClient(HttpClient&&) = delete;
    HttpClient& operator=(HttpClient&&) = delete;
    
    /**
     * @brief 设置默认请求头
     */
    void setDefaultHeader(const std::string& key, const std::string& value);
    
    /**
     * @brief 设置连接池配置
     */
    void setConnectionPoolConfig(const ConnectionPoolConfig& config);
    ConnectionPoolConfig getConnectionPoolConfig() const { return m_poolConfig; }
    
    /**
     * @brief 设置重试配置
     */
    void setRetryConfig(const RetryConfig& config);
    RetryConfig getRetryConfig() const { return m_retryConfig; }
    
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
     * @brief PATCH请求
     */
    HttpResponse patch(const std::string& path,
                      const std::string& body = "",
                      const std::string& contentType = "application/json",
                      const std::map<std::string, std::string>& headers = {});

    /**
     * @brief 表单POST（application/x-www-form-urlencoded）
     */
    HttpResponse postForm(const std::string& path,
                         const std::map<std::string, std::string>& formFields,
                         const std::map<std::string, std::string>& headers = {});

    struct MultipartFile {
        std::string filename;
        std::string contentType;
        std::string data; // 简化：内存数据
    };

    /**
     * @brief multipart/form-data POST
     */
    HttpResponse postMultipart(const std::string& path,
                               const std::map<std::string, std::string>& fields,
                               const std::map<std::string, MultipartFile>& files,
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

    struct CancelToken {
        std::shared_ptr<std::atomic<bool>> cancelled;
    };

    /**
     * @brief 异步GET请求（基于内部线程池）
     */
    std::future<HttpResponse> getAsync(const std::string& path,
                                      const std::map<std::string, std::string>& params = {},
                                      const std::map<std::string, std::string>& headers = {},
                                      std::function<void(const HttpResponse&)> callback = nullptr,
                                      CancelToken* token = nullptr);

    /**
     * @brief 异步POST请求
     */
    std::future<HttpResponse> postAsync(const std::string& path,
                                       const std::string& body = "",
                                       const std::string& contentType = "application/json",
                                       const std::map<std::string, std::string>& headers = {},
                                       std::function<void(const HttpResponse&)> callback = nullptr,
                                       CancelToken* token = nullptr);

    /**
     * @brief 异步PATCH请求
     */
    std::future<HttpResponse> patchAsync(const std::string& path,
                                         const std::string& body = "",
                                         const std::string& contentType = "application/json",
                                         const std::map<std::string, std::string>& headers = {},
                                         std::function<void(const HttpResponse&)> callback = nullptr,
                                         CancelToken* token = nullptr);

    /**
     * @brief 异步通用请求
     */
    std::future<HttpResponse> executeAsync(const HttpRequest& request,
                                           std::function<void(const HttpResponse&)> callback = nullptr,
                                           CancelToken* token = nullptr);

    // ========== 统计接口（轻量级） ==========
    size_t getActiveConnections() const;
    size_t getTotalConnections() const;
    size_t getReusedConnections() const;
    double getConnectionReuseRate() const;
    RetryStatsSnapshot getRetryStats() const;

    // 测试辅助访问（gtest友元）
    friend class HttpClientTestAccessor;

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
    
    // 连接池管理（仅host级缓存，不做统计）
    mutable std::mutex m_clientMutex;
    struct ClientEntry {
        std::shared_ptr<httplib::Client> client;
        std::chrono::steady_clock::time_point lastUsed;
        size_t useCount{0};
    };
    std::map<std::string, ClientEntry> m_clientPool;
    size_t m_totalConnections{0};
    size_t m_reusedConnections{0};

    void pruneIdleClients();
    void enforcePoolLimits();

    // 线程池（用于异步接口）
    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    bool m_stopWorkers{false};
    size_t m_threadCount{0};
    RetryStats m_retryStats;
    bool m_enableHealthCheck{false};

    std::future<HttpResponse> submitAsyncTask(std::function<HttpResponse()> task);
    void startWorkers(size_t threadCount);
    void stopWorkers();
    static HttpErrorType classifyStatus(int statusCode);
};

} // namespace naw::desktop_pet::service::utils
