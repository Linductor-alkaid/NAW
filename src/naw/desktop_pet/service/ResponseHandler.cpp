#include "naw/desktop_pet/service/ResponseHandler.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <sstream>

namespace naw::desktop_pet::service {

// ========== SSE 解码器（复用 APIClient 的逻辑） ==========
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
    explicit ChatStreamAggregator(ResponseHandler::StreamCallbacks cb) : m_cb(std::move(cb)) {}

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

    types::ChatResponse finalize() const {
        types::ChatResponse r;
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
            types::ToolCall tc;
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
    ResponseHandler::StreamCallbacks m_cb;
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

// ========== ResponseHandler 实现 ==========

ResponseHandler::ResponseHandler(ConfigManager& configManager, CacheManager& cacheManager)
    : m_configManager(configManager)
    , m_cacheManager(cacheManager)
{
    loadConfiguration();
}

void ResponseHandler::loadConfiguration() {
    // 读取缓存配置
    if (auto v = m_configManager.get("cache.enabled"); v.has_value() && v->is_boolean()) {
        m_cacheEnabled = v->get<bool>();
    }

    // 读取工具调用缓存配置
    if (auto v = m_configManager.get("response_handler.cache_tool_calls"); v.has_value() && v->is_boolean()) {
        m_cacheToolCalls = v->get<bool>();
    }

    // 读取温度阈值
    if (auto v = m_configManager.get("response_handler.cache_temperature_threshold"); v.has_value() && v->is_number()) {
        m_cacheTemperatureThreshold = v->get<float>();
    }
}

void ResponseHandler::handleStreamResponse(std::istream& stream, StreamCallbacks callbacks) {
    ChatStreamAggregator aggregator(std::move(callbacks));
    SseDecoder decoder;

    std::string chunk;
    constexpr size_t bufferSize = 4096;
    char buffer[bufferSize];

    try {
        while (stream.good() && !aggregator.completed()) {
            stream.read(buffer, bufferSize);
            const size_t bytesRead = static_cast<size_t>(stream.gcount());
            if (bytesRead == 0) break;

            decoder.feed(std::string_view(buffer, bytesRead));
            auto events = decoder.drain();

            for (const auto& event : events) {
                if (event.done) {
                    aggregator.onDone();
                    return;
                }

                if (event.data.empty()) continue;

                try {
                    auto json = nlohmann::json::parse(event.data);
                    aggregator.onChunkJson(json);
                } catch (const nlohmann::json::exception& e) {
                    ErrorInfo error;
                    error.errorType = ErrorType::UnknownError;
                    error.message = "Failed to parse SSE JSON: " + std::string(e.what());
                    error.details = nlohmann::json{{"data", event.data}};
                    aggregator.onError(error);
                    return;
                }
            }
        }

        // 流结束但未收到 [DONE]
        if (!aggregator.completed()) {
            aggregator.onDone();
        }
    } catch (const std::exception& e) {
        ErrorInfo error;
        error.errorType = ErrorType::UnknownError;
        error.message = "Stream processing error: " + std::string(e.what());
        aggregator.onError(error);
    }
}

bool ResponseHandler::validateResponse(const nlohmann::json& json, ErrorInfo* error) {
    // JSON 已经是解析后的对象，直接进行结构验证
    if (!validateResponseStructure(json, error)) {
        return false;
    }

    if (!checkRequiredFields(json, error)) {
        return false;
    }

    // 尝试转换为 ChatResponse 并验证内容
    auto response = types::ChatResponse::fromJson(json);
    if (!response.has_value()) {
        if (error) {
            error->errorType = ErrorType::InvalidRequest;
            error->message = "Failed to parse ChatResponse from JSON";
            error->details = json;
        }
        return false;
    }

    return validateResponse(*response, error);
}

bool ResponseHandler::validateResponse(const types::ChatResponse& response, ErrorInfo* error) {
    return validateResponseContent(response, error);
}

bool ResponseHandler::validateJsonFormat(const std::string& jsonStr, ErrorInfo* error) const {
    // 如果传入的是空字符串，说明 JSON 已经被解析过了，跳过格式验证
    if (jsonStr.empty()) return true;

    try {
        nlohmann::json::parse(jsonStr);
        return true;
    } catch (const nlohmann::json::exception& e) {
        if (error) {
            error->errorType = ErrorType::InvalidRequest;
            error->message = "Invalid JSON format: " + std::string(e.what());
        }
        return false;
    }
}

bool ResponseHandler::validateResponseStructure(const nlohmann::json& json, ErrorInfo* error) const {
    if (!json.is_object()) {
        if (error) {
            error->errorType = ErrorType::InvalidRequest;
            error->message = "Response must be a JSON object";
        }
        return false;
    }

    // 检查 choices 字段
    if (!json.contains("choices") || !json["choices"].is_array() || json["choices"].empty()) {
        if (error) {
            error->errorType = ErrorType::InvalidRequest;
            error->message = "Response must contain a non-empty 'choices' array";
        }
        return false;
    }

    const auto& choices = json["choices"];
    if (!choices[0].is_object()) {
        if (error) {
            error->errorType = ErrorType::InvalidRequest;
            error->message = "First choice must be an object";
        }
        return false;
    }

    return true;
}

bool ResponseHandler::checkRequiredFields(const nlohmann::json& json, ErrorInfo* error) const {
    const auto& choices = json["choices"];
    const auto& c0 = choices[0];

    // 检查 message 字段
    if (!c0.contains("message") || !c0["message"].is_object()) {
        if (error) {
            error->errorType = ErrorType::InvalidRequest;
            error->message = "Choice must contain a 'message' object";
        }
        return false;
    }

    // usage 字段是可选的，但建议存在
    // 这里不强制要求

    return true;
}

bool ResponseHandler::validateResponseContent(const types::ChatResponse& response, ErrorInfo* error) const {
    // 检查 finish_reason 是否有效
    if (response.finishReason.has_value()) {
        const auto& reason = *response.finishReason;
        const std::vector<std::string> validReasons = {"stop", "length", "tool_calls", "content_filter", "null"};
        bool isValid = std::find(validReasons.begin(), validReasons.end(), reason) != validReasons.end();
        if (!isValid) {
            if (error) {
                error->errorType = ErrorType::InvalidRequest;
                error->message = "Invalid finish_reason: " + reason;
            }
            return false;
        }
    }

    // 对于非工具调用响应，内容不应该为空（除非 finish_reason 是 length 或其他合理原因）
    if (response.toolCalls.empty() && response.content.empty() && 
        (!response.finishReason.has_value() || *response.finishReason != "length")) {
        // 这可能是正常的（例如某些模型可能返回空内容），所以这里只作为警告
        // 如果需要严格验证，可以返回 false
    }

    // 验证工具调用参数
    for (const auto& toolCall : response.toolCalls) {
        if (!toolCall.function.arguments.is_object() && !toolCall.function.arguments.is_string()) {
            if (error) {
                error->errorType = ErrorType::InvalidRequest;
                error->message = "Tool call arguments must be an object or string";
            }
            return false;
        }
    }

    return true;
}

std::optional<types::ChatResponse> ResponseHandler::checkCache(const types::ChatRequest& request) {
    if (!m_cacheEnabled) return std::nullopt;

    auto key = m_cacheManager.generateKey(request);
    auto cached = m_cacheManager.get(key);
    
    std::lock_guard<std::mutex> lock(m_statisticsMutex);
    m_statistics.totalResponses++;
    
    if (cached.has_value()) {
        m_statistics.cachedResponses++;
        // 更新响应大小统计
        size_t size = estimateResponseSize(*cached);
        m_statistics.totalResponseSize += size;
    }

    return cached;
}

void ResponseHandler::storeCache(const types::ChatRequest& request, const types::ChatResponse& response) {
    if (!m_cacheEnabled) return;
    if (!shouldCache(request)) return;

    auto key = m_cacheManager.generateKey(request);
    m_cacheManager.put(key, response);
}

bool ResponseHandler::shouldCache(const types::ChatRequest& request) const {
    // 不缓存流式请求
    if (request.stream.has_value() && *request.stream) return false;

    // 检查温度参数
    if (request.temperature.has_value()) {
        if (*request.temperature > m_cacheTemperatureThreshold) return false;
    }

    // 检查工具调用缓存策略
    if (!request.tools.empty() && !m_cacheToolCalls) return false;

    return true;
}

ResponseHandler::ResponseStatistics ResponseHandler::getStatistics() const {
    std::lock_guard<std::mutex> lock(m_statisticsMutex);
    return m_statistics;
}

double ResponseHandler::getCacheHitRate() const {
    std::lock_guard<std::mutex> lock(m_statisticsMutex);
    return m_statistics.getCacheHitRate();
}

void ResponseHandler::updateStatistics(const types::ChatResponse& response, bool isSuccess, bool isCached,
                                       bool isStreaming) {
    std::lock_guard<std::mutex> lock(m_statisticsMutex);
    
    if (isSuccess) {
        m_statistics.successfulResponses++;
    } else {
        m_statistics.failedResponses++;
    }

    if (isStreaming) {
        m_statistics.streamingResponses++;
    }

    size_t size = estimateResponseSize(response);
    m_statistics.totalResponseSize += size;
}

size_t ResponseHandler::estimateResponseSize(const types::ChatResponse& response) const {
    size_t size = 0;
    
    // 内容大小
    size += response.content.size();
    
    // 工具调用大小
    for (const auto& toolCall : response.toolCalls) {
        size += toolCall.id.size();
        size += toolCall.type.size();
        size += toolCall.function.name.size();
        size += toolCall.function.arguments.dump().size();
    }
    
    // finish_reason
    if (response.finishReason.has_value()) {
        size += response.finishReason->size();
    }
    
    // model
    if (response.model.has_value()) {
        size += response.model->size();
    }
    
    // token 计数（每个 uint32_t 4 字节）
    size += sizeof(uint32_t) * 3;
    
    return size;
}

} // namespace naw::desktop_pet::service
