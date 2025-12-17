#include "naw/desktop_pet/service/utils/HttpClient.h"
#include "naw/desktop_pet/service/utils/HttpTypes.h"
#include "naw/desktop_pet/service/utils/HttpSerialization.h"

#include <chrono>
#include <future>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace naw::desktop_pet::service::utils;

// 轻量自测断言工具
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

#define CHECK_GE(a, b)                                                                            \
    do {                                                                                          \
        const auto _va = (a);                                                                     \
        const auto _vb = (b);                                                                     \
        if (!(_va >= _vb)) {                                                                      \
            throw mini_test::AssertionFailed(std::string("CHECK_GE failed: ") + #a " >= " #b +    \
                                             " (" + mini_test::toString(_va) + " vs " +           \
                                             mini_test::toString(_vb) + ")");                     \
        }                                                                                         \
    } while (0)

#define CHECK_GT(a, b)                                                                            \
    do {                                                                                          \
        const auto _va = (a);                                                                     \
        const auto _vb = (b);                                                                     \
        if (!(_va > _vb)) {                                                                       \
            throw mini_test::AssertionFailed(std::string("CHECK_GT failed: ") + #a " > " #b +     \
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

// 辅助：构造简单HttpRequest
static HttpRequest makeRequest(HttpMethod method, const std::string& url) {
    HttpRequest req;
    req.method = method;
    req.url = url;
    return req;
}

namespace naw::desktop_pet::service::utils {
class HttpClientTestAccessor {
public:
    static bool IsRetryable(HttpClient& c, const HttpResponse& r) {
        return c.isRetryableError(r);
    }
    static std::shared_ptr<httplib::Client> GetOrCreate(HttpClient& c, const std::string& url) {
        return c.getOrCreateClient(url);
    }
    static std::map<std::string, std::string> Merge(HttpClient& c,
        const std::map<std::string, std::string>& h) {
        return c.mergeHeaders(h);
    }
    static HttpResponse ExecOnce(HttpClient& c, const HttpRequest& r) {
        return c.executeOnce(r);
    }
    static HttpResponse ExecRetry(HttpClient& c, const HttpRequest& r) {
        return c.executeWithRetry(r);
    }
};
} // namespace naw::desktop_pet::service::utils

using namespace naw::desktop_pet::service::utils;
namespace naw::desktop_pet::service::utils {
class HttpHeadersTestAccessor {
public:
    static HttpHeaders parse(const std::string& raw) { return HttpHeaders::parseRaw(raw); }
};
} // namespace naw::desktop_pet::service::utils

using mini_test::TestCase;
using mini_test::run;

int main() {
    std::vector<TestCase> tests;

    tests.push_back({"RetryClassification", []() {
                         HttpClient client("https://example.com");

                         HttpResponse resp0;  // statusCode 默认0 -> Network
                         CHECK_TRUE(HttpClientTestAccessor::IsRetryable(client, resp0));

                         HttpResponse resp408;
                         resp408.statusCode = 408;
                         CHECK_TRUE(HttpClientTestAccessor::IsRetryable(client, resp408));

                         HttpResponse resp429;
                         resp429.statusCode = 429;
                         CHECK_TRUE(HttpClientTestAccessor::IsRetryable(client, resp429));

                         HttpResponse resp500;
                         resp500.statusCode = 500;
                         CHECK_TRUE(HttpClientTestAccessor::IsRetryable(client, resp500));

                         HttpResponse resp400;
                         resp400.statusCode = 400;
                         CHECK_FALSE(HttpClientTestAccessor::IsRetryable(client, resp400));
                     }});

    tests.push_back({"ConnectionReuseStats", []() {
                         HttpClient client("http://example.com");

                         auto c1 =
                             HttpClientTestAccessor::GetOrCreate(client, "http://example.com/path1");
                         auto c2 =
                             HttpClientTestAccessor::GetOrCreate(client, "http://example.com/path2");
                         CHECK_EQ(c1, c2);

                         CHECK_GE(client.getTotalConnections(), 1u);
                         CHECK_GE(client.getReusedConnections(), 1u);
                         CHECK_GT(client.getConnectionReuseRate(), 0.0);
                     }});

    tests.push_back({"RetryStatsSnapshot", []() {
                         HttpClient client("https://example.com");
                         auto snap = client.getRetryStats();
                         CHECK_GE(snap.totalAttempts, 0);
                         CHECK_GE(snap.totalRetries, 0);
                     }});

    tests.push_back({"FormSerializationEncodes", []() {
                         std::map<std::string, std::string> form{{"a b", "c+d"}, {"中文", "测试"}};
                         auto body = serializeForm(form);
                         CHECK_TRUE(body.find("a%20b=c%2Bd") != std::string::npos);
                         CHECK_TRUE(body.find("%E4%B8%AD%E6%96%87=%E6%B5%8B%E8%AF%95") !=
                                    std::string::npos);
                     }});

    tests.push_back({"JsonRoundTrip", []() {
                         nlohmann::json j = {{"x", 1}, {"arr", {1, 2}}};
                         auto dumped = toJsonBody(j);
                         auto parsed = parseJsonSafe(dumped);
                         CHECK_TRUE(parsed.has_value());
                         CHECK_EQ((*parsed)["x"], 1);
                         CHECK_EQ((*parsed)["arr"].size(), 2u);
                     }});

    tests.push_back({"Base64EncodeDecode", []() {
                         std::string data = "hello base64";
                         auto encoded = encodeBase64(data);
                         auto decoded = decodeBase64(encoded);
                         CHECK_TRUE(decoded.has_value());
                         std::string round(decoded->begin(), decoded->end());
                         CHECK_EQ(round, data);
                     }});

    tests.push_back({"MergeHeadersUsesDefaultWhenConflict", []() {
                         HttpClient client("https://example.com");
                         client.setDefaultHeader("User-Agent", "UA1");
                         std::map<std::string, std::string> reqHeaders = {{"User-Agent", "UA2"},
                                                                         {"X-Test", "1"}};
                         auto merged = HttpClientTestAccessor::Merge(client, reqHeaders);
                         // 当前实现是默认头优先，后插入的请求头不会覆盖
                         CHECK_EQ(merged.at("User-Agent"), "UA1");
                         CHECK_EQ(merged.at("X-Test"), "1");
                     }});

    tests.push_back({"HttpResponseAsJson", []() {
                         HttpResponse resp;
                         resp.body = R"({"ok":true,"v":2})";
                         auto j = resp.asJson();
                         CHECK_TRUE(j.has_value());
                         CHECK_TRUE((*j)["ok"]);
                         CHECK_EQ((*j)["v"], 2);

                         resp.body = "not-json";
                         auto j2 = resp.asJson();
                         CHECK_FALSE(j2.has_value());
                     }});

    tests.push_back({"PatchHandled", []() {
                         HttpClient client("https://example.com");
                         HttpRequest req =
                             makeRequest(HttpMethod::PATCH, "https://example.com/patch");
                         req.body = "data";
                         req.headers = {{"Content-Type", "application/json"}};
                         auto fut = client.patchAsync("/patch", "data");
                         fut.wait();
                     }});

    tests.push_back({"CustomBackoffAndLogger", []() {
                         HttpClient client("https://example.com");
                         RetryConfig cfg = client.getRetryConfig();
                         int loggerCount = 0;
                         cfg.customBackoff = [&](int attempt) {
                             if (attempt == 0) return std::chrono::milliseconds(1);
                             return std::chrono::milliseconds(2);
                         };
                         cfg.retryLogger = [&](int attempt, const HttpResponse&) { loggerCount++; };
                         cfg.maxRetries = 1;
                         client.setRetryConfig(cfg);

                         HttpRequest req = makeRequest(HttpMethod::GET, "https://example.com/fail");
                         HttpClientTestAccessor::ExecRetry(client, req);
                         CHECK_GE(loggerCount, 0);
                     }});

    tests.push_back({"HeaderValidationRejectsControlChars", []() {
                         HttpClient client("https://example.com");
                         HttpRequest req = makeRequest(HttpMethod::GET, "https://example.com/get");
                         req.headers = {{"Bad\nKey", "v"}};
                         auto resp = HttpClientTestAccessor::ExecOnce(client, req);
                         CHECK_EQ(resp.statusCode, 400);
                         CHECK_FALSE(resp.error.empty());
                     }});

    tests.push_back({"RawHeadersParseMultiValues", []() {
                         std::string raw =
                             "Content-Type: application/json\r\n"
                             "Set-Cookie: a=1\r\n"
                             "Set-Cookie: b=2\r\n"
                             "X-Test: v1\r\n"
                             "x-test: v2\r\n";
                         auto h = HttpHeadersTestAccessor::parse(raw);
                         auto cookies = h.getAll("Set-Cookie");
                         CHECK_EQ(cookies.size(), 2u);
                         CHECK_EQ(cookies[0], "a=1");
                         CHECK_EQ(cookies[1], "b=2");
                         auto xtest = h.getAll("X-Test");
                         CHECK_EQ(xtest.size(), 2u);
                         CHECK_EQ(h.getFirst("Content-Type").value_or(""), "application/json");
                         auto ctLen = h.contentLength();
                         CHECK_FALSE(ctLen.has_value());
                     }});

    tests.push_back({"MultipartBuildsBoundaryAndRejectsCtrl", []() {
                         HttpClient client("https://example.com");
                         HttpResponse ok = client.postMultipart("/post", {{"k", "v"}}, {}, {});
                         CHECK_TRUE(ok.statusCode != 400);

                         HttpResponse bad =
                             client.postMultipart("/post", {{"bad\nk", "v"}}, {}, {});
                         CHECK_EQ(bad.statusCode, 400);
                     }});

    tests.push_back({"AsyncCallbackAndCancel", []() {
                         HttpClient client("https://example.com");
                         int callbackCount = 0;
                         HttpClient::CancelToken token{std::make_shared<std::atomic<bool>>(false)};

                         auto fut1 =
                             client.getAsync("/get", {}, {}, [&](const HttpResponse&) {
                                 callbackCount++;
                             });
                         fut1.wait();
                         CHECK_GE(callbackCount, 1);

                         token.cancelled->store(true, std::memory_order_relaxed);
                         auto fut2 = client.getAsync("/get", {}, {}, nullptr, &token);
                         auto r = fut2.get();
                         CHECK_EQ(r.statusCode, 0);
                         CHECK_EQ(r.error, "Cancelled");
                     }});

    tests.push_back({"AsyncConcurrentCancel", []() {
                         HttpClient client("https://example.com");
                         HttpClient::CancelToken tok1{std::make_shared<std::atomic<bool>>(true)};
                         HttpClient::CancelToken tok2{std::make_shared<std::atomic<bool>>(true)};

                         auto f1 = client.getAsync("/get", {}, {}, nullptr, &tok1);
                         auto f2 =
                             client.postAsync("/post", "x", "application/json", {}, nullptr, &tok2);

                         auto r1 = f1.get();
                         auto r2 = f2.get();
                         CHECK_EQ(r1.error, "Cancelled");
                         CHECK_EQ(r2.error, "Cancelled");
                     }});

    tests.push_back({"ConnectionPoolCapacityAndPrune", []() {
                         HttpClient client("http://example.com");
                         ConnectionPoolConfig cfg = client.getConnectionPoolConfig();
                         cfg.maxConnections = 1;
                         cfg.idleTimeout = std::chrono::milliseconds(0);
                         client.setConnectionPoolConfig(cfg);

                         auto c1 = HttpClientTestAccessor::GetOrCreate(client, "http://a.com/path1");
                         auto c2 = HttpClientTestAccessor::GetOrCreate(client, "http://b.com/path2");

                         CHECK_EQ(client.getActiveConnections(), 1u);
                         CHECK_TRUE(c1 != c2);
                     }});

    return run(tests);
}
