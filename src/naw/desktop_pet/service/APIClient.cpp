#include "naw/desktop_pet/service/APIClient.h"

#include "naw/desktop_pet/service/ErrorHandler.h"
#include "naw/desktop_pet/service/utils/HttpClient.h"
#include "naw/desktop_pet/service/utils/HttpTypes.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <future>
#include <utility>
#include <vector>

namespace naw::desktop_pet::service {

using naw::desktop_pet::service::types::ChatRequest;
using naw::desktop_pet::service::types::ChatResponse;
using naw::desktop_pet::service::types::ToolCall;
using naw::desktop_pet::service::utils::HttpClient;
using naw::desktop_pet::service::utils::HttpMethod;
using naw::desktop_pet::service::utils::HttpRequest;
using naw::desktop_pet::service::utils::HttpResponse;

static std::string trimCopy(std::string s) {
    auto notSpace = [](int ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

static int asIntOr(const std::optional<nlohmann::json>& j, int fallback) {
    if (!j.has_value()) return fallback;
    if (j->is_number_integer()) return j->get<int>();
    if (j->is_number()) return static_cast<int>(j->get<double>());
    return fallback;
}

static std::string joinUrl(const std::string& base, const std::string& path) {
    if (base.empty()) return path;
    if (path.empty()) return base;
    if (base.back() == '/' && path.front() == '/') return base.substr(0, base.size() - 1) + path;
    if (base.back() != '/' && path.front() != '/') return base + "/" + path;
    return base + path;
}

static void upsertContextField(ErrorInfo& info, const std::string& key, const std::string& value) {
    if (value.empty()) return;
    if (!info.context.has_value()) info.context = std::map<std::string, std::string>{};
    (*info.context)[key] = value;
}

static void enrichErrorInfoContext(
    ErrorInfo& info,
    const std::string& model,
    const std::string& endpoint,
    const std::optional<HttpRequest>& req = std::nullopt) {
    // 注意：不写入任何敏感信息（例如 api_key、Authorization header）
    upsertContextField(info, "model", model);
    upsertContextField(info, "endpoint", endpoint);
    if (req.has_value()) {
        upsertContextField(info, "url", req->url);
        upsertContextField(info, "method", std::to_string(static_cast<int>(req->method)));
    }
    // attempt/requestId：若未来可得再补齐；这里不强行填充
}

APIClient::ApiClientError::ApiClientError(const ErrorInfo& info)
    : std::runtime_error(info.toString())
    , m_info(info)
{}

APIClient::APIClient(ConfigManager& cfg)
    : m_cfg(cfg)
{
    // 读取配置（假设上层已 applyEnvironmentOverrides；但这里仍按当前值读）
    m_baseUrl = "https://api.siliconflow.cn/v1";
    if (auto v = m_cfg.get("api.base_url"); v.has_value() && v->is_string()) {
        const auto s = trimCopy(v->get<std::string>());
        if (!s.empty()) m_baseUrl = s;
    }

    if (auto v = m_cfg.get("api.api_key"); v.has_value() && v->is_string()) {
        m_apiKey = trimCopy(v->get<std::string>());
    }

    if (auto v = m_cfg.get("api.default_timeout_ms"); v.has_value()) {
        m_timeoutMs = asIntOr(v, 30000);
        if (m_timeoutMs <= 0) m_timeoutMs = 30000;
    }
}

std::string APIClient::getApiKeyRedacted() const {
    return ConfigManager::redactSensitive("api.api_key", m_apiKey);
}

APIClient::ApiConfig APIClient::getApiConfigForModel(const std::string& modelId) const {
    ApiConfig config;
    config.baseUrl = m_baseUrl;  // 默认值
    config.apiKey = m_apiKey;    // 默认值
    config.timeoutMs = m_timeoutMs; // 默认值

    // 1. 从 models 数组中查找模型配置
    if (auto modelsNode = m_cfg.get("models"); modelsNode.has_value() && modelsNode->is_array()) {
        for (const auto& modelNode : *modelsNode) {
            if (!modelNode.is_object()) continue;
            if (!modelNode.contains("model_id") || !modelNode["model_id"].is_string()) continue;
            
            const std::string& id = modelNode["model_id"].get<std::string>();
            if (id != modelId) continue;

            // 找到匹配的模型，检查是否有 api_provider
            if (modelNode.contains("api_provider") && modelNode["api_provider"].is_string()) {
                const std::string provider = modelNode["api_provider"].get<std::string>();
                
                // 从 api_providers 中获取配置
                std::string providerPath = "api_providers." + provider;
                if (auto providerNode = m_cfg.get(providerPath); providerNode.has_value() && providerNode->is_object()) {
                    // 获取 base_url
                    if (auto urlNode = m_cfg.get(providerPath + ".base_url"); urlNode.has_value() && urlNode->is_string()) {
                        const auto s = trimCopy(urlNode->get<std::string>());
                        if (!s.empty()) config.baseUrl = s;
                    }
                    
                    // 获取 api_key
                    if (auto keyNode = m_cfg.get(providerPath + ".api_key"); keyNode.has_value() && keyNode->is_string()) {
                        const auto s = trimCopy(keyNode->get<std::string>());
                        if (!s.empty()) config.apiKey = s;
                    }
                    
                    // 获取 timeout
                    if (auto timeoutNode = m_cfg.get(providerPath + ".default_timeout_ms"); timeoutNode.has_value()) {
                        int timeout = asIntOr(timeoutNode, config.timeoutMs);
                        if (timeout > 0) config.timeoutMs = timeout;
                    }
                }
            }
            // 如果模型没有指定 api_provider，使用默认配置
            break;
        }
    }

    return config;
}

static void configureHttpClient(HttpClient& client, const std::string& apiKey, int timeoutMs) {
    // 注意：不打印 api_key；仅设置 header
    if (!apiKey.empty()) client.setDefaultHeader("Authorization", "Bearer " + apiKey);
    client.setDefaultHeader("Content-Type", "application/json");
    client.setDefaultHeader("Accept", "application/json");
    client.setTimeout(timeoutMs);
}

types::ChatResponse APIClient::chat(const types::ChatRequest& req) {
    // 根据模型ID获取对应的API配置
    const auto apiConfig = getApiConfigForModel(req.model);
    
    HttpClient client(apiConfig.baseUrl);
    configureHttpClient(client, apiConfig.apiKey, apiConfig.timeoutMs);

    const std::string endpoint = "/chat/completions";

    HttpRequest hreq;
    hreq.method = HttpMethod::POST;
    hreq.url = joinUrl(apiConfig.baseUrl, endpoint);
    hreq.body = req.toJson().dump();
    hreq.timeoutMs = apiConfig.timeoutMs;
    hreq.followRedirects = true;
    // 明确设置 headers，确保 ErrorHandler 上下文可关联请求（且不包含敏感信息）
    if (!apiConfig.apiKey.empty()) hreq.headers["Authorization"] = "Bearer " + apiConfig.apiKey;
    hreq.headers["Content-Type"] = "application/json";
    hreq.headers["Accept"] = "application/json";

    const auto resp = client.execute(hreq);
    if (!resp.isSuccess()) {
        auto info = ErrorHandler::fromHttpResponse(resp, std::optional<HttpRequest>{hreq});
        enrichErrorInfoContext(info, req.model, endpoint, std::optional<HttpRequest>{hreq});
        throw ApiClientError(info);
    }

    auto parsed = resp.asJson();
    if (!parsed.has_value()) {
        ErrorInfo info;
        info.errorType = ErrorType::UnknownError;
        info.errorCode = resp.statusCode;
        info.message = "Invalid JSON response";
        info.details = nlohmann::json{
            {"body_snippet", resp.body.substr(0, 1024)},
        };
        enrichErrorInfoContext(info, req.model, endpoint, std::optional<HttpRequest>{hreq});
        throw ApiClientError(info);
    }

    auto r = ChatResponse::fromJson(*parsed);
    if (!r.has_value()) {
        ErrorInfo info;
        info.errorType = ErrorType::UnknownError;
        info.errorCode = resp.statusCode;
        info.message = "Failed to parse ChatResponse";
        info.details = *parsed;
        enrichErrorInfoContext(info, req.model, endpoint, std::optional<HttpRequest>{hreq});
        throw ApiClientError(info);
    }
    return *r;
}

std::future<types::ChatResponse> APIClient::chatAsync(const types::ChatRequest& req) {
    // 保持简单：复用同步实现，并通过 future 异常传播错误
    return std::async(std::launch::async, [this, req]() { return this->chat(req); });
}

std::future<types::ChatResponse> APIClient::chatAsync(const types::ChatRequest& req, utils::HttpClient::CancelToken* token) {
    // 使用 HttpClient 的异步方法，支持取消
    return std::async(std::launch::async, [this, req, token]() -> types::ChatResponse {
        // 根据模型ID获取对应的API配置
        const auto apiConfig = getApiConfigForModel(req.model);
        
        HttpClient client(apiConfig.baseUrl);
        configureHttpClient(client, apiConfig.apiKey, apiConfig.timeoutMs);

        const std::string endpoint = "/chat/completions";
        const std::string url = joinUrl(apiConfig.baseUrl, endpoint);
        const std::string body = req.toJson().dump();

        // 准备请求头
        std::map<std::string, std::string> headers;
        if (!apiConfig.apiKey.empty()) headers["Authorization"] = "Bearer " + apiConfig.apiKey;
        headers["Content-Type"] = "application/json";
        headers["Accept"] = "application/json";

        // 使用异步POST请求，支持取消
        auto future = client.postAsync(url, body, "application/json", headers, nullptr, token);
        const auto resp = future.get();

        // 构建 HttpRequest 用于错误上下文
        HttpRequest hreq;
        hreq.method = HttpMethod::POST;
        hreq.url = url;
        hreq.body = body;
        hreq.timeoutMs = apiConfig.timeoutMs;
        hreq.followRedirects = true;
        hreq.headers = headers;

        if (!resp.isSuccess()) {
            auto info = ErrorHandler::fromHttpResponse(resp, std::optional<HttpRequest>{hreq});
            enrichErrorInfoContext(info, req.model, endpoint, std::optional<HttpRequest>{hreq});
            throw ApiClientError(info);
        }

        auto parsed = resp.asJson();
        if (!parsed.has_value()) {
            ErrorInfo info;
            info.errorType = ErrorType::UnknownError;
            info.errorCode = resp.statusCode;
            info.message = "Invalid JSON response";
            info.details = nlohmann::json{
                {"body_snippet", resp.body.substr(0, 1024)},
            };
            enrichErrorInfoContext(info, req.model, endpoint, std::optional<HttpRequest>{hreq});
            throw ApiClientError(info);
        }

        auto r = ChatResponse::fromJson(*parsed);
        if (!r.has_value()) {
            ErrorInfo info;
            info.errorType = ErrorType::UnknownError;
            info.errorCode = resp.statusCode;
            info.message = "Failed to parse ChatResponse";
            info.details = *parsed;
            enrichErrorInfoContext(info, req.model, endpoint, std::optional<HttpRequest>{hreq});
            throw ApiClientError(info);
        }
        return *r;
    });
}

// ========= SSE decoder =========
namespace {

struct SseEvent {
    std::string data; // 多行 data: 拼接后，以 '\n' 连接
    bool done{false};
};

class SseDecoder {
public:
    void feed(std::string_view chunk) { m_buf.append(chunk.data(), chunk.size()); }

    // 尽可能多产出 events
    std::vector<SseEvent> drain() {
        std::vector<SseEvent> out;
        for (;;) {
            const auto sepPos = findEventSeparator(m_buf);
            if (sepPos == std::string::npos) break;

            const auto sepLen = separatorLengthAt(m_buf, sepPos);
            const std::string raw = m_buf.substr(0, sepPos);
            m_buf.erase(0, sepPos + sepLen);

            auto ev = parseOne(raw);
            if (ev.has_value()) out.push_back(std::move(*ev));
        }
        return out;
    }

private:
    std::string m_buf;

    static size_t findEventSeparator(const std::string& s) {
        // 优先找 "\n\n"，兼容 "\r\n\r\n"
        auto p1 = s.find("\n\n");
        auto p2 = s.find("\r\n\r\n");
        if (p1 == std::string::npos) return p2;
        if (p2 == std::string::npos) return p1;
        return std::min(p1, p2);
    }

    static size_t separatorLengthAt(const std::string& s, size_t pos) {
        if (pos + 4 <= s.size() && s.compare(pos, 4, "\r\n\r\n") == 0) return 4;
        if (pos + 2 <= s.size() && s.compare(pos, 2, "\n\n") == 0) return 2;
        return 2;
    }

    static std::optional<SseEvent> parseOne(const std::string& raw) {
        // 逐行处理，提取 data:
        std::string data;
        size_t i = 0;
        bool first = true;
        while (i < raw.size()) {
            auto eol = raw.find('\n', i);
            if (eol == std::string::npos) eol = raw.size();
            std::string line = raw.substr(i, eol - i);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            i = (eol == raw.size()) ? raw.size() : eol + 1;

            if (line.rfind("data:", 0) == 0) {
                std::string piece = line.substr(5);
                if (!piece.empty() && piece.front() == ' ') piece.erase(piece.begin());
                if (!first) data.push_back('\n');
                first = false;
                data += piece;
            }
        }

        if (data.empty()) return std::nullopt;
        SseEvent ev;
        ev.data = std::move(data);
        ev.done = (ev.data == "[DONE]");
        return ev;
    }
};

struct ToolCallBuild {
    int index{-1};
    std::string id;
    std::string type{"function"};
    std::string functionName;
    std::string functionArguments; // 流式是 string fragments
};

class ChatStreamAggregator {
public:
    explicit ChatStreamAggregator(APIClient::Callbacks cb) : m_cb(std::move(cb)) {}

    void onChunkJson(const nlohmann::json& j) {
        // OpenAI stream: choices[0].delta / choices[0].finish_reason / model
        if (j.contains("model") && j["model"].is_string()) m_model = j["model"].get<std::string>();

        if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) return;
        const auto& c0 = j["choices"][0];
        if (!c0.is_object()) return;

        if (c0.contains("finish_reason") && c0["finish_reason"].is_string()) {
            m_finishReason = c0["finish_reason"].get<std::string>();
        }

        // delta: streaming; fallback message: some providers
        const nlohmann::json* delta = nullptr;
        if (c0.contains("delta") && c0["delta"].is_object()) delta = &c0["delta"];
        else if (c0.contains("message") && c0["message"].is_object()) delta = &c0["message"];
        if (!delta) return;

        // content delta
        if (delta->contains("content") && (*delta)["content"].is_string()) {
            const auto piece = (*delta)["content"].get<std::string>();
            if (!piece.empty()) {
                m_content += piece;
                if (m_cb.onTextDelta) m_cb.onTextDelta(std::string_view{piece});
            }
        }

        // tool_calls delta
        if (delta->contains("tool_calls") && (*delta)["tool_calls"].is_array()) {
            for (const auto& tcj : (*delta)["tool_calls"]) {
                if (!tcj.is_object()) continue;

                int idx = -1;
                if (tcj.contains("index") && tcj["index"].is_number_integer())
                    idx = tcj["index"].get<int>();

                std::string id;
                if (tcj.contains("id") && tcj["id"].is_string()) id = tcj["id"].get<std::string>();

                std::string type = "function";
                if (tcj.contains("type") && tcj["type"].is_string()) type = tcj["type"].get<std::string>();

                std::string nameDelta;
                std::string argsDelta;
                if (tcj.contains("function") && tcj["function"].is_object()) {
                    const auto& fn = tcj["function"];
                    if (fn.contains("name") && fn["name"].is_string()) nameDelta = fn["name"].get<std::string>();
                    if (fn.contains("arguments") && fn["arguments"].is_string())
                        argsDelta = fn["arguments"].get<std::string>();
                }

                auto& b = getOrCreateBuild(idx, id);
                if (!id.empty()) b.id = id;
                if (!type.empty()) b.type = type;
                if (!nameDelta.empty()) b.functionName += nameDelta;
                if (!argsDelta.empty()) b.functionArguments += argsDelta;

                if (m_cb.onToolCallDelta && (!nameDelta.empty() || !argsDelta.empty())) {
                    APIClient::ToolCallDelta d;
                    d.index = b.index;
                    d.id = b.id;
                    d.nameDelta = nameDelta;
                    d.argumentsDelta = argsDelta;
                    m_cb.onToolCallDelta(d);
                }
            }
        }
    }

    ChatResponse finalize() const {
        ChatResponse r;
        r.content = m_content;
        r.finishReason = m_finishReason;
        r.model = m_model;
        r.promptTokens = 0;
        r.completionTokens = 0;
        r.totalTokens = 0;

        // tool_calls：按 index 排序输出
        std::vector<ToolCallBuild> builds;
        builds.reserve(m_toolCalls.size());
        for (const auto& kv : m_toolCalls) builds.push_back(kv.second);
        std::sort(builds.begin(), builds.end(), [](const auto& a, const auto& b) { return a.index < b.index; });

        for (const auto& b : builds) {
            ToolCall tc;
            tc.id = b.id.empty() ? ("toolcall_" + std::to_string(b.index)) : b.id;
            tc.type = b.type.empty() ? "function" : b.type;
            tc.function.name = b.functionName;

            // arguments：尽量解析为 JSON object；失败则保留 string
            if (!b.functionArguments.empty()) {
                try {
                    tc.function.arguments = nlohmann::json::parse(b.functionArguments);
                } catch (...) {
                    tc.function.arguments = b.functionArguments;
                }
            } else {
                tc.function.arguments = nlohmann::json::object();
            }
            r.toolCalls.push_back(std::move(tc));
        }

        return r;
    }

    void onDone() {
        if (m_completed) return;
        m_completed = true;
        if (m_cb.onComplete) m_cb.onComplete(finalize());
    }

    void onError(const ErrorInfo& info) {
        if (m_cb.onError) m_cb.onError(info);
    }

    bool completed() const { return m_completed; }

private:
    APIClient::Callbacks m_cb;
    std::string m_content;
    std::optional<std::string> m_finishReason;
    std::optional<std::string> m_model;
    std::map<int, ToolCallBuild> m_toolCalls; // index -> build
    std::map<std::string, int> m_idToIndex;
    bool m_completed{false};

    ToolCallBuild& getOrCreateBuild(int idx, const std::string& id) {
        // 优先 index；若 index 缺失则回退 id
        if (idx < 0 && !id.empty()) {
            auto it = m_idToIndex.find(id);
            if (it != m_idToIndex.end()) idx = it->second;
            else {
                // 分配一个新的 index（保证稳定顺序）
                int next = 0;
                if (!m_toolCalls.empty()) next = std::max(next, m_toolCalls.rbegin()->first + 1);
                idx = next;
                m_idToIndex[id] = idx;
            }
        }
        if (idx < 0) idx = 0;

        auto it = m_toolCalls.find(idx);
        if (it == m_toolCalls.end()) {
            ToolCallBuild b;
            b.index = idx;
            b.id = id;
            auto [ins, _] = m_toolCalls.emplace(idx, std::move(b));
            it = ins;
        }
        if (!id.empty()) m_idToIndex[id] = idx;
        return it->second;
    }
};

} // namespace

void APIClient::chatStream(const types::ChatRequest& req, Callbacks cb) {
    // 构造 stream=true 的请求
    ChatRequest r = req;
    r.stream = true;

    // 根据模型ID获取对应的API配置
    auto apiConfig = getApiConfigForModel(req.model);
    
    // 如果请求包含工具，增加超时时间
    // 工具调用参数可能很大（如write_file包含完整文件内容），流式输出需要更长时间
    if (!req.tools.empty()) {
        // 工具调用请求需要更长的超时时间，特别是流式输出工具调用参数时
        // 默认超时时间 * 3，但不超过10分钟
        int extendedTimeout = apiConfig.timeoutMs * 3;
        int maxTimeout = 10 * 60 * 1000; // 10分钟
        if (extendedTimeout > maxTimeout) {
            extendedTimeout = maxTimeout;
        }
        apiConfig.timeoutMs = extendedTimeout;
    }
    
    HttpClient client(apiConfig.baseUrl);
    configureHttpClient(client, apiConfig.apiKey, apiConfig.timeoutMs);

    HttpRequest hreq;
    hreq.method = HttpMethod::POST;
    const std::string endpoint = "/chat/completions";
    hreq.url = joinUrl(apiConfig.baseUrl, endpoint);
    hreq.body = r.toJson().dump();
    hreq.timeoutMs = apiConfig.timeoutMs;
    hreq.followRedirects = true;

    // httplib send() 走 HttpRequest.headers；HttpClient::executeStream 不会 merge 默认头，因此我们显式补齐
    // 但为了保持与同步接口一致（Authorization 等），这里直接在 request.headers 中设置
    if (!apiConfig.apiKey.empty()) hreq.headers["Authorization"] = "Bearer " + apiConfig.apiKey;
    hreq.headers["Content-Type"] = "application/json";
    hreq.headers["Accept"] = "text/event-stream";

    SseDecoder decoder;
    ChatStreamAggregator agg(std::move(cb));

    std::mutex mu; // 保护 decoder/agg：避免未来底层回调跨线程（当前 httplib 同线程）

    hreq.streamHandler = [&](std::string_view chunk) {
        std::lock_guard<std::mutex> lk(mu);
        decoder.feed(chunk);
        auto events = decoder.drain();
        for (auto& ev : events) {
            if (ev.done) {
                agg.onDone();
                continue;
            }
            try {
                auto j = nlohmann::json::parse(ev.data);
                agg.onChunkJson(j);
            } catch (...) {
                // 忽略单个坏包：某些供应商可能会发送 keep-alive 空行或非 JSON
            }
        }
    };

    const auto resp = client.executeStream(hreq);
    if (!resp.isSuccess()) {
        auto info = ErrorHandler::fromHttpResponse(resp, std::optional<HttpRequest>{hreq});
        enrichErrorInfoContext(info, req.model, endpoint, std::optional<HttpRequest>{hreq});
        agg.onError(info);
        throw ApiClientError(info);
    }

    // executeStream 成功返回时并不包含 body；完成信号通常依赖 [DONE]
    // 为稳健：若服务端未发送 [DONE]，这里仍触发一次完成回调。
    if (!agg.completed()) agg.onDone();
}

// ========== 嵌入和重排序 API ==========

std::vector<std::vector<float>> APIClient::createEmbeddings(
    const std::vector<std::string>& texts,
    const std::string& modelId
) {
    // 获取嵌入模型配置
    std::string embeddingModelId = modelId;
    std::string baseUrl = m_baseUrl;
    std::string apiKey = m_apiKey;
    int timeoutMs = m_timeoutMs;

    if (embeddingModelId.empty()) {
        if (auto v = m_cfg.get("context_refinement.embedding.model_id"); v.has_value() && v->is_string()) {
            embeddingModelId = trimCopy(v->get<std::string>());
        }
        if (embeddingModelId.empty()) {
            embeddingModelId = "BAAI/bge-large-zh-v1.5"; // 默认模型
        }
    }

    // 获取嵌入API配置
    if (auto v = m_cfg.get("context_refinement.embedding.base_url"); v.has_value() && v->is_string()) {
        const auto s = trimCopy(v->get<std::string>());
        if (!s.empty()) baseUrl = s;
    }
    if (auto v = m_cfg.get("context_refinement.embedding.api_key"); v.has_value() && v->is_string()) {
        const auto s = trimCopy(v->get<std::string>());
        if (!s.empty()) apiKey = s;
    }

    HttpClient client(baseUrl);
    configureHttpClient(client, apiKey, timeoutMs);

    const std::string endpoint = "/embeddings";

    // 构建请求体
    nlohmann::json requestBody;
    requestBody["model"] = embeddingModelId;
    requestBody["input"] = nlohmann::json::array();
    for (const auto& text : texts) {
        requestBody["input"].push_back(text);
    }

    HttpRequest hreq;
    hreq.method = HttpMethod::POST;
    hreq.url = joinUrl(baseUrl, endpoint);
    hreq.body = requestBody.dump();
    hreq.timeoutMs = timeoutMs;
    hreq.followRedirects = true;
    if (!apiKey.empty()) hreq.headers["Authorization"] = "Bearer " + apiKey;
    hreq.headers["Content-Type"] = "application/json";
    hreq.headers["Accept"] = "application/json";

    const auto resp = client.execute(hreq);
    if (!resp.isSuccess()) {
        auto info = ErrorHandler::fromHttpResponse(resp, std::optional<HttpRequest>{hreq});
        enrichErrorInfoContext(info, embeddingModelId, endpoint, std::optional<HttpRequest>{hreq});
        throw ApiClientError(info);
    }

    auto parsed = resp.asJson();
    if (!parsed.has_value()) {
        ErrorInfo info;
        info.errorType = ErrorType::UnknownError;
        info.errorCode = resp.statusCode;
        info.message = "Invalid JSON response from embeddings API";
        info.details = nlohmann::json{{"body_snippet", resp.body.substr(0, 1024)}};
        enrichErrorInfoContext(info, embeddingModelId, endpoint, std::optional<HttpRequest>{hreq});
        throw ApiClientError(info);
    }

    // 解析响应
    std::vector<std::vector<float>> embeddings;
    if (parsed->contains("data") && parsed->at("data").is_array()) {
        for (const auto& item : parsed->at("data")) {
            if (item.contains("embedding") && item["embedding"].is_array()) {
                std::vector<float> embedding;
                for (const auto& val : item["embedding"]) {
                    if (val.is_number()) {
                        embedding.push_back(val.get<float>());
                    }
                }
                embeddings.push_back(std::move(embedding));
            }
        }
    }

    if (embeddings.size() != texts.size()) {
        ErrorInfo info;
        info.errorType = ErrorType::UnknownError;
        info.errorCode = 0;
        info.message = "Embeddings count mismatch: expected " + std::to_string(texts.size()) + 
                       ", got " + std::to_string(embeddings.size());
        enrichErrorInfoContext(info, embeddingModelId, endpoint, std::optional<HttpRequest>{hreq});
        throw ApiClientError(info);
    }

    return embeddings;
}

std::vector<APIClient::RerankResult> APIClient::createRerank(
    const std::string& query,
    const std::vector<std::string>& documents,
    const std::string& modelId,
    int topK
) {
    // 获取重排序模型配置
    std::string rerankModelId = modelId;
    std::string baseUrl = m_baseUrl;
    std::string apiKey = m_apiKey;
    int timeoutMs = m_timeoutMs;

    if (rerankModelId.empty()) {
        if (auto v = m_cfg.get("context_refinement.rerank.model_id"); v.has_value() && v->is_string()) {
            rerankModelId = trimCopy(v->get<std::string>());
        }
        if (rerankModelId.empty()) {
            rerankModelId = "BAAI/bge-reranker-large"; // 默认模型
        }
    }

    if (topK < 0) {
        if (auto v = m_cfg.get("context_refinement.rerank.top_k"); v.has_value()) {
            topK = asIntOr(v, 10);
        } else {
            topK = 10; // 默认值
        }
    }

    // 获取重排序API配置
    if (auto v = m_cfg.get("context_refinement.rerank.base_url"); v.has_value() && v->is_string()) {
        const auto s = trimCopy(v->get<std::string>());
        if (!s.empty()) baseUrl = s;
    }
    if (auto v = m_cfg.get("context_refinement.rerank.api_key"); v.has_value() && v->is_string()) {
        const auto s = trimCopy(v->get<std::string>());
        if (!s.empty()) apiKey = s;
    }

    HttpClient client(baseUrl);
    configureHttpClient(client, apiKey, timeoutMs);

    const std::string endpoint = "/rerank";

    // 构建请求体
    nlohmann::json requestBody;
    requestBody["model"] = rerankModelId;
    requestBody["query"] = query;
    requestBody["documents"] = nlohmann::json::array();
    for (const auto& doc : documents) {
        requestBody["documents"].push_back(doc);
    }
    requestBody["top_k"] = topK;

    HttpRequest hreq;
    hreq.method = HttpMethod::POST;
    hreq.url = joinUrl(baseUrl, endpoint);
    hreq.body = requestBody.dump();
    hreq.timeoutMs = timeoutMs;
    hreq.followRedirects = true;
    if (!apiKey.empty()) hreq.headers["Authorization"] = "Bearer " + apiKey;
    hreq.headers["Content-Type"] = "application/json";
    hreq.headers["Accept"] = "application/json";

    const auto resp = client.execute(hreq);
    if (!resp.isSuccess()) {
        auto info = ErrorHandler::fromHttpResponse(resp, std::optional<HttpRequest>{hreq});
        enrichErrorInfoContext(info, rerankModelId, endpoint, std::optional<HttpRequest>{hreq});
        throw ApiClientError(info);
    }

    auto parsed = resp.asJson();
    if (!parsed.has_value()) {
        ErrorInfo info;
        info.errorType = ErrorType::UnknownError;
        info.errorCode = resp.statusCode;
        info.message = "Invalid JSON response from rerank API";
        info.details = nlohmann::json{{"body_snippet", resp.body.substr(0, 1024)}};
        enrichErrorInfoContext(info, rerankModelId, endpoint, std::optional<HttpRequest>{hreq});
        throw ApiClientError(info);
    }

    // 解析响应
    std::vector<RerankResult> results;
    if (parsed->contains("results") && parsed->at("results").is_array()) {
        for (const auto& item : parsed->at("results")) {
            RerankResult result;
            if (item.contains("index") && item["index"].is_number_unsigned()) {
                result.index = item["index"].get<size_t>();
            } else if (item.contains("index") && item["index"].is_number_integer()) {
                result.index = static_cast<size_t>(item["index"].get<int>());
            }
            if (item.contains("score") && item["score"].is_number()) {
                result.score = item["score"].get<float>();
            }
            results.push_back(result);
        }
    }

    return results;
}

} // namespace naw::desktop_pet::service

