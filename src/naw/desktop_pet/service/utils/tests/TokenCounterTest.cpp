#include "naw/desktop_pet/service/utils/TokenCounter.h"

#include <iostream>
#include <string>
#include <vector>
#include <functional>

using namespace naw::desktop_pet::service::utils;

// 轻量断言工具（与 HttpClientTest 保持一致风格）
namespace mini_test {

class AssertionFailed : public std::runtime_error {
public:
    explicit AssertionFailed(const std::string& msg) : std::runtime_error(msg) {}
};

#define CHECK_TRUE(cond)                                                                          \
    do {                                                                                          \
        if (!(cond))                                                                              \
            throw mini_test::AssertionFailed(std::string("CHECK_TRUE failed: ") + #cond);         \
    } while (0)

#define CHECK_EQ(a, b)                                                                            \
    do {                                                                                          \
        const auto _va = (a);                                                                     \
        const auto _vb = (b);                                                                     \
        if (!(_va == _vb)) {                                                                      \
            throw mini_test::AssertionFailed(std::string("CHECK_EQ failed: ") + #a " vs " #b);    \
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

static void testEstimatorDefaultRule() {
    TokenEstimator estimator;
    const auto tokens = estimator.estimateTokens("unknown-model", "hello world");
    CHECK_TRUE(tokens >= 4); // fixedOverhead 防止过小
}

static void testEstimatorCustomRule() {
    TokenEstimator estimator;
    estimator.setModelRule("custom-model", TokenModelRule{0.5, 2}); // 2 字符 1 token
    const auto tokens = estimator.estimateTokens("custom-model", "abcd"); // 4 chars
    CHECK_EQ(tokens, 4); // 4*0.5 + 2 = 4 向上取整后仍为4
}

static void testCounterRecordAndReset() {
    TokenEstimator estimator;
    TokenCounter counter;

    counter.recordText("gpt-4o-mini", "hello", "hi", estimator);
    counter.record("gpt-4o-mini", 10, 5);

    const auto total = counter.totalUsage();
    CHECK_TRUE(total.totalTokens >= 0);
    CHECK_EQ(total.calls, 2);

    const auto byModel = counter.modelUsage();
    auto it = byModel.find("gpt-4o-mini");
    CHECK_TRUE(it != byModel.end());
    CHECK_EQ(it->second.calls, 2);

    counter.reset();
    CHECK_EQ(counter.totalUsage().calls, 0);
    CHECK_TRUE(counter.modelUsage().empty());
}

int main() {
    std::vector<mini_test::TestCase> cases{
        {"Estimator default rule", testEstimatorDefaultRule},
        {"Estimator custom rule", testEstimatorCustomRule},
        {"Counter record & reset", testCounterRecordAndReset},
    };
    return mini_test::run(cases);
}

