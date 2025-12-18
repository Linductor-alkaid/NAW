#pragma once

#include "naw/desktop_pet/service/utils/TokenCounter.h"

#include <optional>
#include <string>
#include <string_view>

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

struct ChatMessage {
    MessageRole role{MessageRole::User};
    std::string content;
    std::optional<std::string> name;       // tool name (role=tool) or assistant name
    std::optional<std::string> toolCallId; // tool_call_id

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

        // Text-first: if content is not a string, dump it to keep minimal compatibility.
        if (!j.contains("content")) return std::nullopt;
        if (j["content"].is_string()) {
            m.content = j["content"].get<std::string>();
        } else {
            m.content = j["content"].dump();
        }

        if (auto n = readStr("name", "name"); n.has_value()) m.name = *n;
        if (auto id = readStr("tool_call_id", "toolCallId"); id.has_value()) m.toolCallId = *id;

        return m;
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["role"] = roleToString(role);
        j["content"] = content;
        if (name.has_value()) j["name"] = *name;
        if (toolCallId.has_value()) j["tool_call_id"] = *toolCallId;
        return j;
    }

    bool isValid(std::string* reason = nullptr) const {
        if (content.empty()) {
            if (reason) *reason = "content is empty";
            return false;
        }

        // role=tool: name/toolCallId are common but not required (compatibility).
        if (reason) reason->clear();
        return true;
    }

    // Text-only estimation on content (role overhead can be added later).
    std::size_t estimateTokens(const std::string& modelId) const {
        naw::desktop_pet::service::utils::TokenEstimator est;
        return est.estimateTokens(modelId, content);
    }
};

} // namespace naw::desktop_pet::service::types

