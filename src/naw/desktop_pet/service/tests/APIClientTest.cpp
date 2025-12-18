#include "naw/desktop_pet/service/APIClient.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/ErrorTypes.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"

#include "httplib.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
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

template <typename T>
std::string toString(const T& v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

inline std::string toString(ErrorType v) {
    using U = std::underlying_type<ErrorType>::type;
    std::ostringstream oss;
    oss << static_cast<U>(v);
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

static void setEnvVar(const std::string& k, const std::string& v) {
#if defined(_WIN32)
    _putenv_s(k.c_str(), v.c_str());
#else
    setenv(k.c_str(), v.c_str(), 1);
#endif
}

static std::string makeLocalBaseUrl(int port) {
    return "http://127.0.0.1:" + std::to_string(port) + "/v1";
}

struct ServerGuard {
    httplib::Server& server;
    std::thread th;
    explicit ServerGuard(httplib::Server& s) : server(s) {}
    ~ServerGuard() {
        server.stop();
        if (th.joinable()) th.join();
    }
};

int main() {
    using mini_test::TestCase;
    std::vector<TestCase> tests;

    tests.push_back({"config_defaults_and_env_override", []() {
        setEnvVar("SILICONFLOW_API_KEY", "test_key_123");

        ConfigManager cm;
        ErrorInfo err;
        CHECK_TRUE(cm.loadFromString(R"({"api":{"base_url":"https://example.invalid/v1","api_key":"${SILICONFLOW_API_KEY}","default_timeout_ms":12345},"models":[]})", &err));
        cm.applyEnvironmentOverrides();

        APIClient api(cm);
        CHECK_EQ(api.getDefaultTimeoutMs(), 12345);
        CHECK_TRUE(api.getApiKeyRedacted().find("test_key_123") == std::string::npos); // 不应明文
    }});

    tests.push_back({"sync_chat_success_parses_content_and_tool_calls", []() {
        setEnvVar("SILICONFLOW_API_KEY", "test_key_123");

        httplib::Server server;
        server.Post("/v1/chat/completions", [](const httplib::Request& req, httplib::Response& res) {
            const auto auth = req.get_header_value("Authorization");
            if (auth != "Bearer test_key_123") {
                res.status = 401;
                res.set_content(R"({"error":{"message":"unauthorized","type":"invalid_request_error","code":"unauthorized"}})",
                                "application/json");
                return;
            }
            res.status = 200;
            res.set_content(
                R"({"model":"m1","choices":[{"index":0,"message":{"role":"assistant","content":"hi","tool_calls":[{"id":"call_1","type":"function","function":{"name":"get_weather","arguments":{"city":"Beijing"}}}]},"finish_reason":"stop"}],"usage":{"prompt_tokens":1,"completion_tokens":2,"total_tokens":3}})",
                "application/json");
        });

        const int port = server.bind_to_any_port("127.0.0.1");
        CHECK_TRUE(port > 0);
        ServerGuard guard(server);
        guard.th = std::thread([&]() { server.listen_after_bind(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        ConfigManager cm;
        ErrorInfo err;
        const auto cfg = nlohmann::json{
            {"api", {{"base_url", makeLocalBaseUrl(port)}, {"api_key", "${SILICONFLOW_API_KEY}"}, {"default_timeout_ms", 30000}}},
            {"models", nlohmann::json::array()},
        };
        CHECK_TRUE(cm.loadFromString(cfg.dump(), &err));
        cm.applyEnvironmentOverrides();

        APIClient api(cm);
        ChatRequest req;
        req.model = "m1";
        req.messages = {ChatMessage{MessageRole::User, "hello"}};

        const auto resp = api.chat(req);
        CHECK_EQ(resp.content, "hi");
        CHECK_EQ(resp.toolCalls.size(), 1u);
        CHECK_EQ(resp.toolCalls[0].id, "call_1");
        CHECK_EQ(resp.toolCalls[0].function.name, "get_weather");
        CHECK_TRUE(resp.toolCalls[0].function.arguments.is_object());
        CHECK_EQ(resp.totalTokens, 3u);
        CHECK_TRUE(resp.model.has_value());
        CHECK_EQ(*resp.model, "m1");
    }});

    tests.push_back({"sync_chat_error_maps_via_errorhandler", []() {
        setEnvVar("SILICONFLOW_API_KEY", "bad_key");

        httplib::Server server;
        server.Post("/v1/chat/completions", [](const httplib::Request&, httplib::Response& res) {
            res.status = 429;
            res.set_content(R"({"error":{"message":"rate limited","type":"rate_limit","code":"rate_limit"}})",
                            "application/json");
        });

        const int port = server.bind_to_any_port("127.0.0.1");
        CHECK_TRUE(port > 0);
        ServerGuard guard(server);
        guard.th = std::thread([&]() { server.listen_after_bind(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        ConfigManager cm;
        ErrorInfo err;
        CHECK_TRUE(cm.loadFromString(
            nlohmann::json{
                {"api", {{"base_url", makeLocalBaseUrl(port)}, {"api_key", "${SILICONFLOW_API_KEY}"}, {"default_timeout_ms", 30000}}},
                {"models", nlohmann::json::array()},
            }
                .dump(),
            &err));
        cm.applyEnvironmentOverrides();

        APIClient api(cm);
        ChatRequest req;
        req.model = "m1";
        req.messages = {ChatMessage{MessageRole::User, "hello"}};

        try {
            (void)api.chat(req);
            CHECK_TRUE(false);
        } catch (const APIClient::ApiClientError& e) {
            CHECK_EQ(e.errorInfo().errorType, ErrorType::RateLimitError);
            CHECK_EQ(e.errorInfo().errorCode, 429);
        }
    }});

    tests.push_back({"sse_stream_aggregates_text_and_tool_calls", []() {
        setEnvVar("SILICONFLOW_API_KEY", "test_key_123");

        httplib::Server server;
        server.Post("/v1/chat/completions", [](const httplib::Request& req, httplib::Response& res) {
            const auto auth = req.get_header_value("Authorization");
            if (auth != "Bearer test_key_123") {
                res.status = 401;
                res.set_content(R"({"error":{"message":"unauthorized"}})", "application/json");
                return;
            }

            res.status = 200;
            res.set_header("Cache-Control", "no-cache");
            res.set_header("Connection", "keep-alive");

            const std::string e1 = "data: {\"model\":\"m1\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Hel\"}}]}\n\n";
            const std::string e2 = "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"lo \"}}]}\n\n";
            const std::string t1 = "data: {\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_1\",\"type\":\"function\",\"function\":{\"name\":\"get_\",\"arguments\":\"{\\\"city\\\":\\\"Bei\"}}]}}]}\n\n";
            const std::string t2 = "data: {\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"name\":\"weather\",\"arguments\":\"jing\\\"}\"}}]}}]}\n\n";
            const std::string done = "data: [DONE]\n\n";

            res.set_chunked_content_provider(
                "text/event-stream",
                [=](size_t /*offset*/, httplib::DataSink& sink) {
                    // 故意切碎：模拟粘包/断包
                    sink.write(e1.data(), 10);
                    sink.write(e1.data() + 10, e1.size() - 10);
                    sink.write(e2.data(), e2.size());
                    sink.write(t1.data(), 25);
                    sink.write(t1.data() + 25, t1.size() - 25);
                    sink.write(t2.data(), t2.size());
                    sink.write(done.data(), done.size());
                    sink.done();
                    return true;
                },
                [](bool) {});
        });

        const int port = server.bind_to_any_port("127.0.0.1");
        CHECK_TRUE(port > 0);
        ServerGuard guard(server);
        guard.th = std::thread([&]() { server.listen_after_bind(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        ConfigManager cm;
        ErrorInfo err;
        CHECK_TRUE(cm.loadFromString(
            nlohmann::json{
                {"api", {{"base_url", makeLocalBaseUrl(port)}, {"api_key", "${SILICONFLOW_API_KEY}"}, {"default_timeout_ms", 30000}}},
                {"models", nlohmann::json::array()},
            }
                .dump(),
            &err));
        cm.applyEnvironmentOverrides();

        APIClient api(cm);
        ChatRequest req;
        req.model = "m1";
        req.messages = {ChatMessage{MessageRole::User, "hello"}};

        std::string seenText;
        std::string seenName;
        std::string seenArgs;
        ChatResponse finalResp;
        bool completed = false;

        APIClient::Callbacks cb;
        cb.onTextDelta = [&](std::string_view d) { seenText.append(d.data(), d.size()); };
        cb.onToolCallDelta = [&](const APIClient::ToolCallDelta& d) {
            seenName += d.nameDelta;
            seenArgs += d.argumentsDelta;
            CHECK_EQ(d.id, "call_1");
            CHECK_EQ(d.index, 0);
        };
        cb.onComplete = [&](const ChatResponse& r) {
            finalResp = r;
            completed = true;
        };
        cb.onError = [&](const ErrorInfo&) { CHECK_TRUE(false); };

        api.chatStream(req, cb);

        CHECK_TRUE(completed);
        CHECK_EQ(seenText, "Hello ");
        CHECK_EQ(finalResp.content, "Hello ");
        CHECK_EQ(seenName, "get_weather");
        CHECK_TRUE(seenArgs.find("Beijing") != std::string::npos);
        CHECK_EQ(finalResp.toolCalls.size(), 1u);
        CHECK_EQ(finalResp.toolCalls[0].id, "call_1");
        CHECK_EQ(finalResp.toolCalls[0].function.name, "get_weather");
        CHECK_TRUE(finalResp.toolCalls[0].function.arguments.is_object() ||
                   finalResp.toolCalls[0].function.arguments.is_string());
    }});

    return mini_test::run(tests);
}

