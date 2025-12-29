#include "naw/desktop_pet/service/ResponseHandler.h"
#include "naw/desktop_pet/service/CacheManager.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/ErrorTypes.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"
#include "naw/desktop_pet/service/types/ChatMessage.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

using namespace naw::desktop_pet::service;
using namespace naw::desktop_pet::service::types;

// 轻量自测断言工具（复用既有 mini_test 风格）
namespace mini_test {

inline std::string toString(const std::string& v) { return v; }
inline std::string toString(const char* v) { return v ? std::string(v) : "null"; }
inline std::string toString(bool v) { return v ? "true" : "false"; }

inline std::string toString(ErrorType v) {
    using U = std::underlying_type<ErrorType>::type;
    std::ostringstream oss;
    oss << static_cast<U>(v);
    return oss.str();
}

template <typename T>
std::string toString(const T& v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

class AssertionFailed : public std::runtime_error {
public:
    explicit AssertionFailed(const std::string& msg) : std::runtime_error(msg) {}
};

#define CHECK_TRUE(cond)                                                                          \
    do {                                                                                          \
        if (!(cond))                                                                              \
            throw mini_test::AssertionFailed(std::string("CHECK_TRUE failed: ") + #cond);         \
    } while (0)

#define CHECK_FALSE(cond) CHECK_TRUE(!(cond))

#define CHECK_EQ(a, b)                                                                            \
    do {                                                                                          \
        const auto _va = (a);                                                                     \
        const auto _vb = (b);                                                                     \
        if (!(_va == _vb)) {                                                                      \
            throw mini_test::AssertionFailed(std::string("CHECK_EQ failed: ") + #a " vs " #b +    \
                                             " (" + mini_test::toString(_va) + " vs " +           \
                                             mini_test::toString(_vb) + ")");                     \
        }                                                                                         \
    } while (0)

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline int run(const std::vector<TestCase>& tests) {
    int failed = 0;
    for (const auto& t : tests) {
        try {
            t.fn();
            std::cout << "[  OK  ] " << t.name << "\n";
        } catch (const AssertionFailed& e) {
            failed++;
            std::cout << "[ FAIL ] " << t.name << " :: " << e.what() << "\n";
        } catch (const std::exception& e) {
            failed++;
            std::cout << "[ EXC  ] " << t.name << " :: " << e.what() << "\n";
        } catch (...) {
            failed++;
            std::cout << "[ EXC  ] " << t.name << " :: unknown exception\n";
        }
    }
    std::cout << "Executed " << tests.size() << " cases, failed " << failed << ".\n";
    return failed == 0 ? 0 : 1;
}

} // namespace mini_test

// 创建测试用的请求
static ChatRequest createTestRequest(const std::string& modelId, const std::string& content,
                                     std::optional<float> temperature = std::nullopt,
                                     std::optional<uint32_t> maxTokens = std::nullopt,
                                     bool stream = false) {
    ChatRequest req;
    req.model = modelId;
    ChatMessage msg(MessageRole::User, content);
    req.messages.push_back(msg);
    if (temperature.has_value()) req.temperature = temperature;
    if (maxTokens.has_value()) req.maxTokens = maxTokens;
    req.stream = stream;
    return req;
}

// 创建测试用的响应
static ChatResponse createTestResponse(const std::string& content) {
    ChatResponse resp;
    resp.content = content;
    resp.promptTokens = 10;
    resp.completionTokens = 20;
    resp.totalTokens = 30;
    resp.model = "test-model";
    return resp;
}

// 创建测试用的配置管理器
static void createTestConfigManager(ConfigManager& config) {
    nlohmann::json cfg = nlohmann::json::object();
    cfg["cache"] = nlohmann::json::object();
    cfg["cache"]["enabled"] = true;
    cfg["cache"]["default_ttl_seconds"] = 3600;
    cfg["cache"]["max_entries"] = 1000;
    cfg["cache"]["cleanup_interval_seconds"] = 300;
    cfg["response_handler"] = nlohmann::json::object();
    cfg["response_handler"]["cache_tool_calls"] = false;
    cfg["response_handler"]["cache_temperature_threshold"] = 0.01;
    config.loadFromString(cfg.dump());
}

// 创建有效的响应 JSON
static nlohmann::json createValidResponseJson() {
    nlohmann::json json = nlohmann::json::object();
    json["choices"] = nlohmann::json::array();
    nlohmann::json choice = nlohmann::json::object();
    choice["message"] = nlohmann::json::object();
    choice["message"]["content"] = "Hello, world!";
    choice["finish_reason"] = "stop";
    json["choices"].push_back(choice);
    json["usage"] = nlohmann::json::object();
    json["usage"]["prompt_tokens"] = 10;
    json["usage"]["completion_tokens"] = 20;
    json["usage"]["total_tokens"] = 30;
    json["model"] = "test-model";
    return json;
}

int main() {
    using namespace mini_test;

    std::vector<TestCase> tests;

    // ========== 响应验证测试 ==========
    tests.push_back({"ValidateResponse_ValidJson", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        nlohmann::json json = createValidResponseJson();
        CHECK_TRUE(handler.validateResponse(json));
    }});

    tests.push_back({"ValidateResponse_InvalidJson_MissingChoices", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        nlohmann::json json = nlohmann::json::object();
        ErrorInfo error;
        CHECK_FALSE(handler.validateResponse(json, &error));
        CHECK_EQ(error.errorType, ErrorType::InvalidRequest);
    }});

