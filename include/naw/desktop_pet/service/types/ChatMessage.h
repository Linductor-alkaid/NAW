#pragma once

#include "naw/desktop_pet/service/utils/TokenCounter.h"
#include "naw/desktop_pet/service/utils/HttpSerialization.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "nlohmann/json.hpp"

namespace naw::desktop_pet::service::types {

enum class MessageRole {
    System,
    User,
    Assistant,
    Tool
};

inline std::string roleToString(MessageRole r) {
    switch (r) {
        case MessageRole::System: return "system";
        case MessageRole::User: return "user";
        case MessageRole::Assistant: return "assistant";
        case MessageRole::Tool: return "tool";
    }
    return "user";
}

inline std::optional<MessageRole> stringToRole(std::string_view s) {
    auto lowerEq = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            char ca = a[i];
            char cb = b[i];
            if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
            if (ca != cb) return false;
        }
        return true;
    };

    if (lowerEq(s, "system")) return MessageRole::System;
    if (lowerEq(s, "user")) return MessageRole::User;
    if (lowerEq(s, "assistant")) return MessageRole::Assistant;
    if (lowerEq(s, "tool")) return MessageRole::Tool;
    return std::nullopt;
}

struct ImageUrlContent {
    std::string url;
    std::optional<std::string> detail; // "low"|"high"|"auto" (optional)
};

struct ContentPartText {
    std::string text;
};

struct ContentPartImageUrl {
    ImageUrlContent imageUrl;
};

using MessageContentPart = std::variant<ContentPartText, ContentPartImageUrl>;
using MessageContent = std::variant<std::string, std::vector<MessageContentPart>>;

struct ChatMessage {
    MessageRole role{MessageRole::User};
    MessageContent content{std::string{}};
    std::optional<std::string> name;       // tool name (role=tool) or assistant name
    std::optional<std::string> toolCallId; // tool_call_id

    ChatMessage() = default;
    ChatMessage(MessageRole r, std::string text) : role(r), content(std::move(text)) {}

    bool isText() const { return std::holds_alternative<std::string>(content); }

    std::optional<std::string_view> textView() const {
        if (!std::holds_alternative<std::string>(content)) return std::nullopt;
        return std::string_view{std::get<std::string>(content)};
    }

    void setText(std::string text) { content = std::move(text); }

    void appendText(std::string text) {
        if (std::holds_alternative<std::string>(content)) {
            std::get<std::string>(content) += text;
            return;
        }
        auto& parts = std::get<std::vector<MessageContentPart>>(content);
        parts.push_back(ContentPartText{std::move(text)});
    }

    void appendImageUrl(std::string url, std::optional<std::string> detail = std::nullopt) {
        if (std::holds_alternative<std::string>(content)) {
            // Upgrade string content to parts: keep existing text as first part if non-empty.
            std::string existing = std::move(std::get<std::string>(content));
            std::vector<MessageContentPart> parts;
            if (!existing.empty()) parts.push_back(ContentPartText{std::move(existing)});
            parts.push_back(ContentPartImageUrl{ImageUrlContent{std::move(url), std::move(detail)}});
            content = std::move(parts);
            return;
        }
        auto& parts = std::get<std::vector<MessageContentPart>>(content);
        parts.push_back(ContentPartImageUrl{ImageUrlContent{std::move(url), std::move(detail)}});
    }

