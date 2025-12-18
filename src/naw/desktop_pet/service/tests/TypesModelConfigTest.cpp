#include "naw/desktop_pet/service/types/ModelConfig.h"

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace naw::desktop_pet::service::types;

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

int main() {
    using mini_test::TestCase;

    std::vector<TestCase> tests;

    tests.push_back({"FromJsonSnakeCase", []() {
                         nlohmann::json j = {
                             {"model_id", "deepseek-ai/DeepSeek-V3"},
                             {"display_name", "DeepSeek V3"},
                             {"supported_tasks", {"CodeAnalysis", "TechnicalQnA"}},
                             {"max_context_tokens", 64000},
                             {"default_temperature", 0.7},
                             {"default_max_tokens", 4096},
                             {"cost_per_1k_tokens", 0.14},
                             {"max_concurrent_requests", 10},
                             {"supports_streaming", true},
                             {"performance_score", 0.95},
                         };
                         auto cfg = ModelConfig::fromJson(j);
                         CHECK_TRUE(cfg.has_value());
                         CHECK_EQ(cfg->modelId, "deepseek-ai/DeepSeek-V3");
                         CHECK_TRUE(cfg->supportsTask(TaskType::CodeAnalysis));
                         CHECK_FALSE(cfg->supportsTask(TaskType::BugFix));

                         std::vector<std::string> errs;
                         CHECK_TRUE(cfg->isValid(&errs));
                         CHECK_EQ(errs.size(), 0u);

                         auto out = cfg->toJson();
                         CHECK_TRUE(out.contains("model_id"));
                         CHECK_TRUE(out.contains("supported_tasks"));
                     }});

    tests.push_back({"FromJsonCamelCaseCompatibility", []() {
                         nlohmann::json j = {
                             {"modelId", "Qwen/Qwen2-72B-Instruct"},
                             {"displayName", "Qwen"},
                             {"supportedTasks", {"CasualChat"}},
                             {"maxContextTokens", 32768},
                             {"defaultTemperature", 0.8},
                             {"defaultMaxTokens", 2048},
                             {"costPer1kTokens", 0.6},
                             {"maxConcurrentRequests", 8},
                             {"supportsStreaming", true},
                             {"performanceScore", 0.9},
                         };
                         auto cfg = ModelConfig::fromJson(j);
                         CHECK_TRUE(cfg.has_value());
                         CHECK_EQ(cfg->modelId, "Qwen/Qwen2-72B-Instruct");
                         CHECK_TRUE(cfg->supportsTask(TaskType::CasualChat));

                         auto out = cfg->toJson();
                         CHECK_TRUE(out.contains("model_id"));
                         CHECK_FALSE(out.contains("modelId"));
                     }});

    tests.push_back({"IsValidDetectsIssues", []() {
                         ModelConfig cfg;
                         cfg.modelId = "";
                         cfg.maxContextTokens = 0;
                         cfg.maxConcurrentRequests = 0;
                         cfg.supportedTasks.clear();
                         cfg.performanceScore = 2.0f;
                         std::vector<std::string> errs;
                         CHECK_FALSE(cfg.isValid(&errs));
                         CHECK_TRUE(errs.size() >= 3u);
                     }});

    return mini_test::run(tests);
}

