#pragma once

#include "AgentTypes.h"
#include <nlohmann/json.hpp>

namespace naw {
namespace agent {

// ============================================================================
// 枚举类型序列化
// ============================================================================

inline void to_json(nlohmann::json& j, const AgentType& type) {
    j = static_cast<int>(type);
}

inline void from_json(const nlohmann::json& j, AgentType& type) {
    type = static_cast<AgentType>(j.get<int>());
}

inline void to_json(nlohmann::json& j, const InjuryType& type) {
    j = static_cast<int>(type);
}

inline void from_json(const nlohmann::json& j, InjuryType& type) {
    type = static_cast<InjuryType>(j.get<int>());
}

inline void to_json(nlohmann::json& j, const InjurySeverity& severity) {
    j = static_cast<int>(severity);
}

inline void from_json(const nlohmann::json& j, InjurySeverity& severity) {
    severity = static_cast<InjurySeverity>(j.get<int>());
}

inline void to_json(nlohmann::json& j, const RelationshipType& type) {
    j = static_cast<int>(type);
}

inline void from_json(const nlohmann::json& j, RelationshipType& type) {
    type = static_cast<RelationshipType>(j.get<int>());
}

// ============================================================================
// Injury 序列化
// ============================================================================

inline void to_json(nlohmann::json& j, const Injury& injury) {
    j = {
        {"type", injury.type},
        {"severity", injury.severity},
        {"description", injury.description},
        {"bodyPart", injury.bodyPart},
        {"impactFactor", injury.impactFactor},
        {"isPermanent", injury.isPermanent},
        {"timestamp", injury.timestamp}
    };
}

inline void from_json(const nlohmann::json& j, Injury& injury) {
    if (j.contains("type")) j.at("type").get_to(injury.type);
    if (j.contains("severity")) j.at("severity").get_to(injury.severity);
    if (j.contains("description")) j.at("description").get_to(injury.description);
    if (j.contains("bodyPart")) j.at("bodyPart").get_to(injury.bodyPart);
    if (j.contains("impactFactor")) j.at("impactFactor").get_to(injury.impactFactor);
    if (j.contains("isPermanent")) j.at("isPermanent").get_to(injury.isPermanent);
    if (j.contains("timestamp")) j.at("timestamp").get_to(injury.timestamp);
}

// ============================================================================
// PhysicalState 序列化
// ============================================================================

inline void to_json(nlohmann::json& j, const PhysicalState& state) {
    j = {
        {"health", state.health},
        {"stamina", state.stamina},
        {"maxStamina", state.maxStamina},
        {"combatAbility", state.combatAbility},
        {"injuries", state.injuries}
    };
}

inline void from_json(const nlohmann::json& j, PhysicalState& state) {
    if (j.contains("health")) j.at("health").get_to(state.health);
    if (j.contains("stamina")) j.at("stamina").get_to(state.stamina);
    if (j.contains("maxStamina")) j.at("maxStamina").get_to(state.maxStamina);
    if (j.contains("combatAbility")) j.at("combatAbility").get_to(state.combatAbility);
    if (j.contains("injuries")) j.at("injuries").get_to(state.injuries);
}

// ============================================================================
// MentalState 序列化
// ============================================================================

inline void to_json(nlohmann::json& j, const MentalState& state) {
    j = {
        {"morale", state.morale},
        {"stress", state.stress},
        {"loyaltyToPlayer", state.loyaltyToPlayer},
        {"trustLevel", state.trustLevel}
    };
}

inline void from_json(const nlohmann::json& j, MentalState& state) {
    if (j.contains("morale")) j.at("morale").get_to(state.morale);
    if (j.contains("stress")) j.at("stress").get_to(state.stress);
    if (j.contains("loyaltyToPlayer")) j.at("loyaltyToPlayer").get_to(state.loyaltyToPlayer);
    if (j.contains("trustLevel")) j.at("trustLevel").get_to(state.trustLevel);
}

// ============================================================================
// Relationship 序列化
// ============================================================================

inline void to_json(nlohmann::json& j, const Relationship& rel) {
    j = {
        {"type", rel.type},
        {"strength", rel.strength},
        {"lastInteractionTime", rel.lastInteractionTime}
    };
}

inline void from_json(const nlohmann::json& j, Relationship& rel) {
    if (j.contains("type")) j.at("type").get_to(rel.type);
    if (j.contains("strength")) j.at("strength").get_to(rel.strength);
    if (j.contains("lastInteractionTime")) j.at("lastInteractionTime").get_to(rel.lastInteractionTime);
}

// ============================================================================
// SocialState 序列化
// ============================================================================

inline void to_json(nlohmann::json& j, const SocialState& state) {
    j = {
        {"reputation", state.reputation},
        {"faction", state.faction},
        {"factionRank", state.factionRank},
        {"businessReputation", state.businessReputation},
        {"relationships", state.relationships}
    };
}

inline void from_json(const nlohmann::json& j, SocialState& state) {
    if (j.contains("reputation")) j.at("reputation").get_to(state.reputation);
    if (j.contains("faction")) j.at("faction").get_to(state.faction);
    if (j.contains("factionRank")) j.at("factionRank").get_to(state.factionRank);
    if (j.contains("businessReputation")) j.at("businessReputation").get_to(state.businessReputation);
    if (j.contains("relationships")) {
        const auto& rels = j.at("relationships");
        state.relationships.clear();
        for (auto it = rels.begin(); it != rels.end(); ++it) {
            uint64_t agentId = 0;
            try {
                // JSON 对象的键总是字符串类型，直接转换为 uint64_t
                agentId = std::stoull(it.key());
            } catch (...) {
                continue; // 跳过无效的键
            }
            Relationship rel;
            it.value().get_to(rel);
            state.relationships[agentId] = rel;
        }
    }
}

// ============================================================================
// EconomicState 序列化
// ============================================================================

inline void to_json(nlohmann::json& j, const EconomicState& state) {
    j = {
        {"wealth", state.wealth},
        {"debt", state.debt},
        {"resources", state.resources},
        {"items", state.items},
        {"merchantGoods", state.merchantGoods},
        {"pricingStrategy", state.pricingStrategy}
    };
}

inline void from_json(const nlohmann::json& j, EconomicState& state) {
    if (j.contains("wealth")) j.at("wealth").get_to(state.wealth);
    if (j.contains("debt")) j.at("debt").get_to(state.debt);
    if (j.contains("resources")) j.at("resources").get_to(state.resources);
    if (j.contains("items")) j.at("items").get_to(state.items);
    if (j.contains("merchantGoods")) j.at("merchantGoods").get_to(state.merchantGoods);
    if (j.contains("pricingStrategy")) {
        const auto& pricing = j.at("pricingStrategy");
        state.pricingStrategy.clear();
        for (auto it = pricing.begin(); it != pricing.end(); ++it) {
            uint64_t goodId = 0;
            try {
                // JSON 对象的键总是字符串类型，直接转换为 uint64_t
                goodId = std::stoull(it.key());
            } catch (...) {
                continue; // 跳过无效的键
            }
            float price = it.value().get<float>();
            state.pricingStrategy[goodId] = price;
        }
    }
}

// ============================================================================
// Personality 序列化
// ============================================================================

inline void to_json(nlohmann::json& j, const Personality& personality) {
    j = {
        {"courage", personality.courage},
        {"loyalty", personality.loyalty},
        {"independence", personality.independence},
        {"aggressiveness", personality.aggressiveness},
        {"cautiousness", personality.cautiousness}
    };
}

inline void from_json(const nlohmann::json& j, Personality& personality) {
    if (j.contains("courage")) j.at("courage").get_to(personality.courage);
    if (j.contains("loyalty")) j.at("loyalty").get_to(personality.loyalty);
    if (j.contains("independence")) j.at("independence").get_to(personality.independence);
    if (j.contains("aggressiveness")) j.at("aggressiveness").get_to(personality.aggressiveness);
    if (j.contains("cautiousness")) j.at("cautiousness").get_to(personality.cautiousness);
}

// ============================================================================
// SkillLevel 序列化
// ============================================================================

inline void to_json(nlohmann::json& j, const SkillLevel& skills) {
    j = {
        {"melee", skills.melee},
        {"ranged", skills.ranged},
        {"tactics", skills.tactics},
        {"persuasion", skills.persuasion},
        {"negotiation", skills.negotiation},
        {"leadership", skills.leadership},
        {"crafting", skills.crafting},
        {"medical", skills.medical},
        {"scouting", skills.scouting},
        {"knowledge", skills.knowledge}
    };
}

inline void from_json(const nlohmann::json& j, SkillLevel& skills) {
    if (j.contains("melee")) j.at("melee").get_to(skills.melee);
    if (j.contains("ranged")) j.at("ranged").get_to(skills.ranged);
    if (j.contains("tactics")) j.at("tactics").get_to(skills.tactics);
    if (j.contains("persuasion")) j.at("persuasion").get_to(skills.persuasion);
    if (j.contains("negotiation")) j.at("negotiation").get_to(skills.negotiation);
    if (j.contains("leadership")) j.at("leadership").get_to(skills.leadership);
    if (j.contains("crafting")) j.at("crafting").get_to(skills.crafting);
    if (j.contains("medical")) j.at("medical").get_to(skills.medical);
    if (j.contains("scouting")) j.at("scouting").get_to(skills.scouting);
    if (j.contains("knowledge")) j.at("knowledge").get_to(skills.knowledge);
}

// ============================================================================
// MemoryEvent 序列化
// ============================================================================

inline void to_json(nlohmann::json& j, const MemoryEvent& event) {
    j = {
        {"timestamp", event.timestamp},
        {"eventType", event.eventType},
        {"description", event.description},
        {"isKeyMoment", event.isKeyMoment},
        {"emotionalImpact", event.emotionalImpact},
        {"involvedAgents", event.involvedAgents}
    };
}

inline void from_json(const nlohmann::json& j, MemoryEvent& event) {
    if (j.contains("timestamp")) j.at("timestamp").get_to(event.timestamp);
    if (j.contains("eventType")) j.at("eventType").get_to(event.eventType);
    if (j.contains("description")) j.at("description").get_to(event.description);
    if (j.contains("isKeyMoment")) j.at("isKeyMoment").get_to(event.isKeyMoment);
    if (j.contains("emotionalImpact")) j.at("emotionalImpact").get_to(event.emotionalImpact);
    if (j.contains("involvedAgents")) j.at("involvedAgents").get_to(event.involvedAgents);
}

// ============================================================================
// MemorySystem 序列化
// ============================================================================

inline void to_json(nlohmann::json& j, const MemorySystem& memory) {
    j = {
        {"maxRecentEvents", memory.maxRecentEvents},
        {"maxKeyMoments", memory.maxKeyMoments},
        {"maxPlayerInteractions", memory.maxPlayerInteractions},
        {"recentEvents", memory.recentEvents},
        {"keyMoments", memory.keyMoments},
        {"playerInteractions", memory.playerInteractions}
    };
}

inline void from_json(const nlohmann::json& j, MemorySystem& memory) {
    if (j.contains("maxRecentEvents")) j.at("maxRecentEvents").get_to(memory.maxRecentEvents);
    if (j.contains("maxKeyMoments")) j.at("maxKeyMoments").get_to(memory.maxKeyMoments);
    if (j.contains("maxPlayerInteractions")) j.at("maxPlayerInteractions").get_to(memory.maxPlayerInteractions);
    if (j.contains("recentEvents")) j.at("recentEvents").get_to(memory.recentEvents);
    if (j.contains("keyMoments")) j.at("keyMoments").get_to(memory.keyMoments);
    if (j.contains("playerInteractions")) j.at("playerInteractions").get_to(memory.playerInteractions);
}

// ============================================================================
// Identity 序列化
// ============================================================================

inline void to_json(nlohmann::json& j, const Identity& identity) {
    j = {
        {"agentType", identity.agentType},
        {"name", identity.name},
        {"role", identity.role},
        {"narrativeImportance", identity.narrativeImportance},
        {"profession", identity.profession},
        {"storyRole", identity.storyRole},
        {"storyTags", identity.storyTags}
    };
}

inline void from_json(const nlohmann::json& j, Identity& identity) {
    if (j.contains("agentType")) j.at("agentType").get_to(identity.agentType);
    if (j.contains("name")) j.at("name").get_to(identity.name);
    if (j.contains("role")) j.at("role").get_to(identity.role);
    if (j.contains("narrativeImportance")) j.at("narrativeImportance").get_to(identity.narrativeImportance);
    if (j.contains("profession")) j.at("profession").get_to(identity.profession);
    if (j.contains("storyRole")) j.at("storyRole").get_to(identity.storyRole);
    if (j.contains("storyTags")) j.at("storyTags").get_to(identity.storyTags);
}

// ============================================================================
// Agent 序列化
// ============================================================================

inline void to_json(nlohmann::json& j, const Agent& agent) {
    j = {
        {"version", "1.0"},
        {"id", agent.getId()},
        {"identity", agent.getIdentity()},
        {"physicalState", agent.getPhysicalState()},
        {"mentalState", agent.getMentalState()},
        {"socialState", agent.getSocialState()},
        {"economicState", agent.getEconomicState()},
        {"personality", agent.getPersonality()},
        {"skills", agent.getSkills()},
        {"memory", agent.getMemory()}
    };
}

inline void from_json(const nlohmann::json& j, Agent& agent) {
    // 检查版本（可选）
    if (j.contains("version")) {
        std::string version = j.at("version").get<std::string>();
        // 未来可以根据版本号进行不同的解析
    }
    
    if (j.contains("id")) {
        uint64_t id = j.at("id").get<uint64_t>();
        agent.setId(id);
    }
    
    if (j.contains("identity")) {
        Identity identity;
        j.at("identity").get_to(identity);
        agent.setIdentity(identity);
    }
    
    if (j.contains("physicalState")) {
        PhysicalState state;
        j.at("physicalState").get_to(state);
        agent.setPhysicalState(state);
    }
    
    if (j.contains("mentalState")) {
        MentalState state;
        j.at("mentalState").get_to(state);
        agent.setMentalState(state);
    }
    
    if (j.contains("socialState")) {
        SocialState state;
        j.at("socialState").get_to(state);
        agent.setSocialState(state);
    }
    
    if (j.contains("economicState")) {
        EconomicState state;
        j.at("economicState").get_to(state);
        agent.setEconomicState(state);
    }
    
    if (j.contains("personality")) {
        Personality personality;
        j.at("personality").get_to(personality);
        agent.setPersonality(personality);
    }
    
    if (j.contains("skills")) {
        SkillLevel skills;
        j.at("skills").get_to(skills);
        agent.setSkills(skills);
    }
    
    if (j.contains("memory")) {
        MemorySystem memory;
        j.at("memory").get_to(memory);
        agent.setMemory(memory);
    }
}

} // namespace agent
} // namespace naw