    std::string dumpContent() const {
        if (auto tv = textView(); tv.has_value()) return std::string(*tv);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& p : std::get<std::vector<MessageContentPart>>(content)) {
            if (std::holds_alternative<ContentPartText>(p)) {
                arr.push_back({{"type", "text"}, {"text", std::get<ContentPartText>(p).text}});
            } else {
                const auto& img = std::get<ContentPartImageUrl>(p).imageUrl;
                nlohmann::json ij;
                ij["url"] = img.url;
                if (img.detail.has_value()) ij["detail"] = *img.detail;
                arr.push_back({{"type", "image_url"}, {"image_url", std::move(ij)}});
            }
        }
        return arr.dump();
    }

    static bool validateImageUrl(const std::string& url, std::string* reason = nullptr) {
        auto startsWith = [](std::string_view s, std::string_view p) {
            return s.size() >= p.size() && s.substr(0, p.size()) == p;
        };

        if (url.empty()) {
            if (reason) *reason = "image_url.url is empty";
            return false;
        }

        std::string_view u(url);
        if (startsWith(u, "http://") || startsWith(u, "https://")) {
            if (reason) reason->clear();
            return true;
        }

        // data URL: data:image/<fmt>;base64,<payload>
        if (!startsWith(u, "data:image/")) {
            if (reason) *reason = "unsupported url scheme (expect http/https/data:image)";
            return false;
        }

        const auto commaPos = u.find(";base64,");
        if (commaPos == std::string_view::npos) {
            if (reason) *reason = "data url missing ';base64,'";
            return false;
        }

        std::string_view fmt = u.substr(std::string_view("data:image/").size(),
                                        commaPos - std::string_view("data:image/").size());
        auto lower = [](std::string_view s) {
            std::string out;
            out.reserve(s.size());
            for (char c : s) {
                if (c >= 'A' && c <= 'Z') out.push_back(static_cast<char>(c - 'A' + 'a'));
                else out.push_back(c);
            }
            return out;
        };
        const std::string fmtLower = lower(fmt);
        if (!(fmtLower == "png" || fmtLower == "jpeg" || fmtLower == "jpg" || fmtLower == "webp")) {
            if (reason) *reason = "unsupported image format in data url";
            return false;
        }

        std::string payload(u.substr(commaPos + std::string_view(";base64,").size()));
        auto decoded = naw::desktop_pet::service::utils::decodeBase64(payload);
        if (!decoded.has_value()) {
            if (reason) *reason = "base64 decode failed";
            return false;
        }
        if (decoded->empty()) {
            if (reason) *reason = "decoded image bytes are empty";
            return false;
        }
        constexpr std::size_t kMaxBytes = 5 * 1024 * 1024;
        if (decoded->size() > kMaxBytes) {
            if (reason) *reason = "image bytes exceed limit";
            return false;
        }
        if (reason) reason->clear();
        return true;
    }

    // Compatible with snake_case / camelCase input; toJson outputs snake_case.
    static std::optional<ChatMessage> fromJson(const nlohmann::json& j) {
        if (!j.is_object()) return std::nullopt;

        auto readStr = [&](const char* snake, const char* camel) -> std::optional<std::string> {
            if (j.contains(snake) && j[snake].is_string()) return j[snake].get<std::string>();
            if (camel && j.contains(camel) && j[camel].is_string()) return j[camel].get<std::string>();
            return std::nullopt;
        };

        ChatMessage m;

        auto roleStr = readStr("role", "role");
        if (!roleStr.has_value()) return std::nullopt;
        auto role = stringToRole(*roleStr);
        if (!role.has_value()) return std::nullopt;
        m.role = *role;

        if (!j.contains("content")) return std::nullopt;
        if (j["content"].is_string()) {
            m.content = j["content"].get<std::string>();
        } else if (j["content"].is_array()) {
            std::vector<MessageContentPart> parts;
            for (const auto& item : j["content"]) {
                if (!item.is_object()) return std::nullopt;
                if (!item.contains("type") || !item["type"].is_string()) return std::nullopt;
                const std::string type = item["type"].get<std::string>();

                if (type == "text") {
                    if (!item.contains("text") || !item["text"].is_string()) return std::nullopt;
                    parts.push_back(ContentPartText{item["text"].get<std::string>()});
                    continue;
                }
                if (type == "image_url") {
                    if (!item.contains("image_url") || !item["image_url"].is_object()) return std::nullopt;
                    const auto& ij = item["image_url"];
                    if (!ij.contains("url") || !ij["url"].is_string()) return std::nullopt;
                    ImageUrlContent img;
                    img.url = ij["url"].get<std::string>();
                    if (ij.contains("detail") && ij["detail"].is_string()) img.detail = ij["detail"].get<std::string>();
                    parts.push_back(ContentPartImageUrl{std::move(img)});
                    continue;
                }
                // Unknown part type: fail to avoid silently dropping info.
                return std::nullopt;
            }
            m.content = std::move(parts);
        } else {
            // Not supported: object/number/etc
            return std::nullopt;
        }

        if (auto n = readStr("name", "name"); n.has_value()) m.name = *n;
        if (auto id = readStr("tool_call_id", "toolCallId"); id.has_value()) m.toolCallId = *id;

        return m;
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["role"] = roleToString(role);
        if (std::holds_alternative<std::string>(content)) {
            j["content"] = std::get<std::string>(content);
        } else {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& p : std::get<std::vector<MessageContentPart>>(content)) {
                if (std::holds_alternative<ContentPartText>(p)) {
                    arr.push_back({{"type", "text"}, {"text", std::get<ContentPartText>(p).text}});
                } else {
                    const auto& img = std::get<ContentPartImageUrl>(p).imageUrl;
                    nlohmann::json ij;
                    ij["url"] = img.url;
                    if (img.detail.has_value()) ij["detail"] = *img.detail;
                    arr.push_back({{"type", "image_url"}, {"image_url", std::move(ij)}});
                }
            }
            j["content"] = std::move(arr);
        }
        if (name.has_value()) j["name"] = *name;
        if (toolCallId.has_value()) j["tool_call_id"] = *toolCallId;
        return j;
    }

    bool isValid(std::string* reason = nullptr) const {
        if (std::holds_alternative<std::string>(content)) {
            if (std::get<std::string>(content).empty()) {
                if (reason) *reason = "content is empty";
                return false;
            }
        } else {
            const auto& parts = std::get<std::vector<MessageContentPart>>(content);
            if (parts.empty()) {
                if (reason) *reason = "content parts are empty";
                return false;
            }
            for (const auto& p : parts) {
                if (std::holds_alternative<ContentPartText>(p)) {
                    if (std::get<ContentPartText>(p).text.empty()) {
                        if (reason) *reason = "text part is empty";
                        return false;
                    }
                } else {
                    const auto& img = std::get<ContentPartImageUrl>(p).imageUrl;
                    std::string why;
                    if (!validateImageUrl(img.url, &why)) {
                        if (reason) *reason = "invalid image_url: " + why;
                        return false;
                    }
                }
            }
        }

        // role=tool: name/toolCallId are common but not required (compatibility).
        if (reason) reason->clear();
        return true;
    }

    // Rough token estimation on content (text uses TokenEstimator; image uses heuristic).
    std::size_t estimateTokens(const std::string& modelId) const {
        naw::desktop_pet::service::utils::TokenEstimator est;
        std::size_t total = 0;

        auto imageTokens = [](const std::string& url) -> std::size_t {
            // Heuristic:
            // - http/https: fixed base cost
            // - data URL: base cost + size-based cost (capped)
            constexpr std::size_t kBase = 200;
            constexpr std::size_t kMaxExtra = 2000;
            if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) return kBase;
            const auto pos = url.find(";base64,");
            if (pos == std::string::npos) return kBase;
            auto decoded = naw::desktop_pet::service::utils::decodeBase64(url.substr(pos + 8));
            if (!decoded.has_value()) return kBase;
            const std::size_t bytes = decoded->size();
            const std::size_t extra = std::min(kMaxExtra, (bytes / 2048) * 50);
            return kBase + extra;
        };

        if (std::holds_alternative<std::string>(content)) {
            total += est.estimateTokens(modelId, std::get<std::string>(content));
            return total;
        }

        for (const auto& p : std::get<std::vector<MessageContentPart>>(content)) {
            if (std::holds_alternative<ContentPartText>(p)) {
                total += est.estimateTokens(modelId, std::get<ContentPartText>(p).text);
            } else {
                total += imageTokens(std::get<ContentPartImageUrl>(p).imageUrl.url);
            }
        }
        return total;
    }
};

} // namespace naw::desktop_pet::service::types

