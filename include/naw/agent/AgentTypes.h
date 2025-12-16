#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace naw {
namespace agent {

// Agent类型枚举
enum class AgentType : uint8_t {
    Narrative,  // 叙事Agent
    World,      // 世界Agent
    Government  // 政府Agent
};

// 伤势类型枚举
enum class InjuryType : uint8_t {
    Light,      // 轻伤：短期影响，可完全恢复
    Severe,     // 重伤：中期影响，需治疗
    Disabling   // 致残性伤害：永久影响，无法完全恢复
};

// 伤势严重程度
enum class InjurySeverity : uint8_t {
    Minor,      // 轻微
    Moderate,   // 中等
    Critical    // 严重
};

// 关系类型
enum class RelationshipType : uint8_t {
    Favor,      // 好感
    Respect,    // 尊重
    Trust,      // 信任
    Dependence  // 依赖
};

// 伤势信息
struct Injury {
    InjuryType type;              // 伤势类型
    InjurySeverity severity;      // 严重程度
    std::string description;      // 伤势描述（如"失去左臂"）
    std::string bodyPart;         // 受伤部位（如"左臂"、"头部"）
    float impactFactor;           // 影响因子（0.0-1.0，1.0表示完全丧失功能）
    bool isPermanent;             // 是否永久性伤害
    uint64_t timestamp;           // 受伤时间戳
    
    Injury()
        : type(InjuryType::Light)
        , severity(InjurySeverity::Minor)
        , impactFactor(0.0f)
        , isPermanent(false)
        , timestamp(0)
    {}
};

// 身体状态
struct PhysicalState {
    float health;                 // 健康值（0-100）
    std::vector<Injury> injuries; // 伤势列表
    float stamina;                // 体力/耐力（0-100）
    float maxStamina;             // 最大体力
    float combatAbility;          // 战斗能力（0-100，受伤势影响）
    
    PhysicalState()
        : health(100.0f)
        , stamina(100.0f)
        , maxStamina(100.0f)
        , combatAbility(100.0f)
    {}
};

// 心理状态
struct MentalState {
    float morale;                 // 士气（0-100，影响战斗表现）
    float stress;                 // 压力水平（0-100，影响决策理性度）
    float loyaltyToPlayer;         // 对玩家的忠诚度（0-100）
    float trustLevel;             // 信任度（0-100，影响听从指挥的程度）
    
    MentalState()
        : morale(50.0f)
        , stress(0.0f)
        , loyaltyToPlayer(50.0f)
        , trustLevel(50.0f)
    {}
};

// 关系信息
struct Relationship {
    RelationshipType type;         // 关系类型
    float strength;                // 关系强度（0-100）
    uint64_t lastInteractionTime; // 最后互动时间
    
    Relationship()
        : type(RelationshipType::Favor)
        , strength(0.0f)
        , lastInteractionTime(0)
    {}
};

// 社交状态
struct SocialState {
    float reputation;              // 声望（0-100）
    std::unordered_map<uint64_t, Relationship> relationships; // 与其他Agent的关系（Agent ID -> 关系）
    std::string faction;           // 所属阵营
    int32_t factionRank;           // 在阵营中的地位（-1表示无阵营）
    float businessReputation;      // 商业信誉（0-100，影响交易和价格调整频率）
    
    SocialState()
        : reputation(50.0f)
        , factionRank(-1)
        , businessReputation(50.0f)
    {}
};

// 经济状态
struct EconomicState {
    float wealth;                  // 财富
    float debt;                    // 债务
    std::vector<uint64_t> resources; // 拥有的资源ID列表
    std::vector<uint64_t> items;    // 拥有的物品ID列表
    std::vector<uint64_t> merchantGoods; // 经营的商品列表（商人Agent）
    std::unordered_map<uint64_t, float> pricingStrategy; // 商品定价策略（商品ID -> 价格系数）
    
    EconomicState()
        : wealth(0.0f)
        , debt(0.0f)
    {}
};

// 性格属性
struct Personality {
    float courage;                 // 勇气（0-100）：影响是否敢于战斗、冒险
    float loyalty;                 // 忠诚（0-100）：影响对玩家和组织的忠诚度
    float independence;            // 独立性（0-100）：影响自主决策的倾向（高独立性=不容易被指挥）
    float aggressiveness;          // 攻击性（0-100）：影响战斗风格
    float cautiousness;            // 谨慎性（0-100）：影响风险评估
    
    Personality()
        : courage(50.0f)
        , loyalty(50.0f)
        , independence(50.0f)
        , aggressiveness(50.0f)
        , cautiousness(50.0f)
    {}
};

// 技能等级
struct SkillLevel {
    float melee;                   // 近战技能（0-100）
    float ranged;                  // 远程技能（0-100）
    float tactics;                 // 战术技能（0-100）
    float persuasion;              // 说服技能（0-100）
    float negotiation;             // 交涉技能（0-100）
    float leadership;              // 领导技能（0-100）
    float crafting;                // 制作技能（0-100）
    float medical;                 // 医疗技能（0-100）
    float scouting;                // 侦查技能（0-100）
    float knowledge;               // 知识储备（0-100）
    
    SkillLevel()
        : melee(0.0f)
        , ranged(0.0f)
        , tactics(0.0f)
        , persuasion(0.0f)
        , negotiation(0.0f)
        , leadership(0.0f)
        , crafting(0.0f)
        , medical(0.0f)
        , scouting(0.0f)
        , knowledge(0.0f)
    {}
};

// 记忆事件
struct MemoryEvent {
    uint64_t timestamp;            // 事件时间戳
    std::string eventType;         // 事件类型（如"战斗"、"对话"、"交易"）
    std::string description;       // 事件描述
    std::vector<uint64_t> involvedAgents; // 涉及的Agent ID列表
    bool isKeyMoment;              // 是否为关键转折时刻
    float emotionalImpact;         // 情感影响（-100到100，正数表示积极，负数表示消极）
    
    MemoryEvent()
        : timestamp(0)
        , isKeyMoment(false)
        , emotionalImpact(0.0f)
    {}
};

// 记忆系统
struct MemorySystem {
    std::vector<MemoryEvent> recentEvents;      // 最近经历的重要事件
    std::vector<MemoryEvent> keyMoments;       // 关键转折时刻
    std::vector<MemoryEvent> playerInteractions; // 与玩家的互动历史
    size_t maxRecentEvents;                    // 最大最近事件数量
    size_t maxKeyMoments;                      // 最大关键时刻数量
    size_t maxPlayerInteractions;              // 最大玩家互动数量
    
    MemorySystem()
        : maxRecentEvents(50)
        , maxKeyMoments(20)
        , maxPlayerInteractions(100)
    {}
};

// 身份属性
struct Identity {
    AgentType agentType;           // Agent类型
    std::string name;              // Agent名称
    std::string role;               // 角色定位（如"主要伙伴"、"商人"）
    int32_t narrativeImportance;   // 叙事重要性（0-100，0表示不重要）
    std::string profession;        // 职业
    std::unordered_set<std::string> storyTags; // 故事标签（参与哪些故事线）
    std::string storyRole;         // 在故事中的作用
    
    Identity()
        : agentType(AgentType::World)
        , narrativeImportance(0)
    {}
};

} // namespace agent
} // namespace naw

