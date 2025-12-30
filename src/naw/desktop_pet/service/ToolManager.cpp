#include "naw/desktop_pet/service/ToolManager.h"

#include "naw/desktop_pet/service/ErrorHandler.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"

#include <algorithm>
#include <chrono>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace naw::desktop_pet::service {

// ========== ToolUsageStats ==========

nlohmann::json ToolUsageStats::toJson() const {
    nlohmann::json j;
    j["call_count"] = callCount;
    // 如果从未调用过，lastCallTime可能是默认值，需要检查
    if (callCount > 0) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(lastCallTime.time_since_epoch()).count();
        j["last_call_time_ms"] = ms;
    } else {
        j["last_call_time_ms"] = 0;
    }
    j["average_execution_time_ms"] = averageExecutionTimeMs;
    j["error_count"] = errorCount;
    j["error_rate"] = errorRate;
    return j;
}

// ========== ToolDefinition ==========

nlohmann::json ToolDefinition::toJson() const {
    nlohmann::json j;
    j["name"] = name;
    j["description"] = description;
    j["parameters_schema"] = parametersSchema;
    // 权限级别转换为字符串
    const char* permStr = "Public";
    switch (permissionLevel) {
        case PermissionLevel::Restricted:
            permStr = "Restricted";
            break;
        case PermissionLevel::Admin:
            permStr = "Admin";
            break;
        default:
            permStr = "Public";
            break;
    }
    j["permission_level"] = permStr;
    // 注意：handler函数无法序列化，需要在反序列化时外部提供
    j["_requires_handler"] = true;
    return j;
}

std::optional<ToolDefinition> ToolDefinition::fromJson(const nlohmann::json& json, std::string* errorMsg) {
    ToolDefinition tool;

    // 解析名称
    if (!json.contains("name") || !json["name"].is_string()) {
        if (errorMsg) *errorMsg = "Missing or invalid 'name' field";
        return std::nullopt;
    }
    tool.name = json["name"].get<std::string>();

    // 解析描述
    if (json.contains("description") && json["description"].is_string()) {
        tool.description = json["description"].get<std::string>();
    }

    // 解析参数Schema
    if (!json.contains("parameters_schema") || !json["parameters_schema"].is_object()) {
        if (errorMsg) *errorMsg = "Missing or invalid 'parameters_schema' field";
        return std::nullopt;
    }
    tool.parametersSchema = json["parameters_schema"];

    // 解析权限级别
    if (json.contains("permission_level") && json["permission_level"].is_string()) {
        std::string permStr = json["permission_level"].get<std::string>();
        if (permStr == "Restricted") {
            tool.permissionLevel = PermissionLevel::Restricted;
        } else if (permStr == "Admin") {
            tool.permissionLevel = PermissionLevel::Admin;
        } else {
            tool.permissionLevel = PermissionLevel::Public;
        }
    } else {
        tool.permissionLevel = PermissionLevel::Public; // 默认值
    }

    // 注意：handler需要外部提供，这里不设置
    // 调用者需要在反序列化后设置handler

    return tool;
}

bool ToolDefinition::isValid(std::string* errorMsg) const {
    if (name.empty()) {
        if (errorMsg) *errorMsg = "Tool name cannot be empty";
        return false;
    }

    if (!handler) {
        if (errorMsg) *errorMsg = "Tool handler cannot be null";
        return false;
    }

    // 验证 Schema 格式（基本检查）
    if (!parametersSchema.is_object()) {
        if (errorMsg) *errorMsg = "Parameters schema must be a JSON object";
        return false;
    }

    // 检查 Schema 是否包含 type 字段（至少应该是 object）
    if (!parametersSchema.contains("type") || !parametersSchema["type"].is_string()) {
        // 如果没有 type 字段，至少应该有 properties 字段
        if (!parametersSchema.contains("properties") || !parametersSchema["properties"].is_object()) {
            if (errorMsg) *errorMsg = "Parameters schema must have 'type' or 'properties' field";
            return false;
        }
    }

    return true;
}

// ========== ToolManager ==========

ToolManager::ToolManager(ErrorHandler* errorHandler) : m_errorHandler(errorHandler) {}