    tests.push_back({"ValidateResponse_InvalidJson_EmptyChoices", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        nlohmann::json json = nlohmann::json::object();
        json["choices"] = nlohmann::json::array();
        ErrorInfo error;
        CHECK_FALSE(handler.validateResponse(json, &error));
    }});

    tests.push_back({"ValidateResponse_InvalidJson_MissingMessage", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        nlohmann::json json = nlohmann::json::object();
        json["choices"] = nlohmann::json::array();
        nlohmann::json choice = nlohmann::json::object();
        json["choices"].push_back(choice);
        ErrorInfo error;
        CHECK_FALSE(handler.validateResponse(json, &error));
    }});

    tests.push_back({"ValidateResponse_ValidChatResponse", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        ChatResponse response = createTestResponse("Hello");
        CHECK_TRUE(handler.validateResponse(response));
    }});

    tests.push_back({"ValidateResponse_InvalidFinishReason", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        ChatResponse response = createTestResponse("Hello");
        response.finishReason = "invalid_reason";
        ErrorInfo error;
        CHECK_FALSE(handler.validateResponse(response, &error));
    }});

    // ========== 缓存集成测试 ==========
    tests.push_back({"CacheIntegration_CheckCache_Miss", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        ChatRequest request = createTestRequest("model1", "Hello", 0.0f);
        auto cached = handler.checkCache(request);
        CHECK_FALSE(cached.has_value());
    }});

    tests.push_back({"CacheIntegration_StoreAndRetrieve", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        ChatRequest request = createTestRequest("model1", "Hello", 0.0f);
        ChatResponse response = createTestResponse("World");

        handler.storeCache(request, response);
        auto cached = handler.checkCache(request);
        CHECK_TRUE(cached.has_value());
        CHECK_EQ(cached->content, "World");
    }});

    tests.push_back({"CacheIntegration_ShouldNotCacheStreamingRequest", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        ChatRequest request = createTestRequest("model1", "Hello", 0.0f, std::nullopt, true);
        ChatResponse response = createTestResponse("World");

        handler.storeCache(request, response);
        auto cached = handler.checkCache(request);
        CHECK_FALSE(cached.has_value());
    }});

    tests.push_back({"CacheIntegration_ShouldNotCacheHighTemperature", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        ChatRequest request = createTestRequest("model1", "Hello", 1.0f);
        ChatResponse response = createTestResponse("World");

        handler.storeCache(request, response);
        auto cached = handler.checkCache(request);
        CHECK_FALSE(cached.has_value());
    }});

    tests.push_back({"CacheIntegration_ShouldCacheLowTemperature", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        ChatRequest request = createTestRequest("model1", "Hello", 0.0f);
        ChatResponse response = createTestResponse("World");

        handler.storeCache(request, response);
        auto cached = handler.checkCache(request);
        CHECK_TRUE(cached.has_value());
    }});

    tests.push_back({"CacheIntegration_DisabledCache", []() {
        ConfigManager config;
        nlohmann::json cfg = nlohmann::json::object();
        cfg["cache"] = nlohmann::json::object();
        cfg["cache"]["enabled"] = false;
        cfg["response_handler"] = nlohmann::json::object();
        config.loadFromString(cfg.dump());
        
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        ChatRequest request = createTestRequest("model1", "Hello", 0.0f);
        ChatResponse response = createTestResponse("World");

        handler.storeCache(request, response);
        auto cached = handler.checkCache(request);
        CHECK_FALSE(cached.has_value());
    }});

    // ========== 流式响应处理测试 ==========
    tests.push_back({"StreamProcessing_SimpleTextStream", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        std::stringstream stream;
        stream << "data: {\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}\n\n";
        stream << "data: {\"choices\":[{\"delta\":{\"content\":\" \"}}]}\n\n";
        stream << "data: {\"choices\":[{\"delta\":{\"content\":\"world\"}}]}\n\n";
        stream << "data: [DONE]\n\n";

        std::atomic<bool> completed{false};
        std::string fullContent;

        ResponseHandler::StreamCallbacks callbacks;
        callbacks.onTextDelta = [&](std::string_view delta) {
            fullContent += std::string(delta);
        };
        callbacks.onComplete = [&](const ChatResponse& response) {
            completed = true;
            CHECK_EQ(response.content, "Hello world");
        };

        handler.handleStreamResponse(stream, std::move(callbacks));
        CHECK_TRUE(completed.load());
        CHECK_EQ(fullContent, "Hello world");
    }});

    tests.push_back({"StreamProcessing_FinishReason", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        std::stringstream stream;
        stream << "data: {\"choices\":[{\"delta\":{\"content\":\"Hello\"},\"finish_reason\":\"stop\"}]}\n\n";
        stream << "data: [DONE]\n\n";

        std::atomic<bool> completed{false};
        std::optional<std::string> finishReason;

        ResponseHandler::StreamCallbacks callbacks;
        callbacks.onComplete = [&](const ChatResponse& response) {
            completed = true;
            CHECK_TRUE(response.finishReason.has_value());
            CHECK_EQ(*response.finishReason, "stop");
        };

        handler.handleStreamResponse(stream, std::move(callbacks));
        CHECK_TRUE(completed.load());
    }});

    tests.push_back({"StreamProcessing_ToolCallDelta", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        std::stringstream stream;
        stream << "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_1\",\"function\":{\"name\":\"test\",\"arguments\":\"{\\\"arg\\\":\"}}]}}]}\n\n";
        stream << "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"\\\"value\\\"}\"}}]}}]}\n\n";
        stream << "data: [DONE]\n\n";

        std::atomic<bool> completed{false};
        std::atomic<int> toolCallDeltaCount{0};

        ResponseHandler::StreamCallbacks callbacks;
        callbacks.onToolCallDelta = [&](const APIClient::ToolCallDelta& delta) {
            toolCallDeltaCount++;
        };
        callbacks.onComplete = [&](const ChatResponse& response) {
            completed = true;
            CHECK_EQ(response.toolCalls.size(), 1);
            CHECK_EQ(response.toolCalls[0].function.name, "test");
        };

        handler.handleStreamResponse(stream, std::move(callbacks));
        CHECK_TRUE(completed.load());
        CHECK_TRUE(toolCallDeltaCount.load() > 0);
    }});

    tests.push_back({"StreamProcessing_ErrorHandling", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        std::stringstream stream;
        stream << "data: {invalid json}\n\n";

        std::atomic<bool> errorOccurred{false};

        ResponseHandler::StreamCallbacks callbacks;
        callbacks.onError = [&](const ErrorInfo& error) {
            errorOccurred = true;
        };

        handler.handleStreamResponse(stream, std::move(callbacks));
        CHECK_TRUE(errorOccurred.load());
    }});

    // ========== 响应统计测试 ==========
    tests.push_back({"Statistics_InitialState", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        auto stats = handler.getStatistics();
        CHECK_EQ(stats.totalResponses, 0);
        CHECK_EQ(stats.successfulResponses, 0);
        CHECK_EQ(stats.failedResponses, 0);
        CHECK_EQ(stats.cachedResponses, 0);
        CHECK_EQ(stats.streamingResponses, 0);
    }});

    tests.push_back({"Statistics_CacheHitRate", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        ChatRequest request1 = createTestRequest("model1", "Hello", 0.0f);
        ChatRequest request2 = createTestRequest("model1", "World", 0.0f);
        ChatResponse response = createTestResponse("Response");

        // 存储响应
        handler.storeCache(request1, response);

        // 第一次查询（未命中）
        handler.checkCache(request2);
        
        // 第二次查询（命中）
        handler.checkCache(request1);

        auto stats = handler.getStatistics();
        CHECK_EQ(stats.totalResponses, 2);
        CHECK_EQ(stats.cachedResponses, 1);
        
        double hitRate = handler.getCacheHitRate();
        CHECK_TRUE(hitRate > 0.0 && hitRate <= 1.0);
    }});

    tests.push_back({"Statistics_AverageResponseSize", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        ChatRequest request = createTestRequest("model1", "Hello", 0.0f);
        ChatResponse response1 = createTestResponse("Short");
        ChatResponse response2 = createTestResponse("This is a longer response content");

        handler.storeCache(request, response1);
        handler.checkCache(request);
        
        request = createTestRequest("model1", "World", 0.0f);
        handler.storeCache(request, response2);
        handler.checkCache(request);

        auto stats = handler.getStatistics();
        CHECK_TRUE(stats.totalResponses > 0);
        CHECK_TRUE(stats.getAverageResponseSize() > 0);
    }});

    // ========== JSON 格式验证测试 ==========
    tests.push_back({"ValidateJsonFormat_ValidResponseJson", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        // 使用有效的响应格式 JSON
        nlohmann::json validJson = createValidResponseJson();
        CHECK_TRUE(handler.validateResponse(validJson));
    }});

    tests.push_back({"ValidateResponseContent_EmptyContentAllowed", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        ChatResponse response;
        response.content = "";
        response.finishReason = "length";
        // 空内容在某些情况下是允许的（例如达到长度限制）
        CHECK_TRUE(handler.validateResponse(response));
    }});

    tests.push_back({"ValidateResponseContent_ToolCallValidation", []() {
        ConfigManager config;
        createTestConfigManager(config);
        CacheManager cache(config);
        ResponseHandler handler(config, cache);

        ChatResponse response;
        response.content = "";
        ToolCall toolCall;
        toolCall.id = "call_1";
        toolCall.function.name = "test_function";
        toolCall.function.arguments = nlohmann::json::object();
        toolCall.function.arguments["arg1"] = "value1";
        response.toolCalls.push_back(toolCall);

        CHECK_TRUE(handler.validateResponse(response));
    }});

    return mini_test::run(tests);
}

