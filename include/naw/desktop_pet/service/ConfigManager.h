#pragma once

#include "naw/desktop_pet/service/ErrorTypes.h"

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"

namespace naw::desktop_pet::service {

/**
 * @brief 配置管理器：加载/缓存/校验/环境变量覆盖
 *
 * 设计目标：
 * - 以 nlohmann::json 为底座，先满足基础设施层需要；后续 1.5 Types 落地后可再做强类型转换。
 * - 支持 key-path（a.b.c）读取/更新
 * - 支持 env 映射覆盖与 ${ENV_VAR} 占位符替换
 */
class ConfigManager {
public:
    ConfigManager();

    // 从文件加载；
    // - 文件存在：读取并解析
    // - 文件不存在：回退默认配置，并（可选）自动生成配置文件
    bool loadFromFile(const std::string& path, ErrorInfo* err = nullptr);

    // 从 JSON 字符串加载；解析失败返回 false，且不覆盖旧配置
    bool loadFromString(const std::string& jsonText, ErrorInfo* err = nullptr);

    // 获取完整配置的拷贝
    nlohmann::json getRaw() const;

    // 按 key-path 获取；不存在返回 nullopt
    std::optional<nlohmann::json> get(const std::string& keyPath) const;

    // 按 key-path 写入；中间节点不存在会自动创建 object
    bool set(const std::string& keyPath, const nlohmann::json& v, ErrorInfo* err = nullptr);

    // 应用环境变量覆盖（映射覆盖 + ${ENV_VAR} 替换）
    void applyEnvironmentOverrides();

    // 校验配置有效性；返回所有错误/警告（警告以 "WARN:" 前缀）
    std::vector<std::string> validate() const;

    // 读取并返回默认配置（最小可用）
    static nlohmann::json makeDefaultConfig();

    // 将当前配置写入文件（会创建父目录）。成功返回 true。
    bool saveToFile(const std::string& path, ErrorInfo* err = nullptr) const;

    // keyPath 命中敏感字段时，对 value 脱敏
    static std::string redactSensitive(const std::string& keyPath, const std::string& value);

private:
    mutable std::mutex m_mu;
    nlohmann::json m_cfg;

    static std::optional<std::string> getEnv(const std::string& name);
    static bool isSensitiveKeyPath(const std::string& keyPath);

    static std::vector<std::string> splitKeyPath(const std::string& keyPath);
    static const nlohmann::json* getPtrByPath(const nlohmann::json& root, const std::vector<std::string>& parts);
    static nlohmann::json* getOrCreatePtrByPath(nlohmann::json& root, const std::vector<std::string>& parts);

    static void applyEnvMappingOverrides(nlohmann::json& root);
    static void replaceEnvPlaceholdersRecursive(nlohmann::json& node);
    static std::string replaceEnvPlaceholdersInString(const std::string& s);

    static bool startsWith(const std::string& s, const std::string& prefix);
};

} // namespace naw::desktop_pet::service