bool ToolManager::registerTool(const ToolDefinition& tool, bool allowOverwrite, ErrorInfo* error) {
    // 验证工具定义
    std::string validationError;
    if (!tool.isValid(&validationError)) {
        if (error) {
            error->errorType = ErrorType::InvalidRequest;
            error->message = "Invalid tool definition: " + validationError;
        }
        return false;
    }

    // 验证 Schema 格式
    std::string schemaError;
    if (!validateSchemaFormat(tool.parametersSchema, &schemaError)) {
        if (error) {
            error->errorType = ErrorType::InvalidRequest;
            error->message = "Invalid schema format: " + schemaError;
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // 检查工具是否已存在
    if (m_tools.find(tool.name) != m_tools.end() && !allowOverwrite) {
        if (error) {
            error->errorType = ErrorType::InvalidRequest;
            error->message = "Tool '" + tool.name + "' already exists";
        }
        return false;
    }

    // 注册工具
    m_tools[tool.name] = tool;
    return true;
}

size_t ToolManager::registerTools(const std::vector<ToolDefinition>& tools, bool allowOverwrite) {
    size_t successCount = 0;
    for (const auto& tool : tools) {
        if (registerTool(tool, allowOverwrite)) {
            successCount++;
        }
    }
    return successCount;
}

bool ToolManager::unregisterTool(const std::string& toolName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tools.erase(toolName) > 0;
}

std::optional<ToolDefinition> ToolManager::getTool(const std::string& toolName) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_tools.find(toolName);
    if (it != m_tools.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool ToolManager::hasTool(const std::string& toolName) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tools.find(toolName) != m_tools.end();
}

std::vector<ToolDefinition> ToolManager::getAllTools() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ToolDefinition> result;
    result.reserve(m_tools.size());
    for (const auto& pair : m_tools) {
        result.push_back(pair.second);
    }
    return result;
}

std::vector<std::string> ToolManager::getToolNames() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> result;
    result.reserve(m_tools.size());
    for (const auto& pair : m_tools) {
        result.push_back(pair.first);
    }
    return result;
}

std::vector<nlohmann::json> ToolManager::getToolsForAPI() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<nlohmann::json> result;
    result.reserve(m_tools.size());
    
    for (const auto& pair : m_tools) {
        const auto& tool = pair.second;
        
        // 转换为OpenAI Function Calling格式
        // 格式参考：https://docs.siliconflow.cn/cn/userguide/guides/function-calling
        nlohmann::json toolJson = nlohmann::json{
            {"type", "function"},
            {"function", {
                {"name", tool.name},
                {"description", tool.description},
                {"parameters", tool.parametersSchema}
            }}
        };
        
        result.push_back(std::move(toolJson));
    }
    
    return result;
}

std::vector<nlohmann::json> ToolManager::getToolsForAPI(const ToolFilter& filter) const {
    // 获取过滤后的工具列表
    auto filteredTools = getFilteredTools(filter);
    
    // 转换为OpenAI Function Calling格式
    std::vector<nlohmann::json> result;
    result.reserve(filteredTools.size());
    
    for (const auto& tool : filteredTools) {
        nlohmann::json toolJson = nlohmann::json{
            {"type", "function"},
            {"function", {
                {"name", tool.name},
                {"description", tool.description},
                {"parameters", tool.parametersSchema}
            }}
        };
        
        result.push_back(std::move(toolJson));
    }
    
    return result;
}

size_t ToolManager::getToolCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tools.size();
}

