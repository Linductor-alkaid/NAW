#pragma once

#include "naw/desktop_pet/service/ErrorTypes.h"

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"

namespace naw::desktop_pet::service {

// 前向声明
class ErrorHandler;

/**
 * @brief 工具权限级别
 */
enum class PermissionLevel {
    Public,      // 公开工具，所有用户可用
    Restricted,  // 受限工具，需要特定权限
    Admin        // 管理员工具，仅管理员可用
};

/**
 * @brief 工具使用统计信息
 */
struct ToolUsageStats {
    size_t callCount{0};                                                      // 调用次数
    std::chrono::system_clock::time_point lastCallTime;                      // 最后调用时间
    double averageExecutionTimeMs{0.0};                                       // 平均执行时间（毫秒）
    size_t errorCount{0};                                                     // 错误次数
    double errorRate{0.0};                                                    // 错误率（errorCount / callCount）

    /**
     * @brief 转换为JSON格式
     */
    nlohmann::json toJson() const;
};

/**
 * @brief 工具过滤条件
 */
struct ToolFilter {
    std::optional<std::string> namePrefix;                                    // 名称前缀过滤
    std::optional<PermissionLevel> permissionLevel;                         // 权限级别过滤
    // 可扩展其他过滤条件
};

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
    PermissionLevel permissionLevel{PermissionLevel::Public}; // 权限级别（默认公开）

    /**
     * @brief 验证工具定义的有效性
     * @return 如果有效返回 true，否则返回 false
     * @param errorMsg 如果验证失败，输出错误信息
     */
    bool isValid(std::string* errorMsg = nullptr) const;

    /**
     * @brief 将工具定义序列化为JSON（handler函数无法序列化）
     * @return JSON对象
     */
    nlohmann::json toJson() const;

    /**
     * @brief 从JSON反序列化工具定义（需要外部提供handler）
     * @param json JSON对象
     * @param errorMsg 如果失败，输出错误信息
     * @return 如果成功返回工具定义，否则返回 std::nullopt
     */
    static std::optional<ToolDefinition> fromJson(const nlohmann::json& json, std::string* errorMsg = nullptr);
};

/**
 * @brief 工具管理器
 *
 * 提供工具的统一管理、注册、查询和执行功能。
 * 所有操作都是线程安全的。
 */
class ToolManager {
public:
    /**
     * @brief 构造函数
     * @param errorHandler 错误处理器（可选，用于统一错误处理和日志记录）
     */
    explicit ToolManager(ErrorHandler* errorHandler = nullptr);
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
     * @brief 获取所有工具的定义（转换为OpenAI Function Calling格式）
     * 
     * 将ToolManager中的所有工具转换为OpenAI兼容的Function Calling格式，
     * 用于填充ChatRequest.tools字段，供LLM使用。
     * 
     * @return OpenAI Function Calling格式的工具列表（std::vector<nlohmann::json>）
     *         每个元素格式：{"type": "function", "function": {"name": "...", "description": "...", "parameters": {...}}}
     */
    std::vector<nlohmann::json> getToolsForAPI() const;

    /**
     * @brief 获取已注册工具的数量
     * @return 工具数量
     */
    size_t getToolCount() const;

    /**
     * @brief 按名称前缀获取工具
     * @param prefix 名称前缀
     * @return 匹配的工具列表
     */
    std::vector<ToolDefinition> getToolsByPrefix(const std::string& prefix) const;

    /**
     * @brief 按权限级别获取工具
     * @param level 权限级别
     * @return 匹配的工具列表
     */
    std::vector<ToolDefinition> getToolsByPermission(PermissionLevel level) const;

    /**
     * @brief 使用过滤条件获取工具
     * @param filter 过滤条件
     * @return 匹配的工具列表
     */
    std::vector<ToolDefinition> getFilteredTools(const ToolFilter& filter) const;

    // ========== 工具执行 ==========

    /**
     * @brief 执行工具
     * @param toolName 工具名称
     * @param arguments 工具参数（JSON 对象）
     * @param error 如果执行失败，输出错误信息
     * @param checkPermission 是否检查权限（默认 false，保持向后兼容）
     * @param requiredPermission 如果检查权限，所需的权限级别（默认 Public）
     * @return 如果成功返回执行结果（JSON），失败返回 std::nullopt
     */
    std::optional<nlohmann::json> executeTool(const std::string& toolName,
                                               const nlohmann::json& arguments,
                                               ErrorInfo* error = nullptr,
                                               bool checkPermission = false,
                                               PermissionLevel requiredPermission = PermissionLevel::Public);

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

    // ========== 权限控制 ==========

    /**
     * @brief 检查工具权限
     * @param toolName 工具名称
     * @param requiredLevel 所需的权限级别
     * @return 如果工具存在且权限足够返回 true，否则返回 false
     */
    bool checkPermission(const std::string& toolName, PermissionLevel requiredLevel) const;

    // ========== 统计功能 ==========

    /**
     * @brief 获取工具使用统计
     * @param toolName 工具名称
     * @return 如果找到返回统计信息，否则返回 std::nullopt
     */
    std::optional<ToolUsageStats> getToolStats(const std::string& toolName) const;

    /**
     * @brief 获取所有工具的统计信息
     * @return 工具统计信息映射（工具名 -> 统计信息）
     */
    std::unordered_map<std::string, ToolUsageStats> getAllToolStats() const;

    /**
     * @brief 重置工具统计信息
     * @param toolName 工具名称（如果为空，重置所有工具的统计）
     */
    void resetToolStats(const std::string& toolName = "");

    // ========== ErrorHandler 设置 ==========

    /**
     * @brief 设置错误处理器
     * @param errorHandler 错误处理器指针（可以为 nullptr）
     */
    void setErrorHandler(ErrorHandler* errorHandler);

private:
    mutable std::mutex m_mutex;                                    // 保护工具映射表的互斥锁
    std::unordered_map<std::string, ToolDefinition> m_tools;      // 工具映射表（工具名 -> 工具定义）
    std::unordered_map<std::string, ToolUsageStats> m_stats;      // 工具统计信息映射
    mutable std::mutex m_statsMutex;                               // 保护统计信息的互斥锁
    ErrorHandler* m_errorHandler;                                   // 错误处理器（可选）

    /**
     * @brief 更新工具统计信息
     * @param toolName 工具名称
     * @param executionTimeMs 执行时间（毫秒）
     * @param success 是否成功
     */
    void updateToolStats(const std::string& toolName, double executionTimeMs, bool success);

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

