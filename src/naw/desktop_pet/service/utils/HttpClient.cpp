#include "naw/desktop_pet/service/utils/HttpClient.h"
#include "naw/desktop_pet/service/utils/HttpTypes.h"

// 包含cpp-httplib头文件
// 注意：如果不需要HTTPS支持，可以移除CPPHTTPLIB_OPENSSL_SUPPORT
// #define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"

#include <chrono>
#include <future>
#include <iomanip>
#include <sstream>
#include <thread>
#include <cctype>
#include <utility>

// Windows 平台可能在系统头中定义 DELETE 宏，会与 HttpMethod::DELETE 冲突
#ifdef DELETE
#undef DELETE
#endif

namespace naw::desktop_pet::service::utils {

// ========== HttpRequest 实现 ==========

std::string HttpRequest::buildUrl() const {
    if (params.empty()) {
        return url;
    }
    
    std::ostringstream oss;
    oss << url;
    
    bool first = true;
    for (const auto& [key, value] : params) {
        oss << (first ? "?" : "&");
        first = false;
        
        // URL编码：按UTF-8字节百分号转义，仅保留RFC3986未保留字符
        auto encode = [](const std::string& str) -> std::string {
            auto isUnreserved = [](unsigned char c) {
                return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
            };

            std::ostringstream encoded;
            encoded << std::uppercase << std::hex << std::setfill('0');

            for (unsigned char c : str) {
                if (isUnreserved(c)) {
                    encoded << static_cast<char>(c);
                } else {
                    encoded << '%' << std::setw(2) << static_cast<int>(c);
                }
            }
            return encoded.str();
        };
        
        oss << encode(key) << "=" << encode(value);
    }
    
    return oss.str();
}

// ========== HttpClient 实现 ==========

HttpClient::HttpClient(const std::string& baseUrl)
    : m_baseUrl(baseUrl)
    , m_timeoutMs(30000)
    , m_followRedirects(true)
    , m_sslVerification(true)
{
    // 设置默认User-Agent
    m_defaultHeaders["User-Agent"] = "NAW-DesktopPet/1.0";

    // 初始化线程池（最小1线程，默认硬件并发/4）
    const auto hw = std::thread::hardware_concurrency();
    const size_t desired = hw > 0 ? std::max<size_t>(1, hw / 4) : 2;
    startWorkers(desired);
}

HttpClient::~HttpClient() {
    stopWorkers();
}

void HttpClient::setDefaultHeader(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    m_defaultHeaders[key] = value;
}

void HttpClient::setConnectionPoolConfig(const ConnectionPoolConfig& config) {
    m_poolConfig = config;
}

void HttpClient::setRetryConfig(const RetryConfig& config) {
    m_retryConfig = config;
}

void HttpClient::setTimeout(int timeoutMs) {
    m_timeoutMs = timeoutMs;
}

void HttpClient::setFollowRedirects(bool follow) {
    m_followRedirects = follow;
}

void HttpClient::setSSLVerification(bool verify) { m_sslVerification = verify; }
void HttpClient::setCACertPath(const std::string& path) { m_caCertPath = path; }

// ========== 同步请求方法 ==========

HttpResponse HttpClient::get(const std::string& path,
                             const std::map<std::string, std::string>& params,
                             const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.method = HttpMethod::GET;
    request.url = buildFullUrl(path);
    request.params = params;
    request.headers = mergeHeaders(headers);
    request.timeoutMs = m_timeoutMs;
    request.followRedirects = m_followRedirects;
    
    return execute(request);
}

HttpResponse HttpClient::post(const std::string& path,
                             const std::string& body,
                             const std::string& contentType,
                             const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.method = HttpMethod::POST;
    request.url = buildFullUrl(path);
    request.body = body;
    request.timeoutMs = m_timeoutMs;
    request.followRedirects = m_followRedirects;
    
    auto mergedHeaders = mergeHeaders(headers);
    mergedHeaders["Content-Type"] = contentType;
    request.headers = mergedHeaders;
    
    return execute(request);
}

HttpResponse HttpClient::postJson(const std::string& path,
                                 const std::string& jsonBody,
                                 const std::map<std::string, std::string>& headers) {
    return post(path, jsonBody, "application/json", headers);
}

HttpResponse HttpClient::put(const std::string& path,
                            const std::string& body,
                            const std::string& contentType,
                            const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.method = HttpMethod::PUT;
    request.url = buildFullUrl(path);
    request.body = body;
    request.timeoutMs = m_timeoutMs;
    request.followRedirects = m_followRedirects;
    
    auto mergedHeaders = mergeHeaders(headers);
    mergedHeaders["Content-Type"] = contentType;
    request.headers = mergedHeaders;
    
    return execute(request);
}

HttpResponse HttpClient::deleteRequest(const std::string& path,
                                      const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.method = HttpMethod::DELETE;
    request.url = buildFullUrl(path);
    request.headers = mergeHeaders(headers);
    request.timeoutMs = m_timeoutMs;
    request.followRedirects = m_followRedirects;
    
    return execute(request);
}

HttpResponse HttpClient::execute(const HttpRequest& request) {
    return executeWithRetry(request);
}

// ========== 异步请求方法 ==========

std::future<HttpResponse> HttpClient::getAsync(const std::string& path,
                                              const std::map<std::string, std::string>& params,
                                              const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.method = HttpMethod::GET;
    request.url = buildFullUrl(path);
    request.params = params;
    request.headers = mergeHeaders(headers);
    request.timeoutMs = m_timeoutMs;
    request.followRedirects = m_followRedirects;
    return executeAsync(request);
}

std::future<HttpResponse> HttpClient::postAsync(const std::string& path,
                                               const std::string& body,
                                               const std::string& contentType,
                                               const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.method = HttpMethod::POST;
    request.url = buildFullUrl(path);
    request.body = body;
    request.timeoutMs = m_timeoutMs;
    request.followRedirects = m_followRedirects;

    auto mergedHeaders = mergeHeaders(headers);
    mergedHeaders["Content-Type"] = contentType;
    request.headers = std::move(mergedHeaders);

    return executeAsync(request);
}

std::future<HttpResponse> HttpClient::executeAsync(const HttpRequest& request) {
    return submitAsyncTask([this, request]() {
        return execute(request);
    });
}

HttpResponse HttpClient::patch(const std::string& path,
                              const std::string& body,
                              const std::string& contentType,
                              const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.method = HttpMethod::PATCH;
    request.url = buildFullUrl(path);
    request.body = body;
    request.timeoutMs = m_timeoutMs;
    request.followRedirects = m_followRedirects;

    auto mergedHeaders = mergeHeaders(headers);
    mergedHeaders["Content-Type"] = contentType;
    request.headers = std::move(mergedHeaders);

    return execute(request);
}

HttpResponse HttpClient::postForm(const std::string& path,
                                 const std::map<std::string, std::string>& formFields,
                                 const std::map<std::string, std::string>& headers) {
    std::ostringstream bodyStream;
    bool first = true;
    for (const auto& [k, v] : formFields) {
        if (!first) bodyStream << "&";
        first = false;
        bodyStream << k << "=" << v; // 简化：未进行URL编码，可按需扩展
    }
    return post(path, bodyStream.str(), "application/x-www-form-urlencoded", headers);
}

// ========== 私有方法 ==========

std::shared_ptr<httplib::Client> HttpClient::getOrCreateClient(const std::string& url) {
    // 提取主机名作为连接池的key
    std::string host;
    std::string path;
    std::string scheme;
    
    // 简单解析URL（实际可以使用更完善的URL解析库）
    size_t schemeEnd = url.find("://");
    if (schemeEnd != std::string::npos) {
        scheme = url.substr(0, schemeEnd);
        size_t hostStart = schemeEnd + 3;
        size_t hostEnd = url.find('/', hostStart);
        if (hostEnd != std::string::npos) {
            host = url.substr(hostStart, hostEnd - hostStart);
            path = url.substr(hostEnd);
        } else {
            host = url.substr(hostStart);
        }
    } else {
        host = url;
    }
    
    std::lock_guard<std::mutex> lock(m_clientMutex);
    pruneIdleClients();
    
    // 查找现有客户端
    auto it = m_clientPool.find(host);
    if (it != m_clientPool.end() && it->second.client) {
        it->second.lastUsed = std::chrono::steady_clock::now();
        ++it->second.useCount;
        ++m_reusedConnections;
        return it->second.client;
    }
    
    // 创建新客户端
    std::shared_ptr<httplib::Client> client;
    // httplib 不区分 http/https 的 Client 构造；如需 HTTPS 应在编译层开启 CPPHTTPLIB_OPENSSL_SUPPORT。
    client = std::make_shared<httplib::Client>(host);
    
    // 设置超时
    client->set_connection_timeout(m_poolConfig.connectionTimeout.count() / 1000,
                                   (m_poolConfig.connectionTimeout.count() % 1000) * 1000);
    client->set_read_timeout(m_timeoutMs / 1000, (m_timeoutMs % 1000) * 1000);
    client->set_write_timeout(5, 0);
    
    // 设置跟随重定向
    client->set_follow_location(m_followRedirects);
    
    // 存储到连接池
    ClientEntry entry;
    entry.client = client;
    entry.lastUsed = std::chrono::steady_clock::now();
    entry.useCount = 1;
    enforcePoolLimits();
    m_clientPool[host] = std::move(entry);
    ++m_totalConnections;
    return client;
}

HttpResponse HttpClient::executeWithRetry(const HttpRequest& request) {
    HttpResponse response;
    int attempt = 0;
    m_retryStats.totalAttempts.fetch_add(1, std::memory_order_relaxed);
    
    while (attempt <= m_retryConfig.maxRetries) {
        response = executeOnce(request);
        
        // 如果成功或不可重试的错误，直接返回
        if (response.isSuccess() || !isRetryableError(response)) {
            if (response.isSuccess() && attempt > 0) {
                m_retryStats.totalSuccessAfterRetry.fetch_add(1, std::memory_order_relaxed);
            }
            break;
        }
        
        // 如果还有重试机会
        if (attempt < m_retryConfig.maxRetries) {
            auto delay = m_retryConfig.getRetryDelay(attempt);
            std::this_thread::sleep_for(delay);
            m_retryStats.totalRetries.fetch_add(1, std::memory_order_relaxed);
            attempt++;
        } else {
            break;
        }
    }
    
    return response;
}

HttpResponse HttpClient::executeOnce(const HttpRequest& request) {
    HttpResponse response;
    
    try {
        // 获取或创建客户端
        auto client = getOrCreateClient(request.url);
        if (!client) {
            response.error = "Failed to create HTTP client";
            return response;
        }
        
        // 准备请求头
        httplib::Headers headers;
        for (const auto& [key, value] : request.headers) {
            headers.emplace(key, value);
        }
        
        // 可选健康检查（轻量占位）
        if (m_enableHealthCheck) {
            client->set_keep_alive(true);
        }

        // 执行请求
        httplib::Result result;
        std::string fullUrl = request.buildUrl();
        std::string path = fullUrl;
        
        // 提取路径部分，避免npos溢出
        const auto schemePos = fullUrl.find("://");
        const auto searchStart = (schemePos == std::string::npos) ? 0 : schemePos + 3;
        const auto pathStart = fullUrl.find('/', searchStart);
        if (pathStart != std::string::npos) {
            path = fullUrl.substr(pathStart);
        }
        
        switch (request.method) {
            case HttpMethod::GET: {
                result = client->Get(path.c_str(), headers);
                break;
            }
            case HttpMethod::POST: {
                result = client->Post(path.c_str(), headers, request.body, 
                                    request.getHeader("Content-Type").value_or("application/json").c_str());
                break;
            }
            case HttpMethod::PUT: {
                result = client->Put(path.c_str(), headers, request.body,
                                    request.getHeader("Content-Type").value_or("application/json").c_str());
                break;
            }
            case HttpMethod::DELETE: {
                result = client->Delete(path.c_str(), headers);
                break;
            }
            case HttpMethod::PATCH: {
                // httplib可能不支持PATCH，这里可以扩展
                response.error = "PATCH method not yet implemented";
                response.statusCode = 501;
                return response;
            }
            default: {
                response.error = "Unsupported HTTP method";
                response.statusCode = 501;
                return response;
            }
        }
        
        if (result) {
            response.statusCode = result->status;
            response.body = result->body;
            
            // 复制响应头
            for (const auto& [key, value] : result->headers) {
                response.headers[key] = value;
            }
        } else {
            response.statusCode = 0;
            response.error = "Request failed: error_code=" +
                             std::to_string(static_cast<int>(result.error()));
        }
        
    } catch (const std::exception& e) {
        response.statusCode = 0;
        response.error = "Exception: " + std::string(e.what());
    }
    
    return response;
}

std::string HttpClient::methodToString(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET: return "GET";
        case HttpMethod::POST: return "POST";
        case HttpMethod::PUT: return "PUT";
        case HttpMethod::DELETE: return "DELETE";
        case HttpMethod::PATCH: return "PATCH";
        case HttpMethod::HEAD: return "HEAD";
        case HttpMethod::OPTIONS: return "OPTIONS";
        default: return "UNKNOWN";
    }
}

