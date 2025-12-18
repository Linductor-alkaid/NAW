#pragma once

#include "naw/desktop_pet/service/types/ChatMessage.h"

#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace naw::desktop_pet::service::types {

struct Tool {
    std::string name;
    std::string description;
    nlohmann::json parameters; // JSON schema

    static std::optional<Tool> fromJson(const nlohmann::json& j) {
        if (!j.is_object()) return std::nullopt;

        // Supports two shapes:
        // 1) {name, description, parameters}
        // 2) OpenAI-compatible: {type:"function", function:{name, description, parameters}}
        const nlohmann::json* fn = &j;
        if (j.contains("function") && j["function"].is_object()) fn = &j["function"];

        if (!fn->contains("name") || !(*fn)["name"].is_string()) return std::nullopt;

        Tool t;
        t.name = (*fn)["name"].get<std::string>();
        if (fn->contains("description") && (*fn)["description"].is_string())
            t.description = (*fn)["description"].get<std::string>();
        if (fn->contains("parameters")) t.parameters = (*fn)["parameters"];
        return t;
    }

    nlohmann::json toJson() const {
        // Output OpenAI-compatible tool schema.
        return nlohmann::json{
            {"type", "function"},
            {"function",
             {
                 {"name", name},
                 {"description", description},
                 {"parameters", parameters},
             }},
        };
    }
};

struct FunctionCall {
    std::string name;
    nlohmann::json arguments; // object or string, keep raw

    static std::optional<FunctionCall> fromJson(const nlohmann::json& j) {
        if (!j.is_object()) return std::nullopt;
        if (!j.contains("name") || !j["name"].is_string()) return std::nullopt;
        FunctionCall fc;
        fc.name = j["name"].get<std::string>();
        if (j.contains("arguments")) fc.arguments = j["arguments"];
        return fc;
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["name"] = name;
        j["arguments"] = arguments;
        return j;
    }
};

struct ToolCall {
    std::string id;
    std::string type{"function"};
    FunctionCall function;

    static std::optional<ToolCall> fromJson(const nlohmann::json& j) {
        if (!j.is_object()) return std::nullopt;
        if (!j.contains("id") || !j["id"].is_string()) return std::nullopt;
        if (!j.contains("type") || !j["type"].is_string()) return std::nullopt;
        if (!j.contains("function") || !j["function"].is_object()) return std::nullopt;
        auto fn = FunctionCall::fromJson(j["function"]);
        if (!fn.has_value()) return std::nullopt;

        ToolCall tc;
        tc.id = j["id"].get<std::string>();
        tc.type = j["type"].get<std::string>();
        tc.function = *fn;
        return tc;
    }

    nlohmann::json toJson() const {
        return nlohmann::json{
            {"id", id},
            {"type", type},
            {"function", function.toJson()},
        };
    }
};

struct ChatRequest {
    std::string model;
    std::vector<ChatMessage> messages;
    std::optional<float> temperature;
    std::optional<uint32_t> maxTokens;
    std::optional<bool> stream;
    std::optional<std::string> stop; // stop sequence (single)
    std::optional<float> topP;
    std::optional<uint32_t> topK;

    // Function calling
    std::vector<nlohmann::json> tools; // keep raw OpenAI format array
    std::optional<std::string> toolChoice; // "auto"/"none"/tool name

    static std::optional<ChatRequest> fromJson(const nlohmann::json& j) {
        if (!j.is_object()) return std::nullopt;
        if (!j.contains("model") || !j["model"].is_string()) return std::nullopt;
        if (!j.contains("messages") || !j["messages"].is_array()) return std::nullopt;

        ChatRequest r;
        r.model = j["model"].get<std::string>();
        for (const auto& mj : j["messages"]) {
            auto m = ChatMessage::fromJson(mj);
            if (!m.has_value()) return std::nullopt;
            r.messages.push_back(*m);
        }

        if (j.contains("temperature") && j["temperature"].is_number())
            r.temperature = j["temperature"].get<float>();
        if (j.contains("max_tokens") && j["max_tokens"].is_number_integer())
            r.maxTokens = static_cast<uint32_t>(j["max_tokens"].get<int64_t>());
        if (j.contains("stream") && j["stream"].is_boolean()) r.stream = j["stream"].get<bool>();
        if (j.contains("stop") && j["stop"].is_string()) r.stop = j["stop"].get<std::string>();
        if (j.contains("top_p") && j["top_p"].is_number()) r.topP = j["top_p"].get<float>();
        if (j.contains("top_k") && j["top_k"].is_number_integer())
            r.topK = static_cast<uint32_t>(j["top_k"].get<int64_t>());

        // camelCase compatibility: maxTokens/topP/topK/toolChoice
        if (!r.maxTokens.has_value() && j.contains("maxTokens") && j["maxTokens"].is_number_integer())
            r.maxTokens = static_cast<uint32_t>(j["maxTokens"].get<int64_t>());
        if (!r.topP.has_value() && j.contains("topP") && j["topP"].is_number())
            r.topP = j["topP"].get<float>();
        if (!r.topK.has_value() && j.contains("topK") && j["topK"].is_number_integer())
            r.topK = static_cast<uint32_t>(j["topK"].get<int64_t>());

        if (j.contains("tools") && j["tools"].is_array()) r.tools = j["tools"];
        if (j.contains("tool_choice") && j["tool_choice"].is_string())
            r.toolChoice = j["tool_choice"].get<std::string>();
        if (!r.toolChoice.has_value() && j.contains("toolChoice") && j["toolChoice"].is_string())
            r.toolChoice = j["toolChoice"].get<std::string>();

        return r;
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["model"] = model;
        nlohmann::json ms = nlohmann::json::array();
        for (const auto& m : messages) ms.push_back(m.toJson());
        j["messages"] = std::move(ms);
        if (temperature.has_value()) j["temperature"] = *temperature;
        if (maxTokens.has_value()) j["max_tokens"] = *maxTokens;
        if (stream.has_value()) j["stream"] = *stream;
        if (stop.has_value()) j["stop"] = *stop;
        if (topP.has_value()) j["top_p"] = *topP;
        if (topK.has_value()) j["top_k"] = *topK;
        if (!tools.empty()) j["tools"] = tools;
        if (toolChoice.has_value()) j["tool_choice"] = *toolChoice;
        return j;
    }

