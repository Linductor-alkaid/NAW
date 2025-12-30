#pragma once

#include "naw/desktop_pet/service/ErrorTypes.h"

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"

namespace naw::desktop_pet::service {

/**
 * @brief 工具定义结构体
 *
 * 包含工具的名称、描述、参数 Schema 和处理器函数。
 */
struct ToolDefinition {
    std::string name;                          // 工具名称（唯一标识）
    std::string description;                   // 工具描述
    nlohmann::json parametersSchema;          // JSON Schema 格式的参数定义
    std::function<nlohmann::json(const nlohmann::json&)> handler; // 工具处理器函数

    /**
     * @brief 验证工具定义的有效性
     * @return 如果有效返回 true，否则返回 false
     * @param errorMsg 如果验证失败，输出错误信息
     */
    bool isValid(std::string* errorMsg = nullptr) const;
};

/**
 * @brief 工具管理器
 *
 * 提供工具的统一管理、注册、查询和执行功能。
 * 所有操作都是线程安全的。
 */
class ToolManager {
public:
    ToolManager();
    ~ToolManager() = default;

    // ========== 工具注册 ==========

    /**
     * @brief 注册工具
     * @param tool 工具定义
     * @param allowOverwrite 如果工具已存在，是否允许覆盖（默认 false）
     * @param error 如果注册失败，输出错误信息
     * @return 成功返回 true，失败返回 false
     */
    bool registerTool(const ToolDefinition& tool, bool allowOverwrite = false, ErrorInfo* error = nullptr);

    /**
     * @brief 批量注册工具
     * @param tools 工具定义列表
     * @param allowOverwrite 如果工具已存在，是否允许覆盖（默认 false）
     * @return 成功注册的工具数量
     */
    size_t registerTools(const std::vector<ToolDefinition>& tools, bool allowOverwrite = false);

    /**
     * @brief 注销工具
     * @param toolName 工具名称
     * @return 如果工具存在并成功移除返回 true，否则返回 false
     */
    bool unregisterTool(const std::string& toolName);

    // ========== 工具查询 ==========

    /**
     * @brief 按名称查询工具
     * @param toolName 工具名称
     * @return 如果找到返回工具定义的副本，否则返回 std::nullopt
     */
    std::optional<ToolDefinition> getTool(const std::string& toolName) const;

    /**
     * @brief 检查工具是否存在
     * @param toolName 工具名称
     * @return 如果存在返回 true，否则返回 false
     */
    bool hasTool(const std::string& toolName) const;

    /**
     * @brief 获取所有已注册的工具
     * @return 工具定义列表
     */
    std::vector<ToolDefinition> getAllTools() const;

    /**
     * @brief 获取所有工具名称
     * @return 工具名称列表
     */
    std::vector<std::string> getToolNames() const;

    /**
     * @brief 获取已注册工具的数量
     * @return 工具数量
     */
    size_t getToolCount() const;

    // ========== 工具执行 ==========

    /**
     * @brief 执行工具
     * @param toolName 工具名称
     * @param arguments 工具参数（JSON 对象）
     * @param error 如果执行失败，输出错误信息
     * @return 如果成功返回执行结果（JSON），失败返回 std::nullopt
     */
    std::optional<nlohmann::json> executeTool(const std::string& toolName,
                                               const nlohmann::json& arguments,
                                               ErrorInfo* error = nullptr);

    // ========== 参数验证 ==========

    /**
     * @brief 验证工具参数
     * @param tool 工具定义
     * @param arguments 待验证的参数（JSON 对象）
     * @param error 如果验证失败，输出错误信息
     * @return 如果验证通过返回 true，否则返回 false
     */
    static bool validateArguments(const ToolDefinition& tool,
                                  const nlohmann::json& arguments,
                                  ErrorInfo* error = nullptr);

private:
    mutable std::mutex m_mutex;                                    // 保护工具映射表的互斥锁
    std::unordered_map<std::string, ToolDefinition> m_tools;      // 工具映射表（工具名 -> 工具定义）

    /**
     * @brief 验证 JSON Schema 格式
     * @param schema JSON Schema
     * @param errorMsg 如果验证失败，输出错误信息
     * @return 如果有效返回 true，否则返回 false
     */
    static bool validateSchemaFormat(const nlohmann::json& schema, std::string* errorMsg = nullptr);

    /**
     * @brief 验证单个参数值
     * @param value 参数值
     * @param propertySchema 属性 Schema
     * @param errorMsg 如果验证失败，输出错误信息
     * @return 如果验证通过返回 true，否则返回 false
     */
    static bool validatePropertyValue(const nlohmann::json& value,
                                      const nlohmann::json& propertySchema,
                                      std::string* errorMsg = nullptr);
};

} // namespace naw::desktop_pet::service