std::string HttpClient::buildFullUrl(const std::string& path) const {
    if (m_baseUrl.empty()) {
        return path;
    }
    
    // 如果path已经是完整URL，直接返回
    if (path.find("://") != std::string::npos) {
        return path;
    }
    
    // 拼接baseUrl和path
    std::string fullUrl = m_baseUrl;
    if (fullUrl.back() == '/' && path.front() == '/') {
        fullUrl.pop_back();
    } else if (fullUrl.back() != '/' && path.front() != '/') {
        fullUrl += '/';
    }
    fullUrl += path;
    
    return fullUrl;
}

bool HttpClient::isRetryableError(const HttpResponse& response) const {
    const auto type = classifyStatus(response.statusCode);
    switch (type) {
        case HttpErrorType::Network:
            return true;
        case HttpErrorType::Timeout:
            return true;
        case HttpErrorType::RateLimit:
            return m_retryConfig.retryOnRateLimit;
        case HttpErrorType::Server:
            return m_retryConfig.retryOnServerError;
        default:
            return false;
    }
}

std::map<std::string, std::string> HttpClient::mergeHeaders(
    const std::map<std::string, std::string>& requestHeaders) const {
    std::map<std::string, std::string> merged = m_defaultHeaders;
    merged.insert(requestHeaders.begin(), requestHeaders.end());
    return merged;
}

