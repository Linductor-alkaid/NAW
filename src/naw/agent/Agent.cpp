#include "naw/agent/Agent.h"
#include <algorithm>
#include <cmath>

namespace naw {
namespace agent {

Agent::Agent()
    : m_id(0)
{
}

Agent::Agent(uint64_t id)
    : m_id(id)
{
}

void Agent::addInjury(const Injury& injury) {
    m_physicalState.injuries.push_back(injury);
    
    // 如果是永久性伤害，标记为永久
    if (injury.isPermanent) {
        // 永久性伤害会影响健康上限和战斗能力
        // 这里只添加伤势，具体影响在calculateCombatAbility中计算
    }
    
    // 重新计算战斗能力
    m_physicalState.combatAbility = calculateCombatAbility();
}

void Agent::updateRelationship(uint64_t agentId, RelationshipType type, float strength) {
    // 限制强度范围
    strength = std::max(0.0f, std::min(100.0f, strength));
    
    Relationship& rel = m_socialState.relationships[agentId];
    rel.type = type;
    rel.strength = strength;
}

Relationship* Agent::getRelationship(uint64_t agentId) {
    auto it = m_socialState.relationships.find(agentId);
    if (it != m_socialState.relationships.end()) {
        return &it->second;
    }
    return nullptr;
}

const Relationship* Agent::getRelationship(uint64_t agentId) const {
    auto it = m_socialState.relationships.find(agentId);
    if (it != m_socialState.relationships.end()) {
        return &it->second;
    }
    return nullptr;
}

void Agent::addMemoryEvent(const MemoryEvent& event) {
    m_memory.recentEvents.push_back(event);
    
    // 限制最近事件数量
    if (m_memory.recentEvents.size() > m_memory.maxRecentEvents) {
        // 移除最旧的事件
        m_memory.recentEvents.erase(m_memory.recentEvents.begin());
    }
}

void Agent::addKeyMoment(const MemoryEvent& event) {
    m_memory.keyMoments.push_back(event);
    
    // 限制关键时刻数量
    if (m_memory.keyMoments.size() > m_memory.maxKeyMoments) {
        // 移除最旧的关键时刻
        m_memory.keyMoments.erase(m_memory.keyMoments.begin());
    }
}

void Agent::addPlayerInteraction(const MemoryEvent& event) {
    m_memory.playerInteractions.push_back(event);
    
    // 限制玩家互动数量
    if (m_memory.playerInteractions.size() > m_memory.maxPlayerInteractions) {
        // 移除最旧的互动
        m_memory.playerInteractions.erase(m_memory.playerInteractions.begin());
    }
}

float Agent::calculateCombatAbility() const {
    // 基础战斗能力（假设由技能决定）
    float baseAbility = (m_skills.melee + m_skills.ranged + m_skills.tactics) / 3.0f;
    
    // 健康值影响（健康值越低，战斗能力越低）
    float healthFactor = m_physicalState.health / 100.0f;
    
    // 伤势影响
    float injuryFactor = 1.0f;
    for (const auto& injury : m_physicalState.injuries) {
        // 根据伤势类型和影响因子计算
        float impact = injury.impactFactor;
        
        // 致残性伤害影响更大
        if (injury.type == InjuryType::Disabling) {
            impact *= 1.5f; // 致残性伤害影响加倍
        } else if (injury.type == InjuryType::Severe) {
            impact *= 1.2f; // 重伤影响增加20%
        }
        
        injuryFactor -= impact * 0.1f; // 每个伤势减少10%的基础能力（乘以影响因子）
    }
    
    // 确保影响因子在合理范围内
    injuryFactor = std::max(0.0f, std::min(1.0f, injuryFactor));
    
    // 体力影响
    float staminaFactor = m_physicalState.stamina / m_physicalState.maxStamina;
    
    // 士气影响（士气影响战斗表现）
    float moraleFactor = m_mentalState.morale / 100.0f;
    
    // 综合计算
    float finalAbility = baseAbility * healthFactor * injuryFactor * staminaFactor * moraleFactor;
    
    return std::max(0.0f, std::min(100.0f, finalAbility));
}

bool Agent::hasPermanentInjury() const {
    for (const auto& injury : m_physicalState.injuries) {
        if (injury.isPermanent) {
            return true;
        }
    }
    return false;
}

bool Agent::isSeverelyInjured() const {
    // 检查是否有重伤或致残性伤害
    for (const auto& injury : m_physicalState.injuries) {
        if (injury.type == InjuryType::Severe || injury.type == InjuryType::Disabling) {
            return true;
        }
    }
    
    // 或者健康值过低
    return m_physicalState.health < 30.0f;
}

} // namespace agent
} // namespace naw

