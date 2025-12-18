#include "naw/desktop_pet/service/ErrorHandler.h"
#include "naw/desktop_pet/service/ErrorTypes.h"
#include "naw/desktop_pet/service/utils/HttpTypes.h"

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace naw::desktop_pet::service;
using namespace naw::desktop_pet::service::utils;

// 轻量自测断言工具（与 utils/tests 风格保持一致）
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

// MSVC 在较老标准下对 SFINAE 模板的解析比较挑剔，这里直接为项目内枚举提供重载即可
inline std::string toString(ErrorType v) {
    typedef std::underlying_type<ErrorType>::type U;
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

static HttpResponse makeResp(int statusCode, const std::string& body = "", const std::string& err = "") {
    HttpResponse r;
    r.statusCode = statusCode;
    r.body = body;
    r.error = err;
    // 让 isJson() 可用
    if (!body.empty()) {
        r.multiHeaders.add("Content-Type", "application/json");
        r.headers = r.multiHeaders.toFirstValueMap();
    }
    return r;
}

int main() {
    using mini_test::TestCase;

    std::vector<TestCase> tests;

    tests.push_back({"status_to_error_type", []() {
        CHECK_EQ(ErrorHandler::mapHttpStatusToErrorType(0, "Request failed"), ErrorType::NetworkError);
        CHECK_EQ(ErrorHandler::mapHttpStatusToErrorType(0, "timeout"), ErrorType::TimeoutError);
        CHECK_EQ(ErrorHandler::mapHttpStatusToErrorType(408), ErrorType::TimeoutError);
        CHECK_EQ(ErrorHandler::mapHttpStatusToErrorType(429), ErrorType::RateLimitError);
        CHECK_EQ(ErrorHandler::mapHttpStatusToErrorType(400), ErrorType::InvalidRequest);
        CHECK_EQ(ErrorHandler::mapHttpStatusToErrorType(401), ErrorType::InvalidRequest);
        CHECK_EQ(ErrorHandler::mapHttpStatusToErrorType(403), ErrorType::InvalidRequest);
        CHECK_EQ(ErrorHandler::mapHttpStatusToErrorType(500), ErrorType::ServerError);
        CHECK_EQ(ErrorHandler::mapHttpStatusToErrorType(503), ErrorType::ServerError);
    }});

    tests.push_back({"parse_api_error_json", []() {
        const auto j = nlohmann::json::parse(R"({"error":{"message":"bad","type":"invalid_request_error","code":"foo"}})");
        auto info = ErrorHandler::parseApiErrorJson(j, 400);
        CHECK_TRUE(info.has_value());
        CHECK_EQ(info->errorCode, 400);
        CHECK_EQ(info->errorType, ErrorType::InvalidRequest);
        CHECK_TRUE(info->details.has_value());
        CHECK_TRUE(info->toJson().contains("message"));
    }});

    tests.push_back({"from_http_response_prefers_api_message", []() {
        auto resp = makeResp(429, R"({"error":{"message":"rate limited","type":"rate_limit","code":"rate_limit"}})");
        auto info = ErrorHandler::fromHttpResponse(resp);
        CHECK_EQ(info.errorType, ErrorType::RateLimitError);
        CHECK_TRUE(info.message.find("rate limited") != std::string::npos);
    }});

    tests.push_back({"retry_after_seconds_parse", []() {
        auto sec = ErrorHandler::parseRetryAfterSeconds("120");
        CHECK_TRUE(sec.has_value());
        CHECK_EQ(*sec, 120U);
        CHECK_FALSE(ErrorHandler::parseRetryAfterSeconds("abc").has_value());
    }});

    tests.push_back({"should_retry_caps", []() {
        ErrorHandler h;
        ErrorInfo e;

        e.errorType = ErrorType::InvalidRequest;
        CHECK_FALSE(h.shouldRetry(e, 0));

        e.errorType = ErrorType::TimeoutError;
        CHECK_TRUE(h.shouldRetry(e, 0));
        CHECK_TRUE(h.shouldRetry(e, 1));
        CHECK_FALSE(h.shouldRetry(e, 2));

        e.errorType = ErrorType::NetworkError;
        CHECK_TRUE(h.shouldRetry(e, 0));
        CHECK_TRUE(h.shouldRetry(e, 2));
        CHECK_FALSE(h.shouldRetry(e, 3));

        e.errorType = ErrorType::RateLimitError;
        CHECK_TRUE(h.shouldRetry(e, 0));
        CHECK_TRUE(h.shouldRetry(e, 4));
        // 5 次上限：attempt=5 不应再重试
        CHECK_FALSE(h.shouldRetry(e, 5));
    }});

    tests.push_back({"retry_delay_uses_retry_after_header", []() {
        ErrorHandler h;
        ErrorInfo e;
        e.errorType = ErrorType::RateLimitError;

        HttpResponse resp = makeResp(429, R"({"error":{"message":"rate limited"}})");
        resp.multiHeaders.add("Retry-After", "2");
        resp.headers = resp.multiHeaders.toFirstValueMap();

        const auto delayMs = h.getRetryDelayMs(e, 0, std::optional<HttpResponse>{resp});
        CHECK_EQ(delayMs, 2000U);
    }});

    tests.push_back({"retry_after_header_has_priority_over_policy_backoff", []() {
        auto p = ErrorHandler::RetryPolicy::makeDefault();
        p.initialDelayMs = 5000;
        p.enableJitter = false;
        ErrorHandler h(p);

        ErrorInfo e;
        e.errorType = ErrorType::RateLimitError;

        HttpResponse resp = makeResp(429, R"({"error":{"message":"rate limited"}})");
        resp.multiHeaders.add("Retry-After", "2");
        resp.headers = resp.multiHeaders.toFirstValueMap();

        const auto delayMs = h.getRetryDelayMs(e, 3, std::optional<HttpResponse>{resp});
        CHECK_EQ(delayMs, 2000U);
    }});

    return mini_test::run(tests);
}