size_t HttpClient::getActiveConnections() const {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    return m_clientPool.size();
}

size_t HttpClient::getTotalConnections() const {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    return m_totalConnections;
}

size_t HttpClient::getReusedConnections() const {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    return m_reusedConnections;
}

double HttpClient::getConnectionReuseRate() const {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    if (m_totalConnections == 0) {
        return 0.0;
    }
    return static_cast<double>(m_reusedConnections) /
           static_cast<double>(m_totalConnections);
}

RetryStatsSnapshot HttpClient::getRetryStats() const {
    return m_retryStats.snapshot();
}

HttpErrorType HttpClient::classifyStatus(int statusCode) {
    if (statusCode == 0) {
        return HttpErrorType::Network;
    }
    if (statusCode == 408) {
        return HttpErrorType::Timeout;
    }
    if (statusCode == 429) {
        return HttpErrorType::RateLimit;
    }
    if (statusCode >= 500 && statusCode < 600) {
        return HttpErrorType::Server;
    }
    if (statusCode >= 400 && statusCode < 500) {
        return HttpErrorType::Client;
    }
    return HttpErrorType::None;
}

void HttpClient::pruneIdleClients() {
    const auto now = std::chrono::steady_clock::now();
    for (auto it = m_clientPool.begin(); it != m_clientPool.end(); ) {
        if (now - it->second.lastUsed > m_poolConfig.idleTimeout) {
            it = m_clientPool.erase(it);
        } else {
            ++it;
        }
    }
}