std::optional<nlohmann::json> ToolManager::executeTool(const std::string& toolName,
                                                        const nlohmann::json& arguments,
                                                        ErrorInfo* error,
                                                        bool checkPermission,
                                                        PermissionLevel requiredPermission) {
    // 记录执行开始时间
    auto startTime = std::chrono::high_resolution_clock::now();
    bool executionSuccess = false;

    // 查找工具
    auto toolOpt = getTool(toolName);
    if (!toolOpt.has_value()) {
        ErrorInfo err;
        err.errorType = ErrorType::InvalidRequest;
        err.message = "Tool '" + toolName + "' not found";
        
        if (error) {
            *error = err;
        }
        
        // 记录错误日志
        if (m_errorHandler) {
            m_errorHandler->log(ErrorHandler::LogLevel::Warning, 
                               "Tool execution failed: " + err.message, 
                               std::make_optional(err));
        }
        
        // 更新统计（工具不存在）
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        updateToolStats(toolName, duration.count() / 1000.0, false);
        
        return std::nullopt;
    }

    const auto& tool = toolOpt.value();

    // 检查权限（如果启用）
    if (checkPermission) {
        if (!this->checkPermission(toolName, requiredPermission)) {
            ErrorInfo err;
            err.errorType = ErrorType::InvalidRequest;
            err.message = "Insufficient permission to execute tool '" + toolName + "'";
            
            if (error) {
                *error = err;
            }
            
            // 记录错误日志
            if (m_errorHandler) {
                m_errorHandler->log(ErrorHandler::LogLevel::Warning, 
                                   "Tool execution permission denied: " + err.message, 
                                   std::make_optional(err));
            }
            
            // 更新统计（权限不足）
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
            updateToolStats(toolName, duration.count() / 1000.0, false);
            
            return std::nullopt;
        }
    }

    // 验证参数
    ErrorInfo validationError;
    if (!validateArguments(tool, arguments, &validationError)) {
        if (error) {
            *error = validationError;
        }
        
        // 记录错误日志
        if (m_errorHandler) {
            m_errorHandler->log(ErrorHandler::LogLevel::Warning, 
                               "Tool argument validation failed: " + validationError.message, 
                               std::make_optional(validationError));
        }
        
        // 更新统计（参数验证失败）
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        updateToolStats(toolName, duration.count() / 1000.0, false);
        
        return std::nullopt;
    }

    // 执行工具
    try {
        nlohmann::json result = tool.handler(arguments);
        executionSuccess = true;
        
        // 更新统计（成功）
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        updateToolStats(toolName, duration.count() / 1000.0, true);
        
        return result;
    } catch (const std::exception& e) {
        ErrorInfo err;
        err.errorType = ErrorType::ServerError;
        err.message = "Tool execution failed: " + std::string(e.what());
        
        if (error) {
            *error = err;
        }
        
        // 记录错误日志
        if (m_errorHandler) {
            m_errorHandler->log(ErrorHandler::LogLevel::Error, 
                               "Tool execution exception: " + err.message, 
                               std::make_optional(err));
        }
        
        // 更新统计（执行异常）
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        updateToolStats(toolName, duration.count() / 1000.0, false);
        
        return std::nullopt;
    } catch (...) {
        ErrorInfo err;
        err.errorType = ErrorType::ServerError;
        err.message = "Tool execution failed: unknown error";
        
        if (error) {
            *error = err;
        }
        
        // 记录错误日志
        if (m_errorHandler) {
            m_errorHandler->log(ErrorHandler::LogLevel::Error, 
                               "Tool execution unknown error", 
                               std::make_optional(err));
        }
        
        // 更新统计（未知错误）
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        updateToolStats(toolName, duration.count() / 1000.0, false);
        
        return std::nullopt;
    }
}

bool ToolManager::validateArguments(const ToolDefinition& tool,
                                    const nlohmann::json& arguments,
                                    ErrorInfo* error) {
    // 参数必须是对象
    if (!arguments.is_object()) {
        if (error) {
            error->errorType = ErrorType::InvalidRequest;
            error->message = "Arguments must be a JSON object";
        }
        return false;
    }

    const auto& schema = tool.parametersSchema;

    // 检查必需字段
    if (schema.contains("required") && schema["required"].is_array()) {
        for (const auto& requiredField : schema["required"]) {
            if (requiredField.is_string()) {
                std::string fieldName = requiredField.get<std::string>();
                if (!arguments.contains(fieldName)) {
                    if (error) {
                        error->errorType = ErrorType::InvalidRequest;
                        error->message = "Missing required field: " + fieldName;
                    }
                    return false;
                }
            }
        }
    }

    // 验证每个字段的类型
    if (schema.contains("properties") && schema["properties"].is_object()) {
        const auto& properties = schema["properties"];
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            const std::string& fieldName = it.key();
            const auto& propertySchema = it.value();

            // 如果参数中包含该字段，验证其类型
            if (arguments.contains(fieldName)) {
                std::string validationError;
                if (!validatePropertyValue(arguments[fieldName], propertySchema, &validationError)) {
                    if (error) {
                        error->errorType = ErrorType::InvalidRequest;
                        error->message = "Invalid value for field '" + fieldName + "': " + validationError;
                    }
                    return false;
                }
            }
        }
    }

    return true;
}

