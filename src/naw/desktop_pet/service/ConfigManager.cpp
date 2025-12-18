#include "naw/desktop_pet/service/ConfigManager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace naw::desktop_pet::service {

static std::string trimCopy(std::string s) {
    auto notSpace = [](int ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

ConfigManager::ConfigManager()
    : m_cfg(makeDefaultConfig())
{}

bool ConfigManager::loadFromFile(const std::string& path, ErrorInfo* err) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        // 1) 回退默认配置（内存）
        {
            std::lock_guard<std::mutex> lk(m_mu);
            m_cfg = makeDefaultConfig();
            applyEnvMappingOverrides(m_cfg);
            replaceEnvPlaceholdersRecursive(m_cfg);
        }

        // 2) 自动生成配置文件（落盘）。注意：落盘的是“模板/默认值”，仍保持 ${SILICONFLOW_API_KEY} 占位符。
        //    若用户本地已设置环境变量，内存中的 m_cfg 可能已被替换为明文；因此这里必须重新生成一份“未替换的模板”写盘。
        {
            ErrorInfo saveErr;
            ConfigManager tmp;
            // tmp 构造会加载默认配置，但不会应用 env。我们用默认配置直接写盘。
            (void)tmp.saveToFile(path, &saveErr);
            if (err && saveErr.errorCode != 0) {
                // 合并提示，不影响 loadFromFile 的整体成功语义
                if (!err->details.has_value()) err->details = nlohmann::json::object();
                (*err->details)["auto_create_failed"] = saveErr.toJson();
            }
        }

        if (err) {
            err->errorType = ErrorType::UnknownError;
            err->errorCode = 0;
            err->message = "Config file not found, using default config and auto-created template: " + path;
            err->details = nlohmann::json{
                {"path", path},
                {"fallback", "default_config"},
                {"auto_created", true}
            };
        }
        return true;
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    return loadFromString(buffer.str(), err);
}

bool ConfigManager::loadFromString(const std::string& jsonText, ErrorInfo* err) {
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(jsonText);
    } catch (const std::exception& e) {
        if (err) {
            err->errorType = ErrorType::InvalidRequest;
            err->errorCode = 0;
            err->message = std::string("Config JSON parse failed: ") + e.what();
            err->details = nlohmann::json{{"snippet", jsonText.substr(0, 256)}};
        }
        return false;
    }

    if (!parsed.is_object()) {
        if (err) {
            err->errorType = ErrorType::InvalidRequest;
            err->errorCode = 0;
            err->message = "Config root must be a JSON object";
        }
        return false;
    }

    // 在锁外处理 env 逻辑，避免长期占用
    applyEnvMappingOverrides(parsed);
    replaceEnvPlaceholdersRecursive(parsed);

    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_cfg = std::move(parsed);
    }
    return true;
}

nlohmann::json ConfigManager::getRaw() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_cfg;
}

bool ConfigManager::saveToFile(const std::string& path, ErrorInfo* err) const {
    try {
        const std::filesystem::path p(path);
        const auto parent = p.parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            // 若创建失败且目录仍不存在，视为失败
            if (ec && !std::filesystem::exists(parent)) {
                if (err) {
                    err->errorType = ErrorType::UnknownError;
                    err->errorCode = 1;
                    err->message = "Failed to create config directory: " + parent.string();
                    err->details = nlohmann::json{{"path", path}, {"ec", ec.value()}, {"what", ec.message()}};
                }
                return false;
            }
        }

        std::ofstream ofs(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            if (err) {
                err->errorType = ErrorType::UnknownError;
                err->errorCode = 2;
                err->message = "Failed to open config file for write: " + path;
                err->details = nlohmann::json{{"path", path}};
            }
            return false;
        }

        // 写盘时避免写入 env 替换后的敏感信息：始终以默认模板为准
        const auto tmpl = makeDefaultConfig();
        ofs << tmpl.dump(2) << "\n";
        ofs.flush();
        return true;
    } catch (const std::exception& e) {
        if (err) {
            err->errorType = ErrorType::UnknownError;
            err->errorCode = 3;
            err->message = std::string("Failed to save config file: ") + e.what();
            err->details = nlohmann::json{{"path", path}};
        }
        return false;
    }
}

std::optional<nlohmann::json> ConfigManager::get(const std::string& keyPath) const {
    const auto parts = splitKeyPath(keyPath);
    if (parts.empty()) return std::nullopt;

    std::lock_guard<std::mutex> lk(m_mu);
    const nlohmann::json* p = getPtrByPath(m_cfg, parts);
    if (!p) return std::nullopt;
    // 显式构造 optional，避免 MSVC 下 json -> optional<json> 隐式转换失败
    return std::optional<nlohmann::json>{*p};
}