void HttpClient::enforcePoolLimits() {
    // 如果超出总连接数限制，淘汰最久未使用的
    while (m_clientPool.size() >= m_poolConfig.maxConnections) {
        auto oldest = m_clientPool.end();
        for (auto it = m_clientPool.begin(); it != m_clientPool.end(); ++it) {
            if (oldest == m_clientPool.end() ||
                it->second.lastUsed < oldest->second.lastUsed) {
                oldest = it;
            }
        }
        if (oldest != m_clientPool.end()) {
            m_clientPool.erase(oldest);
        } else {
            break;
        }
    }
}

// ========== 线程池实现 ==========

std::future<HttpResponse> HttpClient::submitAsyncTask(std::function<HttpResponse()> task) {
    std::packaged_task<HttpResponse()> packaged(std::move(task));
    auto fut = packaged.get_future();

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_stopWorkers) {
            // 若已停止，立即返回错误
            packaged();
            return fut;
        }

        // std::function 需可复制，使用 shared_ptr 持有 move-only packaged_task
        auto taskPtr = std::make_shared<std::packaged_task<HttpResponse()>>(std::move(packaged));
        m_tasks.emplace([taskPtr]() { (*taskPtr)(); });
    }
    m_queueCv.notify_one();
    return fut;
}

void HttpClient::startWorkers(size_t threadCount) {
    stopWorkers();  // 防御性，先停旧池
    m_stopWorkers = false;
    m_threadCount = std::max<size_t>(1, threadCount);

    for (size_t i = 0; i < m_threadCount; ++i) {
        m_workers.emplace_back([this]() {
            for (;;) {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> lock(m_queueMutex);
                    m_queueCv.wait(lock, [this]() { return m_stopWorkers || !m_tasks.empty(); });
                    if (m_stopWorkers && m_tasks.empty()) {
                        return;
                    }
                    job = std::move(m_tasks.front());
                    m_tasks.pop();
                }
                try {
                    job();
                } catch (...) {
                    // 兜底，防止线程异常退出
                }
            }
        });
    }
}

void HttpClient::stopWorkers() {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_stopWorkers = true;
    }
    m_queueCv.notify_all();
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();
    m_threadCount = 0;

    // 清空剩余任务
    std::queue<std::function<void()>> empty;
    std::swap(m_tasks, empty);
}

} // namespace naw::desktop_pet::service::utils
