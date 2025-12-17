#include "naw/desktop_pet/service/utils/TokenUsageClient.h"

#include <iomanip>
#include <sstream>
#include <variant>

#include "naw/desktop_pet/service/utils/HttpSerialization.h"

namespace naw::desktop_pet::service::utils {

static std::string toIso8601(const std::chrono::system_clock::time_point& tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

TokenUsageClient::TokenUsageClient(std::string baseUrl, std::string apiKey)
    : baseUrl_(std::move(baseUrl)), apiKey_(std::move(apiKey)) {}

HttpRequest TokenUsageClient::buildRequest(const TokenUsageQuery& query) const {
    HttpRequest req;
    req.method = HttpMethod::GET;
    req.url = baseUrl_ + "/v1/billing/usage";
    req.timeoutMs = query.timeoutMs;
    req.setHeader("Authorization", "Bearer " + apiKey_);
    req.setHeader("Content-Type", "application/json");

    if (query.model) {
        req.setParam("model", *query.model);
    }
    if (query.startTime) {
        req.setParam("start_time", toIso8601(*query.startTime));
    }
    if (query.endTime) {
        req.setParam("end_time", toIso8601(*query.endTime));
    }
    return req;
}

std::variant<std::vector<RemoteTokenUsage>, std::string>
TokenUsageClient::parseResponse(const HttpResponse& resp) const {
    if (!resp.isSuccess()) {
        std::ostringstream err;
        err << "HTTP " << resp.statusCode << ": " << resp.body;
        return err.str();
    }

    auto jsonOpt = resp.asJson();
    if (!jsonOpt) {
        return std::string("Failed to parse JSON response");
    }
    const auto& j = *jsonOpt;

    std::vector<RemoteTokenUsage> usages;
    try {
        if (j.contains("data") && j.at("data").is_array()) {
            for (const auto& item : j.at("data")) {
                RemoteTokenUsage u;
                if (item.contains("model") && item.at("model").is_string())
                    u.model = item.at("model").get<std::string>();
                if (item.contains("prompt_tokens") && item.at("prompt_tokens").is_number_unsigned())
                    u.promptTokens = item.at("prompt_tokens").get<std::uint64_t>();
                if (item.contains("completion_tokens") && item.at("completion_tokens").is_number_unsigned())
                    u.completionTokens = item.at("completion_tokens").get<std::uint64_t>();
                if (item.contains("total_tokens") && item.at("total_tokens").is_number_unsigned())
                    u.totalTokens = item.at("total_tokens").get<std::uint64_t>();
                if (item.contains("currency") && item.at("currency").is_string())
                    u.currency = item.at("currency").get<std::string>();
                usages.push_back(std::move(u));
            }
        }
        return usages;
    } catch (const std::exception& e) {
        return std::string("Parse error: ") + e.what();
    }
}

std::variant<std::vector<RemoteTokenUsage>, std::string>
TokenUsageClient::queryUsage(const TokenUsageQuery& query) {
    auto req = buildRequest(query);
    auto resp = http_.execute(req);
    return parseResponse(resp);
}

} // namespace naw::desktop_pet::service::utils

