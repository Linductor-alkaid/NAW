#pragma once

#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/ErrorTypes.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"

#include <cstdint>
#include <functional>
#include <future>
#include <string>

namespace naw::desktop_pet::service {

/**
 * @brief OpenAI 兼容的 Chat Completions 客户端（支持 SSE 流式）
 *
 * - 同步：chat()
 * - 异步：chatAsync()
 * - 流式：chatStream()，提供 text delta 与 tool_call delta 回调
 */
class APIClient {
public:
    struct ToolCallDelta {
        int index{-1};
        std::string id;
        std::string nameDelta;
        std::string argumentsDelta;
    };

    struct Callbacks {
        std::function<void(std::string_view)> onTextDelta;
        std::function<void(const ToolCallDelta&)> onToolCallDelta;
        std::function<void(const types::ChatResponse&)> onComplete;
        std::function<void(const ErrorInfo&)> onError;
    };

    class ApiClientError : public std::runtime_error {
    public:
        explicit ApiClientError(const ErrorInfo& info);
        const ErrorInfo& errorInfo() const { return m_info; }

    private:
        ErrorInfo m_info;
    };

    explicit APIClient(ConfigManager& cfg);
    ~APIClient() = default;

    // 禁止拷贝/移动：持有引用，且内部含不可移动成员时更安全
    APIClient(const APIClient&) = delete;
    APIClient& operator=(const APIClient&) = delete;
    APIClient(APIClient&&) = delete;
    APIClient& operator=(APIClient&&) = delete;

    // ========== 核心接口 ==========
    types::ChatResponse chat(const types::ChatRequest& req);
    std::future<types::ChatResponse> chatAsync(const types::ChatRequest& req);

    // SSE 流式：阻塞直到完成或出错
    void chatStream(const types::ChatRequest& req, Callbacks cb);

    // ========== 便于测试/诊断 ==========
    std::string getBaseUrl() const { return m_baseUrl; }
    std::string getApiKeyRedacted() const;
    int getDefaultTimeoutMs() const { return m_timeoutMs; }

private:
    ConfigManager& m_cfg;
    std::string m_baseUrl;
    std::string m_apiKey;
    int m_timeoutMs{30000};
};

} // namespace naw::desktop_pet::service

