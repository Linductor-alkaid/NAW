#include "naw/desktop_pet/service/types/TaskPriority.h"
#include "naw/desktop_pet/service/types/TaskType.h"

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <type_traits>

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

inline std::string toString(TaskType v) {
    using U = std::underlying_type_t<TaskType>;
    std::ostringstream oss;
    oss << static_cast<U>(v);
    return oss.str();
}

inline std::string toString(TaskPriority v) {
    using U = std::underlying_type_t<TaskPriority>;
    std::ostringstream oss;
    oss << static_cast<U>(v);
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

    tests.push_back({"TaskTypeRoundTrip", []() {
                         const std::vector<TaskType> all = {
                             TaskType::CasualChat,
                             TaskType::CodeDiscussion,
                             TaskType::TechnicalQnA,
                             TaskType::CodeGeneration,
                             TaskType::CodeAnalysis,
                             TaskType::CodeReview,
                             TaskType::CodeExplanation,
                             TaskType::BugFix,
                             TaskType::ProjectAnalysis,
                             TaskType::ArchitectureDesign,
                             TaskType::Documentation,
                             TaskType::AgentDecision,
                             TaskType::AgentReasoning,
                             TaskType::ContextUnderstanding,
                             TaskType::SpeechRecognition,
                             TaskType::SpeechSynthesis,
                             TaskType::VisionUnderstanding,
                             TaskType::SceneAnalysis,
                             TaskType::ProactiveResponse,
                             TaskType::ToolCalling,
                             TaskType::CodeToolExecution,
                         };

                         for (auto t : all) {
                             auto s = taskTypeToString(t);
                             auto back = stringToTaskType(s);
                             CHECK_TRUE(back.has_value());
                             CHECK_EQ(*back, t);
                             CHECK_FALSE(getTaskTypeDescription(t).empty());
                         }

                         CHECK_FALSE(stringToTaskType("NotAType").has_value());
                     }});

    tests.push_back({"TaskTypeClassification", []() {
                         CHECK_TRUE(isCodeRelatedTask(TaskType::CodeAnalysis));
                         CHECK_TRUE(isCodeRelatedTask(TaskType::ArchitectureDesign));
                         CHECK_FALSE(isCodeRelatedTask(TaskType::CasualChat));

                         CHECK_TRUE(isMultimodalTask(TaskType::SpeechRecognition));
                         CHECK_TRUE(isMultimodalTask(TaskType::VisionUnderstanding));
                         CHECK_FALSE(isMultimodalTask(TaskType::CodeGeneration));
                     }});

    tests.push_back({"TaskPriorityRoundTripAndCompare", []() {
                         const std::vector<TaskPriority> all = {
                             TaskPriority::Critical,
                             TaskPriority::High,
                             TaskPriority::Normal,
                             TaskPriority::Low,
                         };
                         for (auto p : all) {
                             auto s = taskPriorityToString(p);
                             auto back = stringToTaskPriority(s);
                             CHECK_TRUE(back.has_value());
                             CHECK_EQ(*back, p);
                         }

                         CHECK_TRUE(comparePriority(TaskPriority::Critical, TaskPriority::High));
                         CHECK_TRUE(comparePriority(TaskPriority::High, TaskPriority::Normal));
                         CHECK_FALSE(comparePriority(TaskPriority::Low, TaskPriority::Normal));
                     }});

    return mini_test::run(tests);
}

