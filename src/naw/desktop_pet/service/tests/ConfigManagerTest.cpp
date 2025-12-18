#include "naw/desktop_pet/service/ConfigManager.h"

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
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
        CHECK_TRUE(cm.loadFromFile("this_file_should_not_exist_123456789.json", &err));
        auto api = cm.get("api");
        CHECK_TRUE(api.has_value());
        CHECK_TRUE(api->is_object());
        CHECK_TRUE(!err.message.empty());
    }});

    tests.push_back({"validate_catches_missing_api_key", []() {
        ConfigManager cm;
        ErrorInfo err;
        CHECK_TRUE(cm.loadFromString(R"({"api":{"base_url":"https://api.siliconflow.cn/v1","api_key":""},"models":[]})", &err));
        const auto issues = cm.validate();
        CHECK_TRUE(containsAny(issues, "api.api_key"));
    }});

    tests.push_back({"redact_sensitive", []() {
        CHECK_EQ(ConfigManager::redactSensitive("api.api_key", "abcd1234"), "******");
        const auto r = ConfigManager::redactSensitive("api.api_key", "abcd1234567890");
        CHECK_TRUE(r.find("******") != std::string::npos);
        CHECK_TRUE(r.find("abcd") == std::string::npos); // 不应原样出现
    }});

    return mini_test::run(tests);
}