bool ConfigManager::set(const std::string& keyPath, const nlohmann::json& v, ErrorInfo* err) {
    const auto parts = splitKeyPath(keyPath);
    if (parts.empty()) {
        if (err) {
            err->errorType = ErrorType::InvalidRequest;
            err->errorCode = 0;
            err->message = "Empty keyPath";
        }
        return false;
    }

    std::lock_guard<std::mutex> lk(m_mu);
    nlohmann::json* p = getOrCreatePtrByPath(m_cfg, parts);
    if (!p) {
        if (err) {
            err->errorType = ErrorType::InvalidRequest;
            err->errorCode = 0;
            err->message = "Failed to create keyPath: " + keyPath;
        }
        return false;
    }
    *p = v;
    return true;
}

void ConfigManager::applyEnvironmentOverrides() {
    std::lock_guard<std::mutex> lk(m_mu);
    applyEnvMappingOverrides(m_cfg);
    replaceEnvPlaceholdersRecursive(m_cfg);
}

std::vector<std::string> ConfigManager::validate() const {
    nlohmann::json cfgCopy = getRaw();
    std::vector<std::string> out;

    // api
    if (!cfgCopy.contains("api") || !cfgCopy["api"].is_object()) {
        out.push_back("Missing or invalid 'api' object");
        return out;
    }
    const auto& api = cfgCopy["api"];
    if (!api.contains("base_url") || !api["base_url"].is_string() || trimCopy(api["base_url"].get<std::string>()).empty()) {
        out.push_back("Missing or invalid 'api.base_url' (string required)");
    } else {
        const auto baseUrl = trimCopy(api["base_url"].get<std::string>());
        if (!(startsWith(baseUrl, "http://") || startsWith(baseUrl, "https://"))) {
            out.push_back("Invalid 'api.base_url' (must start with http:// or https://)");
        }
    }

    if (!api.contains("api_key") || !api["api_key"].is_string()) {
        out.push_back("Missing or invalid 'api.api_key' (string required)");
    } else {
        const auto key = trimCopy(api["api_key"].get<std::string>());
        if (key.empty()) {
            out.push_back("Invalid 'api.api_key' (empty)");
        } else if (startsWith(key, "${") && key.find('}') != std::string::npos) {
            // 仍为占位符，通常意味着 env 未提供
            out.push_back("Invalid 'api.api_key' (unresolved env placeholder): " + redactSensitive("api.api_key", key));
        }
    }

    if (api.contains("default_timeout_ms")) {
        if (!api["default_timeout_ms"].is_number_integer()) {
            out.push_back("Invalid 'api.default_timeout_ms' (integer required)");
        } else {
            const auto t = api["default_timeout_ms"].get<long long>();
            if (t <= 0 || t > 300000) out.push_back("Invalid 'api.default_timeout_ms' (range 1..300000)");
        }
    }

    // models
    std::set<std::string> modelIds;
    if (cfgCopy.contains("models")) {
        if (!cfgCopy["models"].is_array()) {
            out.push_back("Invalid 'models' (array required)");
        } else {
            for (size_t i = 0; i < cfgCopy["models"].size(); ++i) {
                const auto& m = cfgCopy["models"][i];
                if (!m.is_object()) {
                    out.push_back("Invalid 'models[" + std::to_string(i) + "]' (object required)");
                    continue;
                }
                if (!m.contains("model_id") || !m["model_id"].is_string() || trimCopy(m["model_id"].get<std::string>()).empty()) {
                    out.push_back("Missing or invalid 'models[" + std::to_string(i) + "].model_id'");
                } else {
                    modelIds.insert(trimCopy(m["model_id"].get<std::string>()));
                }
                if (!m.contains("supported_tasks") || !m["supported_tasks"].is_array()) {
                    out.push_back("Missing or invalid 'models[" + std::to_string(i) + "].supported_tasks' (array required)");
                }
            }
        }
    } else {
        out.push_back("Missing 'models' (array required)");
    }

    // routing.default_model_per_task
    if (cfgCopy.contains("routing")) {
        if (!cfgCopy["routing"].is_object()) {
            out.push_back("Invalid 'routing' (object required)");
        } else if (cfgCopy["routing"].contains("default_model_per_task")) {
            const auto& d = cfgCopy["routing"]["default_model_per_task"];
            if (!d.is_object()) {
                out.push_back("Invalid 'routing.default_model_per_task' (object required)");
            } else {
                for (auto it = d.begin(); it != d.end(); ++it) {
                    if (!it.value().is_string()) {
                        out.push_back("Invalid routing mapping for task '" + it.key() + "' (string model_id required)");
                        continue;
                    }
                    const auto mid = trimCopy(it.value().get<std::string>());
                    if (!mid.empty() && !modelIds.empty() && modelIds.find(mid) == modelIds.end()) {
                        out.push_back("WARN: routing.default_model_per_task[" + it.key() + "] refers to unknown model_id: " + mid);
                    }
                }
            }
        }
    }

    return out;
}

