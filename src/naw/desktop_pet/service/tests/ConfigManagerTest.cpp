#include "naw/desktop_pet/service/ConfigManager.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <condition_variable>
#include <chrono>
#include <vector>

using namespace naw::desktop_pet::service;

// 轻量自测断言工具（复用 ErrorHandlerTest 风格）
namespace mini_test {

inline std::string toString(const std::string& v) { return v; }
inline std::string toString(const char* v) { return v ? std::string(v) : "null"; }
inline std::string toString(bool v) { return v ? "true" : "false"; }

template <typename T>
std::string toString(const T& v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

class AssertionFailed : public std::runtime_error {
public:
    explicit AssertionFailed(const std::string& msg) : std::runtime_error(msg) {}
};

#define CHECK_TRUE(cond)                                                                          \
    do {                                                                                          \
        if (!(cond))                                                                              \
            throw mini_test::AssertionFailed(std::string("CHECK_TRUE failed: ") + #cond);         \
    } while (0)

#define CHECK_FALSE(cond) CHECK_TRUE(!(cond))

#define CHECK_EQ(a, b)                                                                            \
    do {                                                                                          \
        const auto _va = (a);                                                                     \
        const auto _vb = (b);                                                                     \
        if (!(_va == _vb)) {                                                                      \
            throw mini_test::AssertionFailed(std::string("CHECK_EQ failed: ") + #a " vs " #b +    \
                                             " (" + mini_test::toString(_va) + " vs " +           \
                                             mini_test::toString(_vb) + ")");                     \
        }                                                                                         \
    } while (0)

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline int run(const std::vector<TestCase>& tests) {
    int failed = 0;
    for (const auto& t : tests) {
        try {
            t.fn();
            std::cout << "[  OK  ] " << t.name << "\n";
        } catch (const AssertionFailed& e) {
            failed++;
            std::cout << "[ FAIL ] " << t.name << " :: " << e.what() << "\n";
        } catch (const std::exception& e) {
            failed++;
            std::cout << "[ EXC  ] " << t.name << " :: " << e.what() << "\n";
        } catch (...) {
            failed++;
            std::cout << "[ EXC  ] " << t.name << " :: unknown exception\n";
        }
    }
    std::cout << "Executed " << tests.size() << " cases, failed " << failed << ".\n";
    return failed == 0 ? 0 : 1;
}

} // namespace mini_test

static bool containsAny(const std::vector<std::string>& xs, const std::string& needle) {
    for (const auto& x : xs) {
        if (x.find(needle) != std::string::npos) return true;
    }
    return false;
}

static void setEnvVar(const std::string& k, const std::string& v) {
#if defined(_WIN32)
    _putenv_s(k.c_str(), v.c_str());
#else
    setenv(k.c_str(), v.c_str(), 1);
#endif
}

static void unsetEnvVar(const std::string& k) {
#if defined(_WIN32)
    _putenv_s(k.c_str(), "");
#else
    unsetenv(k.c_str());
#endif
}

int main() {
    using mini_test::TestCase;

    std::vector<TestCase> tests;

    tests.push_back({"load_from_string_and_get_set", []() {
        ConfigManager cm;
        ErrorInfo err;
        const std::string txt = R"({"api":{"base_url":"https://api.siliconflow.cn/v1","api_key":"k","default_timeout_ms":123},"models":[]})";
        CHECK_TRUE(cm.loadFromString(txt, &err));

        auto v = cm.get("api.base_url");
        CHECK_TRUE(v.has_value());
        CHECK_TRUE(v->is_string());
        CHECK_EQ(v->get<std::string>(), "https://api.siliconflow.cn/v1");

        CHECK_TRUE(cm.set("api.default_timeout_ms", 456, &err));
        auto t = cm.get("api.default_timeout_ms");
        CHECK_TRUE(t.has_value());
        CHECK_TRUE(t->is_number_integer());
        CHECK_EQ(t->get<int>(), 456);
    }});

    tests.push_back({"parse_error_does_not_overwrite_old_config", []() {
        ConfigManager cm;
        ErrorInfo err;
        CHECK_TRUE(cm.loadFromString(R"({"api":{"base_url":"https://a","api_key":"k"}})", &err));

        const auto before = cm.getRaw().dump();
        CHECK_FALSE(cm.loadFromString(R"({"api":)", &err));
        const auto after = cm.getRaw().dump();
        CHECK_EQ(before, after);
    }});

    tests.push_back({"load_missing_file_falls_back_to_default", []() {
        ConfigManager cm;
        ErrorInfo err;
        const std::string path = "this_file_should_not_exist_123456789.json";
        // 清理可能残留
        std::error_code ec;
        std::filesystem::remove(path, ec);

        CHECK_TRUE(cm.loadFromFile(path, &err));
        auto api = cm.get("api");
        CHECK_TRUE(api.has_value());
        CHECK_TRUE(api->is_object());
        CHECK_TRUE(!err.message.empty());

        // 应自动生成文件（模板）
        CHECK_TRUE(std::filesystem::exists(path));
        std::ifstream ifs(path, std::ios::in | std::ios::binary);
        CHECK_TRUE(ifs.is_open());
        std::stringstream buf;
        buf << ifs.rdbuf();
        const auto j = nlohmann::json::parse(buf.str());
        CHECK_TRUE(j.is_object());
        CHECK_TRUE(j.contains("api"));
        CHECK_TRUE(j["api"].contains("api_key"));
        // 写盘时不应写入明文 key，应保留占位符
        CHECK_EQ(j["api"]["api_key"].get<std::string>(), "${SILICONFLOW_API_KEY}");

        // 测试结束清理
        std::filesystem::remove(path, ec);
    }});

    tests.push_back({"auto_create_default_config_in_config_dir", []() {
        ConfigManager cm;
        ErrorInfo err;

        const std::string path = "config/ai_service_config.json";
        std::error_code ec;
        // 先确保文件不存在
        std::filesystem::remove(path, ec);

        CHECK_TRUE(cm.loadFromFile(path, &err));
        CHECK_TRUE(std::filesystem::exists(path));

        std::ifstream ifs(path, std::ios::in | std::ios::binary);
        CHECK_TRUE(ifs.is_open());
        std::stringstream buf;
        buf << ifs.rdbuf();
        const auto j = nlohmann::json::parse(buf.str());
        CHECK_TRUE(j.is_object());
        CHECK_TRUE(j.contains("api"));
        CHECK_TRUE(j["api"].contains("api_key"));
        CHECK_EQ(j["api"]["api_key"].get<std::string>(), "${SILICONFLOW_API_KEY}");
    }});

    tests.push_back({"validate_catches_missing_api_key", []() {
        ConfigManager cm;
        ErrorInfo err;
        CHECK_TRUE(cm.loadFromString(R"({"api":{"base_url":"https://api.siliconflow.cn/v1","api_key":""},"models":[]})", &err));
        const auto issues = cm.validate();
        CHECK_TRUE(containsAny(issues, "api.api_key"));
    }});

    tests.push_back({"env_placeholder_replacement", []() {
        unsetEnvVar("TEST_PLACEHOLDER_KEY");
        setEnvVar("TEST_PLACEHOLDER_KEY", "abc123");

        ConfigManager cm;
        ErrorInfo err;
        CHECK_TRUE(cm.loadFromString(
            R"({"api":{"base_url":"https://api.siliconflow.cn/v1","api_key":"${TEST_PLACEHOLDER_KEY}","default_timeout_ms":1},"models":[]})",
            &err));
        auto v = cm.get("api.api_key");
        CHECK_TRUE(v.has_value());
        CHECK_EQ(v->get<std::string>(), "abc123");
    }});

    tests.push_back({"env_mapping_override_api_key", []() {
        setEnvVar("SILICONFLOW_API_KEY", "override_key");

        ConfigManager cm;
        ErrorInfo err;
        // even if json provides a different key, env mapping should override
        CHECK_TRUE(cm.loadFromString(
            R"({"api":{"base_url":"https://api.siliconflow.cn/v1","api_key":"json_key","default_timeout_ms":1},"models":[]})",
            &err));
        auto v = cm.get("api.api_key");
        CHECK_TRUE(v.has_value());
        CHECK_EQ(v->get<std::string>(), "override_key");
    }});

    tests.push_back({"validate_routing_task_key_must_be_tasktype", []() {
        ConfigManager cm;
        ErrorInfo err;
        CHECK_TRUE(cm.loadFromString(
            R"({"api":{"base_url":"https://api.siliconflow.cn/v1","api_key":"k","default_timeout_ms":1},)"
            R"("models":[{"model_id":"m1","supported_tasks":[]}],)"
            R"("routing":{"default_model_per_task":{"NotATask":"m1"}}})",
            &err));
        const auto issues = cm.validate();
        CHECK_TRUE(containsAny(issues, "Invalid routing task type key"));
    }});

    tests.push_back({"hot_reload_success_and_rollback", []() {
        ConfigManager cm;
        ErrorInfo err;

        const std::string path = "hot_reload_test_config.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);

        // initial valid config
        {
            std::ofstream ofs(path, std::ios::out | std::ios::binary | std::ios::trunc);
            CHECK_TRUE(ofs.is_open());
            ofs << R"({"api":{"base_url":"https://api.siliconflow.cn/v1","api_key":"k","default_timeout_ms":1},"models":[]})";
        }
        CHECK_TRUE(cm.loadFromFile(path, &err));

        std::mutex mu;
        std::condition_variable cv;
        int callbacks = 0;

        ConfigManager::WatchOptions opt;
        opt.pollInterval = std::chrono::milliseconds(30);
        opt.debounce = std::chrono::milliseconds(30);

        CHECK_TRUE(cm.startWatchingFile(path, opt,
                                       [&](const nlohmann::json& newCfg, const std::vector<std::string>&) {
                                           (void)newCfg;
                                           std::lock_guard<std::mutex> lk(mu);
                                           callbacks++;
                                           cv.notify_all();
                                       },
                                       &err));

        // modify to a new valid config
        {
            std::ofstream ofs(path, std::ios::out | std::ios::binary | std::ios::trunc);
            CHECK_TRUE(ofs.is_open());
            ofs << R"({"api":{"base_url":"https://changed","api_key":"k","default_timeout_ms":1},"models":[]})";
        }

        {
            std::unique_lock<std::mutex> lk(mu);
            CHECK_TRUE(cv.wait_for(lk, std::chrono::seconds(2), [&] { return callbacks >= 1; }));
        }
        auto baseUrl = cm.get("api.base_url");
        CHECK_TRUE(baseUrl.has_value());
        CHECK_EQ(baseUrl->get<std::string>(), "https://changed");

        // now write invalid json, should rollback (keep old config)
        {
            std::ofstream ofs(path, std::ios::out | std::ios::binary | std::ios::trunc);
            CHECK_TRUE(ofs.is_open());
            ofs << R"({"api":)";
        }

        // wait a bit to let watcher process
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        auto baseUrl2 = cm.get("api.base_url");
        CHECK_TRUE(baseUrl2.has_value());
        CHECK_EQ(baseUrl2->get<std::string>(), "https://changed");
        CHECK_TRUE(!cm.getLastReloadError().empty());

        cm.stopWatching();
        std::filesystem::remove(path, ec);
    }});

    tests.push_back({"redact_sensitive", []() {
        CHECK_EQ(ConfigManager::redactSensitive("api.api_key", "abcd1234"), "******");
        const auto r = ConfigManager::redactSensitive("api.api_key", "abcd1234567890");
        CHECK_TRUE(r.find("******") != std::string::npos);
        CHECK_TRUE(r.find("abcd") == std::string::npos); // 不应原样出现
    }});

    return mini_test::run(tests);
}

