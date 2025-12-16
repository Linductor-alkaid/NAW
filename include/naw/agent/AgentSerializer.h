#pragma once

#include "Agent.h"
#include "AgentSerialization.h"
#include <string>
#include <memory>

// 前向声明，避免直接依赖Render命名空间
namespace Render {
    class JsonSerializer;
}

namespace naw {
namespace agent {

/**
 * Agent序列化器
 * 负责Agent数据的序列化和反序列化（JSON格式）
 * 
 * 使用Render项目中的JsonSerializer工具类进行文件操作
 */
class AgentSerializer {
public:
    AgentSerializer() = default;
    ~AgentSerializer() = default;
    
    // 禁止拷贝，允许移动
    AgentSerializer(const AgentSerializer&) = delete;
    AgentSerializer& operator=(const AgentSerializer&) = delete;
    AgentSerializer(AgentSerializer&&) = default;
    AgentSerializer& operator=(AgentSerializer&&) = default;
    
    /**
     * 将Agent序列化为JSON字符串
     * @param agent 要序列化的Agent
     * @return JSON字符串
     */
    std::string serialize(const Agent& agent) const;
    
    /**
     * 从JSON字符串反序列化Agent
     * @param jsonStr JSON字符串
     * @return 反序列化后的Agent，失败返回nullptr
     */
    std::unique_ptr<Agent> deserialize(const std::string& jsonStr) const;
    
    /**
     * 将Agent序列化为JSON并保存到文件
     * @param agent 要序列化的Agent
     * @param filepath 文件路径
     * @return 是否成功
     */
    bool saveToFile(const Agent& agent, const std::string& filepath) const;
    
    /**
     * 从文件加载Agent
     * @param filepath 文件路径
     * @return 加载的Agent，失败返回nullptr
     */
    std::unique_ptr<Agent> loadFromFile(const std::string& filepath) const;
};

} // namespace agent
} // namespace naw