nlohmann::json ConfigManager::makeDefaultConfig() {
    // 最小可用：api + models + routing + tools
    nlohmann::json j;
    j["_comment"] = "NAW System AI Service config template (auto-generated). JSON has no comments; use _comment fields.";
    j["api"] = {
        {"_comment", "api_key is recommended to be injected via env var, avoid plaintext on disk."},
        {"base_url", "https://api.siliconflow.cn/v1"},
        {"api_key", "${SILICONFLOW_API_KEY}"},
        {"default_timeout_ms", 30000}
    };
    j["models"] = nlohmann::json::array({
        {
            {"model_id", "deepseek-ai/DeepSeek-V3"},
            {"display_name", "DeepSeek V3"},
            {"supported_tasks", nlohmann::json::array({"CodeGeneration", "CodeAnalysis", "TechnicalQnA"})},
            {"supports_streaming", true}
        }
    });
    j["routing"] = {
        {"default_model_per_task", {{"CodeGeneration", "deepseek-ai/DeepSeek-V3"}}},
        {"fallback_model", "deepseek-ai/DeepSeek-V3"}
    };
    j["tools"] = {
        {"project_root", "${PROJECT_ROOT}"}
    };
    return j;
}

std::string ConfigManager::redactSensitive(const std::string& keyPath, const std::string& value) {
    if (!isSensitiveKeyPath(keyPath)) return value;
    const auto v = trimCopy(value);
    if (v.size() <= 8) return "******";
    return v.substr(0, 2) + "******" + v.substr(v.size() - 2);
}

std::optional<std::string> ConfigManager::getEnv(const std::string& name) {
    if (name.empty()) return std::nullopt;
#if defined(_WIN32)
    // MSVC: getenv 会触发 C4996，改用 _dupenv_s
    char* buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, name.c_str()) != 0 || !buf) return std::nullopt;
    std::string s = buf;
    std::free(buf);
#else
    const char* v = std::getenv(name.c_str());
    if (!v) return std::nullopt;
    std::string s = v;
#endif
    if (s.empty()) return std::nullopt;
    return s;
}

bool ConfigManager::isSensitiveKeyPath(const std::string& keyPath) {
    std::string low = keyPath;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return low.find("api_key") != std::string::npos || low.find("apikey") != std::string::npos || low.find("secret") != std::string::npos;
}

std::vector<std::string> ConfigManager::splitKeyPath(const std::string& keyPath) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : keyPath) {
        if (c == '.') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

const nlohmann::json* ConfigManager::getPtrByPath(const nlohmann::json& root, const std::vector<std::string>& parts) {
    const nlohmann::json* p = &root;
    for (const auto& k : parts) {
        if (!p->is_object()) return nullptr;
        if (!p->contains(k)) return nullptr;
        p = &((*p)[k]);
    }
    return p;
}

nlohmann::json* ConfigManager::getOrCreatePtrByPath(nlohmann::json& root, const std::vector<std::string>& parts) {
    nlohmann::json* p = &root;
    for (size_t i = 0; i < parts.size(); ++i) {
        const auto& k = parts[i];
        if (!p->is_object()) {
            *p = nlohmann::json::object();
        }
        if (i == parts.size() - 1) {
            return &((*p)[k]);
        }
        p = &((*p)[k]);
    }
    return p;
}

void ConfigManager::applyEnvMappingOverrides(nlohmann::json& root) {
    // 固定映射：env -> keyPath
    struct MapItem {
        const char* env;
        const char* keyPath;
    };
    const MapItem mapping[] = {
        {"SILICONFLOW_API_KEY", "api.api_key"},
        {"SILICONFLOW_BASE_URL", "api.base_url"},
        {"PROJECT_ROOT", "tools.project_root"},
    };

    for (const auto& m : mapping) {
        auto v = getEnv(m.env);
        if (!v.has_value()) continue;
        // 空字符串视为“未提供”
        const auto val = trimCopy(v.value());
        if (val.empty()) continue;
        const auto parts = splitKeyPath(m.keyPath);
        nlohmann::json* p = getOrCreatePtrByPath(root, parts);
        if (p) *p = val;
    }
}

void ConfigManager::replaceEnvPlaceholdersRecursive(nlohmann::json& node) {
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            replaceEnvPlaceholdersRecursive(it.value());
        }
        return;
    }
    if (node.is_array()) {
        for (auto& v : node) {
            replaceEnvPlaceholdersRecursive(v);
        }
        return;
    }
    if (node.is_string()) {
        node = replaceEnvPlaceholdersInString(node.get<std::string>());
    }
}

std::string ConfigManager::replaceEnvPlaceholdersInString(const std::string& s) {
    // 替换 ${ENV_NAME} 形式的占位符；未找到 env 时保留原样
    std::string out;
    out.reserve(s.size());

    for (size_t i = 0; i < s.size();) {
        if (i + 2 < s.size() && s[i] == '$' && s[i + 1] == '{') {
            const auto end = s.find('}', i + 2);
            if (end != std::string::npos) {
                const auto name = s.substr(i + 2, end - (i + 2));
                auto v = getEnv(name);
                if (v.has_value()) {
                    out += v.value();
                } else {
                    out += s.substr(i, end - i + 1);
                }
                i = end + 1;
                continue;
            }
        }
        out.push_back(s[i]);
        i++;
    }
    return out;
}

bool ConfigManager::startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

} // namespace naw::desktop_pet::service

