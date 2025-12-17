#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>
#include <variant>

#include "naw/desktop_pet/service/utils/HttpClient.h"

namespace naw::desktop_pet::service::utils {

struct RemoteTokenUsage {
    std::string model;
    std::uint64_t promptTokens{0};
    std::uint64_t completionTokens{0};
    std::uint64_t totalTokens{0};
    std::string currency; // e.g. "USD" if provided
};

struct TokenUsageQuery {
    std::optional<std::string> model;
    std::optional<std::chrono::system_clock::time_point> startTime;
    std::optional<std::chrono::system_clock::time_point> endTime;
    int timeoutMs{10000};
};

class TokenUsageClient {
public:
    explicit TokenUsageClient(std::string baseUrl, std::string apiKey);

    /**
     * @brief 查询账户Token用量（按时间窗口/模型过滤）
     * @return 成功返回用量列表；失败返回错误字符串
     */
    std::variant<std::vector<RemoteTokenUsage>, std::string>
    queryUsage(const TokenUsageQuery& query);

private:
    std::string baseUrl_;
    std::string apiKey_;
    HttpClient http_;

    HttpRequest buildRequest(const TokenUsageQuery& query) const;
    std::variant<std::vector<RemoteTokenUsage>, std::string>
    parseResponse(const HttpResponse& resp) const;
};

} // namespace naw::desktop_pet::service::utils

