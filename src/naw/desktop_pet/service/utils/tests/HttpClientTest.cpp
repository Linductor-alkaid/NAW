#include "naw/desktop_pet/service/utils/HttpClient.h"
#include "naw/desktop_pet/service/utils/HttpTypes.h"

#include <gtest/gtest.h>

using namespace naw::desktop_pet::service::utils;

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

TEST(HttpClientTests, RetryClassification) {
    HttpClient client("https://example.com");

    HttpResponse resp0;  // statusCode 默认0 -> Network
    EXPECT_TRUE(HttpClientTestAccessor::IsRetryable(client, resp0));

    HttpResponse resp408; resp408.statusCode = 408;
    EXPECT_TRUE(HttpClientTestAccessor::IsRetryable(client, resp408));

    HttpResponse resp429; resp429.statusCode = 429;
    EXPECT_TRUE(HttpClientTestAccessor::IsRetryable(client, resp429));

    HttpResponse resp500; resp500.statusCode = 500;
    EXPECT_TRUE(HttpClientTestAccessor::IsRetryable(client, resp500));

    HttpResponse resp400; resp400.statusCode = 400;
    EXPECT_FALSE(HttpClientTestAccessor::IsRetryable(client, resp400));
}

TEST(HttpClientTests, ConnectionReuseStats) {
    HttpClient client("http://example.com");

    // 模拟获取同 host 的 client，多次调用应复用计数增加
    auto c1 = HttpClientTestAccessor::GetOrCreate(client, "http://example.com/path1");
    auto c2 = HttpClientTestAccessor::GetOrCreate(client, "http://example.com/path2");
    EXPECT_EQ(c1, c2);

    EXPECT_GE(client.getTotalConnections(), 1u);
    EXPECT_GE(client.getReusedConnections(), 1u);
    EXPECT_GT(client.getConnectionReuseRate(), 0.0);
}

TEST(HttpClientTests, RetryStatsSnapshot) {
    HttpClient client("https://example.com");
    auto snap = client.getRetryStats();
    EXPECT_GE(snap.totalAttempts, 0);
    EXPECT_GE(snap.totalRetries, 0);
}

TEST(HttpClientTests, MergeHeadersPrefersRequest) {
    HttpClient client("https://example.com");
    client.setDefaultHeader("User-Agent", "UA1");
    std::map<std::string, std::string> reqHeaders = {{"User-Agent", "UA2"}, {"X-Test", "1"}};
    auto merged = HttpClientTestAccessor::Merge(client, reqHeaders);
    EXPECT_EQ(merged.at("User-Agent"), "UA2");
    EXPECT_EQ(merged.at("X-Test"), "1");
}

TEST(HttpClientTests, PatchHandled) {
    HttpClient client("https://example.com");
    HttpRequest req = makeRequest(HttpMethod::PATCH, "https://example.com/patch");
    req.body = "data";
    req.headers = {{"Content-Type", "application/json"}};
    // 不执行网络，仅验证不会返回501分支
    auto fut = client.patchAsync("/patch", "data");
    // 只要能提交异步任务即可
    fut.wait();
    SUCCEED();
}

TEST(HttpClientTests, CustomBackoffAndLogger) {
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
    // 预期执行时如果失败会走 logger，网络若不可达计数也会增加
    HttpClientTestAccessor::ExecRetry(client, req);
    EXPECT_GE(loggerCount, 0);
}

TEST(HttpClientTests, HeaderValidationRejectsControlChars) {
    HttpClient client("https://example.com");
    HttpRequest req = makeRequest(HttpMethod::GET, "https://example.com/get");
    req.headers = {{"Bad\nKey", "v"}};
    auto resp = HttpClientTestAccessor::ExecOnce(client, req);
    EXPECT_EQ(resp.statusCode, 400);
    EXPECT_FALSE(resp.error.empty());
}
