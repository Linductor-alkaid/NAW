#include "naw/desktop_pet/service/types/RequestResponse.h"

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

    tests.push_back({"ToolJsonOpenAIFormat", []() {
                         Tool t;
                         t.name = "read_file";
                         t.description = "read";
                         t.parameters = nlohmann::json{{"type", "object"}};

                         auto j = t.toJson();
                         CHECK_TRUE(j.contains("type"));
                         CHECK_EQ(j["type"].get<std::string>(), "function");
                         CHECK_TRUE(j.contains("function"));
                         CHECK_EQ(j["function"]["name"].get<std::string>(), "read_file");
                     }});

    tests.push_back({"ChatRequestJsonSnakeCaseOutput", []() {
                         ChatRequest r;
                         r.model = "deepseek-ai/DeepSeek-V3";
                         r.messages.push_back(ChatMessage{MessageRole::User, "hi"});
                         r.maxTokens = 123;
                         r.topP = 0.9f;
                         r.toolChoice = "auto";

                         auto j = r.toJson();
                         CHECK_TRUE(j.contains("max_tokens"));
                         CHECK_FALSE(j.contains("maxTokens"));
                         CHECK_TRUE(j.contains("tool_choice"));
                         CHECK_FALSE(j.contains("toolChoice"));
                     }});

    tests.push_back({"ChatRequestFromJsonCamelCaseCompatibility", []() {
                         nlohmann::json j = {
                             {"model", "deepseek-ai/DeepSeek-V3"},
                             {"messages", nlohmann::json::array({{{"role", "user"}, {"content", "hi"}}})},
                             {"maxTokens", 10},
                             {"topP", 0.8},
                             {"toolChoice", "none"},
                         };
                         auto r = ChatRequest::fromJson(j);
                         CHECK_TRUE(r.has_value());
                         CHECK_TRUE(r->maxTokens.has_value());
                         CHECK_EQ(*r->maxTokens, 10u);
                         CHECK_TRUE(r->topP.has_value());
                         CHECK_TRUE(r->toolChoice.has_value());
                         CHECK_EQ(*r->toolChoice, "none");
                     }});

    tests.push_back({"ChatResponseFromOpenAIShapeWithToolCalls", []() {
                         nlohmann::json j = {
                             {"model", "x"},
                             {"choices",
                              nlohmann::json::array(
                                  {{{"finish_reason", "tool_calls"},
                                    {"message",
                                     {{"role", "assistant"},
                                      {"content", ""},
                                      {"tool_calls",
                                       nlohmann::json::array({{{"id", "call_1"},
                                                               {"type", "function"},
                                                               {"function",
                                                                {{"name", "read_file"},
                                                                 {"arguments", {{"path", "a"}}}}}}})}}}}})},
                             {"usage", {{"prompt_tokens", 1}, {"completion_tokens", 2}, {"total_tokens", 3}}},
                         };

                         auto r = ChatResponse::fromJson(j);
                         CHECK_TRUE(r.has_value());
                         CHECK_TRUE(r->finishReason.has_value());
                         CHECK_EQ(*r->finishReason, "tool_calls");
                         CHECK_TRUE(r->hasToolCalls());
                         CHECK_EQ(r->toolCalls.size(), 1u);
                         CHECK_EQ(r->toolCalls[0].function.name, "read_file");
                         CHECK_EQ(r->totalTokens, 3u);
                     }});

    return mini_test::run(tests);
}