    std::size_t estimateTokens() const {
        // Text-only estimation for messages' content.
        naw::desktop_pet::service::utils::TokenEstimator est;
        std::size_t total = 0;
        for (const auto& m : messages) total += est.estimateTokens(model, m.content);
        return total;
    }
};

struct ChatResponse {
    std::string content;
    std::vector<ToolCall> toolCalls;
    std::optional<std::string> finishReason;
    uint32_t promptTokens{0};
    uint32_t completionTokens{0};
    uint32_t totalTokens{0};
    std::optional<std::string> model;

    static std::optional<ChatResponse> fromJson(const nlohmann::json& j) {
        if (!j.is_object()) return std::nullopt;
        ChatResponse r;

        // OpenAI-compatible response: choices[0].message.content
        if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty() &&
            j["choices"][0].is_object()) {
            const auto& c0 = j["choices"][0];
            if (c0.contains("finish_reason") && c0["finish_reason"].is_string())
                r.finishReason = c0["finish_reason"].get<std::string>();
            if (c0.contains("message") && c0["message"].is_object()) {
                const auto& msg = c0["message"];
                if (msg.contains("content") && msg["content"].is_string())
                    r.content = msg["content"].get<std::string>();
                if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                    for (const auto& tcj : msg["tool_calls"]) {
                        auto tc = ToolCall::fromJson(tcj);
                        if (tc.has_value()) r.toolCalls.push_back(*tc);
                    }
                }
            }
        } else {
            // Simplified shape: {content, tool_calls, finish_reason, usage}
            if (j.contains("content") && j["content"].is_string()) r.content = j["content"].get<std::string>();
            if (j.contains("tool_calls") && j["tool_calls"].is_array()) {
                for (const auto& tcj : j["tool_calls"]) {
                    auto tc = ToolCall::fromJson(tcj);
                    if (tc.has_value()) r.toolCalls.push_back(*tc);
                }
            }
            if (j.contains("finish_reason") && j["finish_reason"].is_string())
                r.finishReason = j["finish_reason"].get<std::string>();
        }

        if (j.contains("usage") && j["usage"].is_object()) {
            const auto& u = j["usage"];
            if (u.contains("prompt_tokens") && u["prompt_tokens"].is_number_integer())
                r.promptTokens = static_cast<uint32_t>(u["prompt_tokens"].get<int64_t>());
            if (u.contains("completion_tokens") && u["completion_tokens"].is_number_integer())
                r.completionTokens = static_cast<uint32_t>(u["completion_tokens"].get<int64_t>());
            if (u.contains("total_tokens") && u["total_tokens"].is_number_integer())
                r.totalTokens = static_cast<uint32_t>(u["total_tokens"].get<int64_t>());
        }

        if (j.contains("model") && j["model"].is_string()) r.model = j["model"].get<std::string>();
        return r;
    }

    nlohmann::json toJson() const {
        // Output simplified snake_case for internal usage/tests.
        nlohmann::json j;
        j["content"] = content;
        if (!toolCalls.empty()) {
            nlohmann::json tcs = nlohmann::json::array();
            for (const auto& tc : toolCalls) tcs.push_back(tc.toJson());
            j["tool_calls"] = std::move(tcs);
        }
        if (finishReason.has_value()) j["finish_reason"] = *finishReason;
        j["usage"] = {
            {"prompt_tokens", promptTokens},
            {"completion_tokens", completionTokens},
            {"total_tokens", totalTokens},
        };
        if (model.has_value()) j["model"] = *model;
        return j;
    }

    bool hasToolCalls() const { return !toolCalls.empty(); }
};

} // namespace naw::desktop_pet::service::types