bool ToolManager::validateSchemaFormat(const nlohmann::json& schema, std::string* errorMsg) {
    if (!schema.is_object()) {
        if (errorMsg) *errorMsg = "Schema must be a JSON object";
        return false;
    }

    // 检查 properties 字段（如果存在）
    if (schema.contains("properties")) {
        if (!schema["properties"].is_object()) {
            if (errorMsg) *errorMsg = "Schema 'properties' must be an object";
            return false;
        }
    }

    // 检查 required 字段（如果存在）
    if (schema.contains("required")) {
        if (!schema["required"].is_array()) {
            if (errorMsg) *errorMsg = "Schema 'required' must be an array";
            return false;
        }
    }

    // 检查 type 字段（如果存在）
    if (schema.contains("type")) {
        if (!schema["type"].is_string()) {
            if (errorMsg) *errorMsg = "Schema 'type' must be a string";
            return false;
        }
    }

    return true;
}

bool ToolManager::validatePropertyValue(const nlohmann::json& value,
                                        const nlohmann::json& propertySchema,
                                        std::string* errorMsg) {
    // 如果 Schema 中没有 type 字段，跳过类型检查
    if (!propertySchema.contains("type") || !propertySchema["type"].is_string()) {
        return true; // 允许任意类型
    }

    std::string typeStr = propertySchema["type"].get<std::string>();

    // 验证基本类型
    if (typeStr == "string") {
        if (!value.is_string()) {
            if (errorMsg) *errorMsg = "Expected string, got " + std::string(value.type_name());
            return false;
        }
        // 字符串类型验证通过，继续验证字符串长度等约束
    } else if (typeStr == "number") {
        if (!value.is_number()) {
            if (errorMsg) *errorMsg = "Expected number, got " + std::string(value.type_name());
            return false;
        }
    } else if (typeStr == "integer") {
        if (!value.is_number_integer()) {
            if (errorMsg) *errorMsg = "Expected integer, got " + std::string(value.type_name());
            return false;
        }
    } else if (typeStr == "boolean") {
        if (!value.is_boolean()) {
            if (errorMsg) *errorMsg = "Expected boolean, got " + std::string(value.type_name());
            return false;
        }
    } else if (typeStr == "object") {
        if (!value.is_object()) {
            if (errorMsg) *errorMsg = "Expected object, got " + std::string(value.type_name());
            return false;
        }
        // 递归验证嵌套对象的属性
        if (propertySchema.contains("properties") && propertySchema["properties"].is_object()) {
            const auto& nestedProperties = propertySchema["properties"];
            for (auto it = nestedProperties.begin(); it != nestedProperties.end(); ++it) {
                const std::string& nestedFieldName = it.key();
                const auto& nestedPropertySchema = it.value();
                if (value.contains(nestedFieldName)) {
                    std::string nestedError;
                    if (!validatePropertyValue(value[nestedFieldName], nestedPropertySchema, &nestedError)) {
                        if (errorMsg) *errorMsg = "Nested field '" + nestedFieldName + "': " + nestedError;
                        return false;
                    }
                }
            }
        }
    } else if (typeStr == "array") {
        if (!value.is_array()) {
            if (errorMsg) *errorMsg = "Expected array, got " + std::string(value.type_name());
            return false;
        }
        // 如果指定了 items schema，验证数组元素
        if (propertySchema.contains("items") && propertySchema["items"].is_object()) {
            const auto& itemsSchema = propertySchema["items"];
            for (size_t i = 0; i < value.size(); ++i) {
                std::string itemError;
                if (!validatePropertyValue(value[i], itemsSchema, &itemError)) {
                    if (errorMsg) *errorMsg = "Array element [" + std::to_string(i) + "]: " + itemError;
                    return false;
                }
            }
        }
    } else {
        // 未知类型，允许通过（扩展性）
        return true;
    }

    // 验证 enum（如果指定）- 适用于所有类型
    if (propertySchema.contains("enum") && propertySchema["enum"].is_array()) {
        bool found = false;
        for (const auto& enumValue : propertySchema["enum"]) {
            if (enumValue == value) {
                found = true;
                break;
            }
        }
        if (!found) {
            if (errorMsg) *errorMsg = "Value not in enum list";
            return false;
        }
    }

    // 验证数字范围（minimum/maximum）- 适用于number和integer类型
    if (value.is_number()) {
        if (propertySchema.contains("minimum")) {
            double minVal = propertySchema["minimum"].get<double>();
            double val = value.get<double>();
            if (val < minVal) {
                if (errorMsg) *errorMsg = "Value " + std::to_string(val) + " is less than minimum " + std::to_string(minVal);
                return false;
            }
        }
        if (propertySchema.contains("maximum")) {
            double maxVal = propertySchema["maximum"].get<double>();
            double val = value.get<double>();
            if (val > maxVal) {
                if (errorMsg) *errorMsg = "Value " + std::to_string(val) + " is greater than maximum " + std::to_string(maxVal);
                return false;
            }
        }
    }

    // 验证字符串长度（minLength/maxLength）和pattern - 适用于string类型
    if (typeStr == "string" && value.is_string()) {
        std::string str = value.get<std::string>();
        if (propertySchema.contains("minLength")) {
            // 支持number_integer和number_unsigned
            size_t minLen = 0;
            if (propertySchema["minLength"].is_number_unsigned()) {
                minLen = propertySchema["minLength"].get<size_t>();
            } else if (propertySchema["minLength"].is_number_integer()) {
                int64_t minLenInt = propertySchema["minLength"].get<int64_t>();
                if (minLenInt < 0) {
                    if (errorMsg) *errorMsg = "minLength cannot be negative";
                    return false;
                }
                minLen = static_cast<size_t>(minLenInt);
            }
            if (str.length() < minLen) {
                if (errorMsg) *errorMsg = "String length " + std::to_string(str.length()) + " is less than minLength " + std::to_string(minLen);
                return false;
            }
        }
        if (propertySchema.contains("maxLength")) {
            // 支持number_integer和number_unsigned
            size_t maxLen = 0;
            if (propertySchema["maxLength"].is_number_unsigned()) {
                maxLen = propertySchema["maxLength"].get<size_t>();
            } else if (propertySchema["maxLength"].is_number_integer()) {
                int64_t maxLenInt = propertySchema["maxLength"].get<int64_t>();
                if (maxLenInt < 0) {
                    if (errorMsg) *errorMsg = "maxLength cannot be negative";
                    return false;
                }
                maxLen = static_cast<size_t>(maxLenInt);
            }
            if (str.length() > maxLen) {
                if (errorMsg) *errorMsg = "String length " + std::to_string(str.length()) + " is greater than maxLength " + std::to_string(maxLen);
                return false;
            }
        }
        // 验证正则表达式pattern（可选）
        if (propertySchema.contains("pattern") && propertySchema["pattern"].is_string()) {
            try {
                std::string pattern = propertySchema["pattern"].get<std::string>();
                std::regex regexPattern(pattern);
                if (!std::regex_match(str, regexPattern)) {
                    if (errorMsg) *errorMsg = "String does not match pattern: " + pattern;
                    return false;
                }
            } catch (const std::regex_error&) {
                // 正则表达式无效，跳过验证（允许通过）
            }
        }
    }

    return true;
}

