#pragma once

#include "AgentTypes.h"
#include <cstdint>
#include <string>
#include <memory>

namespace naw {
namespace agent {

/**
 * Agent类 - NPC生命线与叙事管理系统的核心数据结构
 * 
 * Agent代表游戏世界中的NPC，包含身份、状态、性格、能力和记忆等完整信息
 */
class Agent {
public:
    Agent();
    explicit Agent(uint64_t id);
    ~Agent() = default;
    
    // 禁止拷贝，允许移动
    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;
    Agent(Agent&&) = default;
    Agent& operator=(Agent&&) = default;
    
    // 基础属性访问
    uint64_t getId() const { return m_id; }
    void setId(uint64_t id) { m_id = id; }
    
    // 身份属性访问
    const Identity& getIdentity() const { return m_identity; }
    Identity& getIdentity() { return m_identity; }
    void setIdentity(const Identity& identity) { m_identity = identity; }
    
    // 状态属性访问
    const PhysicalState& getPhysicalState() const { return m_physicalState; }
    PhysicalState& getPhysicalState() { return m_physicalState; }
    void setPhysicalState(const PhysicalState& state) { m_physicalState = state; }
    
    const MentalState& getMentalState() const { return m_mentalState; }
    MentalState& getMentalState() { return m_mentalState; }
    void setMentalState(const MentalState& state) { m_mentalState = state; }
    
    const SocialState& getSocialState() const { return m_socialState; }
    SocialState& getSocialState() { return m_socialState; }
    void setSocialState(const SocialState& state) { m_socialState = state; }
    
    const EconomicState& getEconomicState() const { return m_economicState; }
    EconomicState& getEconomicState() { return m_economicState; }
    void setEconomicState(const EconomicState& state) { m_economicState = state; }
    
    // 性格属性访问
    const Personality& getPersonality() const { return m_personality; }
    Personality& getPersonality() { return m_personality; }
    void setPersonality(const Personality& personality) { m_personality = personality; }
    
    // 能力属性访问
    const SkillLevel& getSkills() const { return m_skills; }
    SkillLevel& getSkills() { return m_skills; }
    void setSkills(const SkillLevel& skills) { m_skills = skills; }
    
    // 记忆系统访问
    const MemorySystem& getMemory() const { return m_memory; }
    MemorySystem& getMemory() { return m_memory; }
    void setMemory(const MemorySystem& memory) { m_memory = memory; }
    
    // 便捷方法：添加伤势
    void addInjury(const Injury& injury);
    
    // 便捷方法：更新关系
    void updateRelationship(uint64_t agentId, RelationshipType type, float strength);
    Relationship* getRelationship(uint64_t agentId);
    const Relationship* getRelationship(uint64_t agentId) const;
    
    // 便捷方法：添加记忆事件
    void addMemoryEvent(const MemoryEvent& event);
    void addKeyMoment(const MemoryEvent& event);
    void addPlayerInteraction(const MemoryEvent& event);
    
    // 便捷方法：计算战斗能力（考虑伤势影响）
    float calculateCombatAbility() const;
    
    // 便捷方法：检查是否为叙事Agent
    bool isNarrativeAgent() const { return m_identity.agentType == AgentType::Narrative; }
    
    // 便捷方法：检查是否为世界Agent
    bool isWorldAgent() const { return m_identity.agentType == AgentType::World; }
    
    // 便捷方法：检查是否为政府Agent
    bool isGovernmentAgent() const { return m_identity.agentType == AgentType::Government; }
    
    // 便捷方法：检查是否有永久性伤害
    bool hasPermanentInjury() const;
    
    // 便捷方法：检查是否死亡
    bool isDead() const { return m_physicalState.health <= 0.0f; }
    
    // 便捷方法：检查是否重伤
    bool isSeverelyInjured() const;
    
private:
    uint64_t m_id;                 // Agent唯一ID
    
    Identity m_identity;            // 身份属性
    PhysicalState m_physicalState;  // 身体状态
    MentalState m_mentalState;     // 心理状态
    SocialState m_socialState;     // 社交状态
    EconomicState m_economicState;  // 经济状态
    Personality m_personality;      // 性格属性
    SkillLevel m_skills;            // 能力属性
    MemorySystem m_memory;          // 记忆系统
};

} // namespace agent
} // namespace naw

