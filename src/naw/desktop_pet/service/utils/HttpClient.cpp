#include "naw/desktop_pet/service/utils/HttpClient.h"
#include "naw/desktop_pet/service/utils/HttpTypes.h"

// 包含cpp-httplib头文件
// 注意：如果不需要HTTPS支持，可以移除CPPHTTPLIB_OPENSSL_SUPPORT
// #define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"

#include <sstream>
#include <thread>
#include <chrono>
#include <random>
#include <algorithm>
#include <cctype>
#include <cmath>

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
        
        // URL编码（简化版本）
        auto encode = [](const std::string& str) -> std::string {
            std::ostringstream encoded;
            for (unsigned char c : str) {
                if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                    encoded << c;
                } else {
                    encoded << '%' << std::hex << std::uppercase 
                            << static_cast<int>(c) << std::dec;
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
}

HttpClient::~HttpClient() = default;

HttpClient::HttpClient(HttpClient&&) noexcept = default;
HttpClient& HttpClient::operator=(HttpClient&&) noexcept = default;

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

void HttpClient::setSSLVerification(bool verify) {
    m_sslVerification = verify;
}

void HttpClient::setCACertPath(const std::string& path) {
    m_caCertPath = path;
}

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
    return std::async(std::launch::async, [this, path, params, headers]() {
        return get(path, params, headers);
    });
}

std::future<HttpResponse> HttpClient::postAsync(const std::string& path,
                                               const std::string& body,
                                               const std::string& contentType,
                                               const std::map<std::string, std::string>& headers) {
    return std::async(std::launch::async, [this, path, body, contentType, headers]() {
        return post(path, body, contentType, headers);
    });
}

std::future<HttpResponse> HttpClient::postJsonAsync(const std::string& path,
                                                   const std::string& jsonBody,
                                                   const std::map<std::string, std::string>& headers) {
    return std::async(std::launch::async, [this, path, jsonBody, headers]() {
        return postJson(path, jsonBody, headers);
    });
}

std::future<HttpResponse> HttpClient::executeAsync(const HttpRequest& request) {
    return std::async(std::launch::async, [this, request]() {
        return execute(request);
    });
}

// ========== 连接池统计 ==========

size_t HttpClient::getActiveConnections() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_activeConnections;
}

size_t HttpClient::getTotalConnections() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_totalConnections;
}

double HttpClient::getConnectionReuseRate() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    if (m_totalConnections == 0) {
        return 0.0;
    }
    return static_cast<double>(m_reusedConnections) / m_totalConnections;
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
    
    // 查找现有客户端
    auto it = m_clientPool.find(host);
    if (it != m_clientPool.end() && it->second) {
        {
            std::lock_guard<std::mutex> statsLock(m_statsMutex);
            m_reusedConnections++;
        }
        return it->second;
    }
    
    // 创建新客户端
    std::shared_ptr<httplib::Client> client;
    if (scheme == "https") {
        client = std::make_shared<httplib::Client>(host);
        if (!m_sslVerification) {
            client->enable_server_certificate_verification(false);
        }
        if (!m_caCertPath.empty()) {
            client->set_ca_cert_path(m_caCertPath.c_str());
        }
    } else {
        client = std::make_shared<httplib::Client>(host);
    }
    
    // 设置超时
    client->set_connection_timeout(m_poolConfig.connectionTimeout.count() / 1000,
                                   (m_poolConfig.connectionTimeout.count() % 1000) * 1000);
    client->set_read_timeout(m_timeoutMs / 1000, (m_timeoutMs % 1000) * 1000);
    client->set_write_timeout(5, 0);
    
    // 设置跟随重定向
    client->set_follow_location(m_followRedirects);
    
    // 存储到连接池
    m_clientPool[host] = client;
    
    {
        std::lock_guard<std::mutex> statsLock(m_statsMutex);
        m_totalConnections++;
        m_activeConnections++;
    }
    
    return client;
}

HttpResponse HttpClient::executeWithRetry(const HttpRequest& request) {
    HttpResponse response;
    int attempt = 0;
    
    while (attempt <= m_retryConfig.maxRetries) {
        response = executeOnce(request);
        
        // 如果成功或不可重试的错误，直接返回
        if (response.isSuccess() || !isRetryableError(response)) {
            break;
        }
        
        // 如果还有重试机会
        if (attempt < m_retryConfig.maxRetries) {
            auto delay = m_retryConfig.getRetryDelay(attempt);
            std::this_thread::sleep_for(delay);
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
        
        // 执行请求
        httplib::Result result;
        std::string fullUrl = request.buildUrl();
        std::string path = fullUrl;
        
        // 提取路径部分
        size_t pathStart = fullUrl.find('/', fullUrl.find("://") + 3);
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
            response.error = "Request failed: " + std::string(result.error());
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
    // 网络错误（statusCode为0）
    if (response.statusCode == 0) {
        return true;
    }
    
    // 服务器错误（5xx）通常可重试
    if (response.isServerError()) {
        return true;
    }
    
    // 限流错误（429）可重试
    if (response.statusCode == 429) {
        return true;
    }
    
    // 请求超时（408）可重试
    if (response.statusCode == 408) {
        return true;
    }
    
    // 其他客户端错误（4xx）通常不可重试
    return false;
}

std::map<std::string, std::string> HttpClient::mergeHeaders(
    const std::map<std::string, std::string>& requestHeaders) const {
    std::map<std::string, std::string> merged = m_defaultHeaders;
    merged.insert(requestHeaders.begin(), requestHeaders.end());
    return merged;
}

} // namespace naw::desktop_pet::service::utils
