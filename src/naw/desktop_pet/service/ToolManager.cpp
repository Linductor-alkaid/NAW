#include "naw/desktop_pet/service/ToolManager.h"

#include "naw/desktop_pet/service/ErrorHandler.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace naw::desktop_pet::service {

// ========== ToolDefinition ==========

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

ToolManager::ToolManager() = default;

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

size_t ToolManager::getToolCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tools.size();
}

std::optional<nlohmann::json> ToolManager::executeTool(const std::string& toolName,
                                                        const nlohmann::json& arguments,
                                                        ErrorInfo* error) {
    // 查找工具
    auto toolOpt = getTool(toolName);
    if (!toolOpt.has_value()) {
        if (error) {
            error->errorType = ErrorType::InvalidRequest;
            error->message = "Tool '" + toolName + "' not found";
        }
        return std::nullopt;
    }

    const auto& tool = toolOpt.value();

    // 验证参数
    ErrorInfo validationError;
    if (!validateArguments(tool, arguments, &validationError)) {
        if (error) {
            *error = validationError;
        }
        return std::nullopt;
    }

    // 执行工具
    try {
        nlohmann::json result = tool.handler(arguments);
        return result;
    } catch (const std::exception& e) {
        if (error) {
            error->errorType = ErrorType::ServerError;
            error->message = "Tool execution failed: " + std::string(e.what());
        }
        return std::nullopt;
    } catch (...) {
        if (error) {
            error->errorType = ErrorType::ServerError;
            error->message = "Tool execution failed: unknown error";
        }
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

    return true;
}

} // namespace naw::desktop_pet::service

