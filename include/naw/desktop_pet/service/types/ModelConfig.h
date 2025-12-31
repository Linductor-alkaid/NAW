#pragma once

#include "naw/desktop_pet/service/types/TaskType.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace naw::desktop_pet::service::types {

struct ModelConfig {
    std::string modelId;
    std::string displayName;
    std::vector<TaskType> supportedTasks;
    uint32_t maxContextTokens{0};
    float defaultTemperature{0.7f};
    uint32_t defaultMaxTokens{0};
    float costPer1kTokens{0.0f};
    uint32_t maxConcurrentRequests{0};
    bool supportsStreaming{true};
    std::optional<std::string> recommendedPromptStyle;
    float performanceScore{0.0f};
    std::optional<std::string> apiProvider;  // 指定使用的 API 提供商（如 "zhipu"）

    static std::optional<ModelConfig> fromJson(const nlohmann::json& j) {
        if (!j.is_object()) return std::nullopt;

        auto getStr = [&](const char* snake, const char* camel) -> std::optional<std::string> {
            if (j.contains(snake) && j[snake].is_string()) return j[snake].get<std::string>();
            if (camel && j.contains(camel) && j[camel].is_string()) return j[camel].get<std::string>();
            return std::nullopt;
        };
        auto getU32 = [&](const char* snake, const char* camel) -> std::optional<uint32_t> {
            const char* keys[2] = {snake, camel};
            for (const char* k : keys) {
                if (!k) continue;
                if (!j.contains(k)) continue;
                if (j[k].is_number_unsigned()) return j[k].get<uint32_t>();
                if (j[k].is_number_integer()) {
                    auto v = j[k].get<int64_t>();
                    if (v < 0) return std::nullopt;
                    return static_cast<uint32_t>(v);
                }
            }
            return std::nullopt;
        };
        auto getF32 = [&](const char* snake, const char* camel) -> std::optional<float> {
            const char* keys[2] = {snake, camel};
            for (const char* k : keys) {
                if (!k) continue;
                if (!j.contains(k)) continue;
                if (j[k].is_number()) return j[k].get<float>();
            }
            return std::nullopt;
        };
        auto getBool = [&](const char* snake, const char* camel) -> std::optional<bool> {
            const char* keys[2] = {snake, camel};
            for (const char* k : keys) {
                if (!k) continue;
                if (!j.contains(k)) continue;
                if (j[k].is_boolean()) return j[k].get<bool>();
            }
            return std::nullopt;
        };

        ModelConfig cfg;

        // 必需字段
        auto modelId = getStr("model_id", "modelId");
        if (!modelId.has_value()) return std::nullopt;
        cfg.modelId = *modelId;

        if (auto dn = getStr("display_name", "displayName"); dn.has_value()) cfg.displayName = *dn;

        // supported_tasks/supportsTasks
        const char* tasksKeys[2] = {"supported_tasks", "supportedTasks"};
        for (const char* k : tasksKeys) {
            if (!j.contains(k)) continue;
            if (!j[k].is_array()) continue;
            for (const auto& item : j[k]) {
                if (!item.is_string()) continue;
                auto t = stringToTaskType(item.get<std::string>());
                if (t.has_value()) cfg.supportedTasks.push_back(*t);
            }
            break;
        }

        if (auto v = getU32("max_context_tokens", "maxContextTokens"); v.has_value())
            cfg.maxContextTokens = *v;
        if (auto v = getF32("default_temperature", "defaultTemperature"); v.has_value())
            cfg.defaultTemperature = *v;
        if (auto v = getU32("default_max_tokens", "defaultMaxTokens"); v.has_value())
            cfg.defaultMaxTokens = *v;
        if (auto v = getF32("cost_per_1k_tokens", "costPer1kTokens"); v.has_value())
            cfg.costPer1kTokens = *v;
        if (auto v = getU32("max_concurrent_requests", "maxConcurrentRequests"); v.has_value())
            cfg.maxConcurrentRequests = *v;
        if (auto v = getBool("supports_streaming", "supportsStreaming"); v.has_value())
            cfg.supportsStreaming = *v;
        if (auto v = getStr("recommended_prompt_style", "recommendedPromptStyle"); v.has_value())
            cfg.recommendedPromptStyle = *v;
        if (auto v = getF32("performance_score", "performanceScore"); v.has_value())
            cfg.performanceScore = *v;
        if (auto v = getStr("api_provider", "apiProvider"); v.has_value())
            cfg.apiProvider = *v;

        return cfg;
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["model_id"] = modelId;
        j["display_name"] = displayName;
        nlohmann::json tasks = nlohmann::json::array();
        for (auto t : supportedTasks) tasks.push_back(taskTypeToString(t));
        j["supported_tasks"] = std::move(tasks);
        j["max_context_tokens"] = maxContextTokens;
        j["default_temperature"] = defaultTemperature;
        j["default_max_tokens"] = defaultMaxTokens;
        j["cost_per_1k_tokens"] = costPer1kTokens;
        j["max_concurrent_requests"] = maxConcurrentRequests;
        j["supports_streaming"] = supportsStreaming;
        if (recommendedPromptStyle.has_value())
            j["recommended_prompt_style"] = *recommendedPromptStyle;
        j["performance_score"] = performanceScore;
        if (apiProvider.has_value())
            j["api_provider"] = *apiProvider;
        return j;
    }

    bool supportsTask(TaskType t) const {
        return std::find(supportedTasks.begin(), supportedTasks.end(), t) != supportedTasks.end();
    }

    bool isValid(std::vector<std::string>* errors = nullptr) const {
        bool ok = true;
        auto push = [&](std::string msg) {
            ok = false;
            if (errors) errors->push_back(std::move(msg));
        };

        if (modelId.empty()) push("modelId is empty");
        if (maxContextTokens == 0) push("maxContextTokens is 0");
        if (defaultTemperature < 0.0f || defaultTemperature > 2.0f)
            push("defaultTemperature out of range [0,2]");
        if (maxConcurrentRequests == 0) push("maxConcurrentRequests is 0");
        if (supportedTasks.empty()) push("supportedTasks is empty");
        if (performanceScore < 0.0f || performanceScore > 1.0f)
            push("performanceScore out of range [0,1]");

        return ok;
    }
};

} // namespace naw::desktop_pet::service::types

