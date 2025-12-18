#pragma once

#include "naw/desktop_pet/service/ErrorTypes.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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
    ~ConfigManager();

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

    // =========================
    // Hot Reload (optional)
    // =========================
    struct WatchOptions {
        // 轮询间隔（越小越实时，但更耗资源）
        std::chrono::milliseconds pollInterval{250};
        // 防抖时间：检测到文件变化后，等待一段时间确保写入完成
        std::chrono::milliseconds debounce{300};
    };

    using ReloadCallback = std::function<void(const nlohmann::json& newConfig,
                                              const std::vector<std::string>& validationIssues)>;

    // 启动对指定配置文件的热重载监控；若已在监控，则会先停止旧监控再启动新的
    bool startWatchingFile(const std::string& path, const WatchOptions& opt, ReloadCallback cb, ErrorInfo* err = nullptr);
    void stopWatching();
    bool isWatching() const;

    // 最近一次热重载失败原因（为空表示最近一次成功或尚未发生失败）
    std::string getLastReloadError() const;

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
    static std::vector<std::string> validateJson(const nlohmann::json& cfgCopy);
    static bool hasHardValidationErrors(const std::vector<std::string>& issues);

    static bool startsWith(const std::string& s, const std::string& prefix);

    // Watcher runtime
    mutable std::mutex m_watchMu;
    std::condition_variable m_watchCv;
    std::thread m_watchThread;
    std::atomic<bool> m_watchStop{false};
    bool m_watching{false};
    std::string m_watchPath;
    WatchOptions m_watchOpt{};
    ReloadCallback m_reloadCb{};
    std::filesystem::file_time_type m_lastWriteTime{};
    std::string m_lastReloadError;
};

} // namespace naw::desktop_pet::service

