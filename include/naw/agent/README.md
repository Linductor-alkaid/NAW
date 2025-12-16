# Agent基础数据结构

本文档介绍NPC生命线与叙事管理系统中的Agent基础数据结构。

## 概述

Agent是游戏世界中NPC的核心数据结构，包含身份、状态、性格、能力和记忆等完整信息。

## 文件结构

- `AgentTypes.h`: 定义所有Agent相关的类型、枚举和数据结构
- `Agent.h`: Agent主类定义
- `Agent.cpp`: Agent类实现
- `AgentSerialization.h`: Agent序列化函数定义（使用nlohmann::json）
- `AgentSerializer.h`: Agent序列化器接口
- `AgentSerializer.cpp`: Agent序列化器实现（使用Render::JsonSerializer）

## 核心组件

### Agent类型

- `AgentType::Narrative`: 叙事Agent（参与主线或支线故事）
- `AgentType::World`: 世界Agent（维持世界运转的普通NPC）
- `AgentType::Government`: 政府Agent（特殊的独立Agent）

### 数据结构

#### Identity（身份属性）
- 角色定位、叙事重要性、职业
- 故事标签、在故事中的作用

#### PhysicalState（身体状态）
- 健康值（0-100）
- 伤势列表（轻伤、重伤、致残性伤害）
- 体力/耐力
- 战斗能力（受伤势影响）

#### MentalState（心理状态）
- 士气（影响战斗表现）
- 压力水平（影响决策理性度）
- 对玩家的忠诚度
- 信任度

#### SocialState（社交状态）
- 声望
- 与其他Agent的关系网络
- 所属阵营及地位
- 商业信誉

#### EconomicState（经济状态）
- 财富、债务
- 拥有的资源和物品
- 经营的商品列表（商人Agent）
- 商品定价策略

#### Personality（性格属性）
- 勇气、忠诚、独立性、攻击性、谨慎性（各0-100）

#### SkillLevel（能力属性）
- 战斗技能（近战、远程、战术）
- 社交技能（说服、交涉、领导）
- 专业技能（制作、医疗、侦查）
- 知识储备

#### MemorySystem（记忆系统）
- 最近经历的重要事件
- 关键转折时刻
- 与玩家的互动历史

## 使用示例

### 创建Agent

```cpp
#include "naw/agent/Agent.h"

using namespace naw::agent;

// 创建新Agent
Agent agent(1001); // 指定ID

// 设置身份
Identity identity;
identity.agentType = AgentType::Narrative;
identity.name = "张三";
identity.role = "主要伙伴";
identity.narrativeImportance = 80;
identity.profession = "战士";
identity.storyTags.insert("主线任务");
identity.storyRole = "玩家伙伴";
agent.setIdentity(identity);

// 设置性格
Personality personality;
personality.courage = 80.0f;
personality.loyalty = 90.0f;
personality.independence = 40.0f;
personality.aggressiveness = 70.0f;
personality.cautiousness = 50.0f;
agent.setPersonality(personality);
```

### 添加伤势

```cpp
Injury injury;
injury.type = InjuryType::Disabling;
injury.severity = InjurySeverity::Critical;
injury.description = "失去左臂";
injury.bodyPart = "左臂";
injury.impactFactor = 0.5f; // 影响50%的功能
injury.isPermanent = true;
injury.timestamp = getCurrentTimestamp();

agent.addInjury(injury);
```

### 更新关系

```cpp
// 更新与另一个Agent的关系
agent.updateRelationship(1002, RelationshipType::Trust, 85.0f);
```

### 添加记忆事件

```cpp
MemoryEvent event;
event.timestamp = getCurrentTimestamp();
event.eventType = "战斗";
event.description = "与玩家并肩作战";
event.isKeyMoment = true;
event.emotionalImpact = 30.0f; // 积极影响
event.involvedAgents.push_back(playerId);

agent.addKeyMoment(event);
```

### 序列化/反序列化

Agent序列化使用项目中的`Render::JsonSerializer`工具类，基于`nlohmann::json`库实现。

```cpp
#include "naw/agent/AgentSerializer.h"

AgentSerializer serializer;

// 序列化为JSON字符串
std::string jsonStr = serializer.serialize(agent);

// 保存到文件（使用Render::JsonSerializer）
serializer.saveToFile(agent, "agent_1001.json");

// 从文件加载（使用Render::JsonSerializer）
auto loadedAgent = serializer.loadFromFile("agent_1001.json");
if (loadedAgent) {
    // 使用加载的Agent
    std::cout << "Loaded agent: " << loadedAgent->getIdentity().name << std::endl;
}
```

**序列化实现方式**：
- 所有Agent相关类型的序列化函数定义在`AgentSerialization.h`中
- 使用`to_json`和`from_json`函数实现nlohmann::json的自动序列化
- 序列化器类使用`Render::JsonSerializer`进行文件操作

## 便捷方法

- `calculateCombatAbility()`: 计算战斗能力（考虑伤势、健康、体力、士气等）
- `isNarrativeAgent()`: 检查是否为叙事Agent
- `isWorldAgent()`: 检查是否为世界Agent
- `isGovernmentAgent()`: 检查是否为政府Agent
- `hasPermanentInjury()`: 检查是否有永久性伤害
- `isDead()`: 检查是否死亡
- `isSeverelyInjured()`: 检查是否重伤

## 注意事项

1. Agent使用移动语义，禁止拷贝构造和拷贝赋值
2. 所有状态值应在合理范围内（如健康值0-100）
3. 序列化器使用`nlohmann::json`库和项目中的`Render::JsonSerializer`工具类
4. 记忆系统有容量限制，会自动移除最旧的事件
5. 序列化函数定义在`AgentSerialization.h`中，遵循与UI主题序列化相同的模式

## 后续开发

- [x] 集成nlohmann::json库（已完成）
- [x] 使用项目中的JsonSerializer工具（已完成）
- [ ] 添加数据验证机制
- [ ] 实现增量序列化（只序列化变化的部分）
- [ ] 添加版本控制支持（序列化格式已包含版本字段）
- [ ] 添加序列化错误处理和日志记录

