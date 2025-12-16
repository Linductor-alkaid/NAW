#include "naw/agent/AgentSerializer.h"
#include "render/json_serializer.h"
#include <nlohmann/json.hpp>
#include <sstream>

namespace naw {
namespace agent {

std::string AgentSerializer::serialize(const Agent& agent) const {
    try {
        nlohmann::json j = agent;
        return j.dump(4); // 4空格缩进
    } catch (const std::exception& e) {
        // 错误处理：返回空JSON对象
        return "{}";
    }
}

std::unique_ptr<Agent> AgentSerializer::deserialize(const std::string& jsonStr) const {
    try {
        nlohmann::json j = nlohmann::json::parse(jsonStr);
        auto agent = std::make_unique<Agent>();
        j.get_to(*agent);
        return agent;
    } catch (const nlohmann::json::parse_error& e) {
        // JSON解析错误
        return nullptr;
    } catch (const std::exception& e) {
        // 其他错误
        return nullptr;
    }
}

bool AgentSerializer::saveToFile(const Agent& agent, const std::string& filepath) const {
    try {
        nlohmann::json j = agent;
        return Render::JsonSerializer::SaveToFile(j, filepath, 4);
    } catch (const std::exception& e) {
        return false;
    }
}

std::unique_ptr<Agent> AgentSerializer::loadFromFile(const std::string& filepath) const {
    try {
        nlohmann::json j;
        if (!Render::JsonSerializer::LoadFromFile(filepath, j)) {
            return nullptr;
        }
        
        auto agent = std::make_unique<Agent>();
        j.get_to(*agent);
        return agent;
    } catch (const std::exception& e) {
        return nullptr;
    }
}

} // namespace agent
} // namespace naw
