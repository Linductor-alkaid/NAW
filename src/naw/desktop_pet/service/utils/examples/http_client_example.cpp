#include "naw/desktop_pet/service/utils/HttpClient.h"
#include <iostream>

using namespace naw::desktop_pet::service::utils;

int main() {
    const std::string baseUrl = "https://postman-echo.com";
    HttpClient client(baseUrl);

    // 同步 GET
    auto resp = client.get("/get", {{"hello", "world"}});
    std::cout << "[Sync GET] status=" << resp.statusCode
              << " error=" << resp.error << "\n";

    // 异步 GET
    auto fut = client.getAsync("/get", {{"q", "async"}});
    auto asyncResp = fut.get();
    std::cout << "[Async GET] status=" << asyncResp.statusCode
              << " error=" << asyncResp.error << "\n";

    // 异步 POST
    auto futPost = client.postAsync("/post", R"({"ping":true})");
    auto postResp = futPost.get();
    std::cout << "[Async POST] status=" << postResp.statusCode
              << " error=" << postResp.error << "\n";

    // 表单 POST
    std::map<std::string, std::string> formFields{{"foo", "bar"}, {"lang", "cpp"}};
    auto formResp = client.postForm("/post", formFields);
    std::cout << "[Form POST] status=" << formResp.statusCode
              << " error=" << formResp.error << "\n";
    if (!formResp.body.empty()) {
        std::cout << "[Form Body snippet] " << formResp.body.substr(0, 120) << "...\n";
    }

    // 统计
    std::cout << "Active connections: " << client.getActiveConnections() << "\n";
    std::cout << "Total connections: " << client.getTotalConnections() << "\n";
    std::cout << "Reuse rate: " << client.getConnectionReuseRate() << "\n";

    auto stats = client.getRetryStats();
    std::cout << "Retry attempts=" << stats.totalAttempts
              << " retries=" << stats.totalRetries
              << " successAfterRetry=" << stats.totalSuccessAfterRetry << "\n";

    if (!resp.body.empty()) {
        std::cout << "[Body snippet] " << resp.body.substr(0, 120) << "...\n";
    }
    return 0;
}