// ========== 权限控制 ==========

bool ToolManager::checkPermission(const std::string& toolName, PermissionLevel requiredLevel) const {
    auto toolOpt = getTool(toolName);
    if (!toolOpt.has_value()) {
        return false; // 工具不存在
    }

    const auto& tool = toolOpt.value();
    
    // 权限级别检查：Admin > Restricted > Public
    // 只有当 requiredLevel >= tool.permissionLevel 时才允许访问
    // Public(0) < Restricted(1) < Admin(2)
    // 所以：Public可以访问所有，Restricted可以访问Restricted和Admin，Admin只能访问Admin
    
    if (tool.permissionLevel == PermissionLevel::Public) {
        return true; // Public工具所有人都可以访问
    } else if (tool.permissionLevel == PermissionLevel::Restricted) {
        // Restricted工具需要Restricted或Admin权限
        return requiredLevel == PermissionLevel::Restricted || requiredLevel == PermissionLevel::Admin;
    } else { // Admin
        // Admin工具只能被Admin权限访问
        return requiredLevel == PermissionLevel::Admin;
    }
}

// ========== 工具过滤查询 ==========

std::vector<ToolDefinition> ToolManager::getToolsByPrefix(const std::string& prefix) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ToolDefinition> result;
    
    for (const auto& pair : m_tools) {
        if (pair.first.size() >= prefix.size() && 
            pair.first.substr(0, prefix.size()) == prefix) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

std::vector<ToolDefinition> ToolManager::getToolsByPermission(PermissionLevel level) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ToolDefinition> result;
    
    for (const auto& pair : m_tools) {
        if (pair.second.permissionLevel == level) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

std::vector<ToolDefinition> ToolManager::getFilteredTools(const ToolFilter& filter) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ToolDefinition> result;
    
    for (const auto& pair : m_tools) {
        bool matches = true;
        
        // 名称前缀过滤
        if (filter.namePrefix.has_value()) {
            const std::string& prefix = filter.namePrefix.value();
            if (pair.first.size() < prefix.size() || 
                pair.first.substr(0, prefix.size()) != prefix) {
                matches = false;
            }
        }
        
        // 权限级别过滤
        if (filter.permissionLevel.has_value() && matches) {
            if (pair.second.permissionLevel != filter.permissionLevel.value()) {
                matches = false;
            }
        }
        
        if (matches) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

// ========== 统计功能 ==========

void ToolManager::updateToolStats(const std::string& toolName, double executionTimeMs, bool success) {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    
    auto& stats = m_stats[toolName];
    stats.callCount++;
    stats.lastCallTime = std::chrono::system_clock::now();
    
    // 更新平均执行时间（使用移动平均）
    if (stats.callCount == 1) {
        stats.averageExecutionTimeMs = executionTimeMs;
    } else {
        // 移动平均：新平均值 = (旧平均值 * (n-1) + 新值) / n
        stats.averageExecutionTimeMs = 
            (stats.averageExecutionTimeMs * (stats.callCount - 1) + executionTimeMs) / stats.callCount;
    }
    
    // 更新错误统计
    if (!success) {
        stats.errorCount++;
    }
    
    // 计算错误率
    if (stats.callCount > 0) {
        stats.errorRate = static_cast<double>(stats.errorCount) / static_cast<double>(stats.callCount);
    }
}

std::optional<ToolUsageStats> ToolManager::getToolStats(const std::string& toolName) const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    auto it = m_stats.find(toolName);
    if (it != m_stats.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::unordered_map<std::string, ToolUsageStats> ToolManager::getAllToolStats() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_stats;
}

void ToolManager::resetToolStats(const std::string& toolName) {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    if (toolName.empty()) {
        // 重置所有统计
        m_stats.clear();
    } else {
        // 重置指定工具的统计
        m_stats.erase(toolName);
    }
}

// ========== ErrorHandler 设置 ==========

void ToolManager::setErrorHandler(ErrorHandler* errorHandler) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_errorHandler = errorHandler;
}

// ========== 工具与LLM集成 ==========

bool ToolManager::populateToolsToRequest(
    types::ChatRequest& request,
    const ToolFilter& filter,
    const std::string& toolChoice,
    ErrorInfo* error
) const {
    // 验证toolChoice参数
    if (toolChoice != "auto" && toolChoice != "none" && !toolChoice.empty()) {
        // 如果指定了特定工具名，验证工具是否存在
        if (!hasTool(toolChoice)) {
            if (error) {
                error->errorType = ErrorType::InvalidRequest;
                error->errorCode = 404;
                error->message = "Tool not found: " + toolChoice;
            }
            return false;
        }
    }

    // 获取工具列表（应用过滤条件）
    std::vector<nlohmann::json> tools;
    if (filter.namePrefix.has_value() || filter.permissionLevel.has_value()) {
        // 使用过滤条件
        tools = getToolsForAPI(filter);
    } else {
        // 获取所有工具
        tools = getToolsForAPI();
    }

    // 填充工具列表到请求
    request.tools = std::move(tools);

    // 设置工具选择策略
    // 注意：根据ChatRequest的定义，toolChoice是std::optional<std::string>类型
    // 对于特定工具名，直接使用工具名作为字符串（简化实现）
    // 如果需要完整的OpenAI格式（对象形式），需要在toJson()方法中特殊处理
    if (toolChoice == "none") {
        request.toolChoice = "none";
    } else if (!toolChoice.empty() && toolChoice != "auto") {
        // 特定工具名：直接使用工具名（简化实现）
        // 注意：这可能需要后续扩展以支持完整的OpenAI对象格式
        request.toolChoice = toolChoice;
    } else {
        // "auto" 或空字符串：让LLM自动决定
        request.toolChoice = "auto";
    }

    return true;
}

} // namespace naw::desktop_pet::service

