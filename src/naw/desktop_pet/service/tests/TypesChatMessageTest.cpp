#include "naw/desktop_pet/service/types/ChatMessage.h"

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

    tests.push_back({"FromJsonTextOnly", []() {
                         nlohmann::json j = {
                             {"role", "user"},
                             {"content", "hello"},
                         };
                         auto m = ChatMessage::fromJson(j);
                         CHECK_TRUE(m.has_value());
                         CHECK_TRUE(m->isText());
                         CHECK_TRUE(m->textView().has_value());
                         CHECK_EQ(std::string(*m->textView()), "hello");
                         CHECK_EQ(roleToString(m->role), "user");
                     }});

    tests.push_back({"FromJsonMultimodalTextArray", []() {
                         nlohmann::json j = {
                             {"role", "assistant"},
                             {"content", nlohmann::json::array({{{"type", "text"}, {"text", "hi"}}})},
                         };
                         auto m = ChatMessage::fromJson(j);
                         CHECK_TRUE(m.has_value());
                         CHECK_FALSE(m->isText());
                         CHECK_TRUE(m->isValid());
                     }});

    tests.push_back({"ToolCallIdCamelCaseCompatibility", []() {
                         nlohmann::json j = {
                             {"role", "tool"},
                             {"content", "ok"},
                             {"toolCallId", "abc"},
                             {"name", "read_file"},
                         };
                         auto m = ChatMessage::fromJson(j);
                         CHECK_TRUE(m.has_value());
                         CHECK_TRUE(m->toolCallId.has_value());
                         CHECK_EQ(*m->toolCallId, "abc");

                         auto out = m->toJson();
                         CHECK_TRUE(out.contains("tool_call_id"));
                         CHECK_EQ(out["tool_call_id"].get<std::string>(), "abc");
                     }});

    tests.push_back({"IsValidRejectsEmptyContent", []() {
                         ChatMessage m;
                         m.role = MessageRole::User;
                         m.setText("");
                         std::string reason;
                         CHECK_FALSE(m.isValid(&reason));
                         CHECK_FALSE(reason.empty());
                     }});

    tests.push_back({"EstimateTokensNonZeroForText", []() {
                         ChatMessage m;
                         m.role = MessageRole::User;
                         m.setText("hello world");
                         auto n = m.estimateTokens("deepseek-ai/DeepSeek-V3");
                         CHECK_TRUE(n > 0);
                     }});

    tests.push_back({"MultimodalImageUrlHttp", []() {
                         nlohmann::json j = {
                             {"role", "user"},
                             {"content",
                              nlohmann::json::array({
                                  {{"type", "text"}, {"text", "look"}},
                                  {{"type", "image_url"}, {"image_url", {{"url", "https://example.com/a.png"}}}},
                              })},
                         };
                         auto m = ChatMessage::fromJson(j);
                         CHECK_TRUE(m.has_value());
                         CHECK_TRUE(m->isValid());
                         auto out = m->toJson();
                         CHECK_TRUE(out["content"].is_array());
                     }});

    tests.push_back({"MultimodalImageUrlDataBase64", []() {
                         // Minimal PNG header base64 (not a full image, but enough for decode + non-empty)
                         const std::string dataUrl = "data:image/png;base64,iVBORw0KGgo=";
                         nlohmann::json j = {
                             {"role", "user"},
                             {"content",
                              nlohmann::json::array({
                                  {{"type", "image_url"}, {"image_url", {{"url", dataUrl}}}},
                              })},
                         };
                         auto m = ChatMessage::fromJson(j);
                         CHECK_TRUE(m.has_value());
                         CHECK_TRUE(m->isValid());

                         auto tokens = m->estimateTokens("deepseek-ai/DeepSeek-V3");
                         CHECK_TRUE(tokens >= 200);
                     }});

    tests.push_back({"MultimodalRejectsInvalidBase64", []() {
                         const std::string bad = "data:image/png;base64,@@@";
                         nlohmann::json j = {
                             {"role", "user"},
                             {"content",
                              nlohmann::json::array({
                                  {{"type", "image_url"}, {"image_url", {{"url", bad}}}},
                              })},
                         };
                         auto m = ChatMessage::fromJson(j);
                         CHECK_TRUE(m.has_value());
                         std::string reason;
                         CHECK_FALSE(m->isValid(&reason));
                         CHECK_FALSE(reason.empty());
                     }});

    return mini_test::run(tests);
}

