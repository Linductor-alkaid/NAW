#include "naw/desktop_pet/service/utils/AudioProcessor.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstring>

using naw::desktop_pet::service::utils::AudioErrorCode;
using naw::desktop_pet::service::utils::AudioFormat;
using naw::desktop_pet::service::utils::AudioProcessor;
using naw::desktop_pet::service::utils::AudioStreamConfig;

// 轻量断言工具（与 utils/tests/TokenCounterTest 保持一致风格）
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

static AudioStreamConfig makeCfg(AudioFormat fmt, std::uint32_t sr, std::uint32_t ch) {
    AudioStreamConfig cfg{};
    cfg.format = fmt;
    cfg.sampleRate = sr;
    cfg.channels = ch;
    cfg.periodSizeInFrames = 0;
    return cfg;
}

static std::vector<std::uint8_t> makeS16SinePcm(std::uint32_t sr, std::uint32_t ch, double seconds, double freqHz, double amp) {
    const std::size_t frames = static_cast<std::size_t>(sr * seconds);
    std::vector<std::int16_t> samples(frames * ch);
    for (std::size_t f = 0; f < frames; ++f) {
        const double t = static_cast<double>(f) / static_cast<double>(sr);
        const double s = std::sin(2.0 * 3.141592653589793 * freqHz * t) * amp;
        const auto v = static_cast<std::int16_t>(std::lrint(std::max(-1.0, std::min(1.0, s)) * 32767.0));
        for (std::size_t c = 0; c < ch; ++c) {
            samples[f * ch + c] = v;
        }
    }
    std::vector<std::uint8_t> bytes(samples.size() * sizeof(std::int16_t));
    std::memcpy(bytes.data(), samples.data(), bytes.size());
    return bytes;
}

static void testValidatePcm() {
    const auto cfg = makeCfg(AudioFormat::S16, 16000, 1);
    auto pcm = makeS16SinePcm(16000, 1, 0.1, 440.0, 0.2);

    auto err = AudioProcessor::validatePcmBuffer(cfg, pcm.size());
    CHECK_TRUE(!err.has_value());

    // bytes 非整帧
    err = AudioProcessor::validatePcmBuffer(cfg, pcm.size() - 1);
    CHECK_TRUE(err.has_value());
    CHECK_EQ(err->code, AudioErrorCode::InvalidArgs);

    // 采样率非法
    auto bad = cfg;
    bad.sampleRate = 0;
    err = AudioProcessor::validatePcmBuffer(bad, pcm.size());
    CHECK_TRUE(err.has_value());
}

static void testAnalyzeNormalizeGain() {
    const auto cfg = makeCfg(AudioFormat::S16, 16000, 1);
    auto pcm = makeS16SinePcm(16000, 1, 0.2, 440.0, 0.2);

    auto st = AudioProcessor::analyzePcm(cfg, pcm.data(), pcm.size());
    CHECK_TRUE(st.frames > 0);
    CHECK_TRUE(st.peakAbs > 0.0f);
    CHECK_TRUE(st.peakAbs < 0.5f);

    // 增益 +6dB 后峰值增大（但不应超过 1 太多）
    CHECK_TRUE(AudioProcessor::applyGainInPlace(cfg, pcm, 6.0f));
    auto st2 = AudioProcessor::analyzePcm(cfg, pcm.data(), pcm.size());
    CHECK_TRUE(st2.peakAbs > st.peakAbs);

    // 归一化到 -1dBFS：峰值应接近 0.891
    CHECK_TRUE(AudioProcessor::normalizePeakInPlace(cfg, pcm, -1.0f));
    auto st3 = AudioProcessor::analyzePcm(cfg, pcm.data(), pcm.size());
    CHECK_TRUE(std::abs(st3.peakAbs - 0.891f) < 0.06f);
}

static void testTrimSilence() {
    const auto cfg = makeCfg(AudioFormat::S16, 16000, 1);

    // 构造：0.1s 静音 + 0.2s 正弦 + 0.1s 静音
    auto head = makeS16SinePcm(16000, 1, 0.1, 440.0, 0.0);
    auto body = makeS16SinePcm(16000, 1, 0.2, 440.0, 0.2);
    auto tail = makeS16SinePcm(16000, 1, 0.1, 440.0, 0.0);
    std::vector<std::uint8_t> pcm;
    pcm.reserve(head.size() + body.size() + tail.size());
    pcm.insert(pcm.end(), head.begin(), head.end());
    pcm.insert(pcm.end(), body.begin(), body.end());
    pcm.insert(pcm.end(), tail.begin(), tail.end());

    const auto beforeFrames = AudioProcessor::analyzePcm(cfg, pcm.data(), pcm.size()).frames;
    CHECK_TRUE(AudioProcessor::trimSilenceInPlace(cfg, pcm, -40.0f, 50 /*ms*/));
    const auto afterFrames = AudioProcessor::analyzePcm(cfg, pcm.data(), pcm.size()).frames;
    CHECK_TRUE(afterFrames < beforeFrames);
    CHECK_TRUE(afterFrames > 0);
}

static void testStreamErrorLastError() {
    AudioProcessor audio;
    CHECK_TRUE(audio.initialize());

    AudioStreamConfig stream{};
    stream.format = AudioFormat::S16;
    stream.sampleRate = 16000;
    stream.channels = 1;

    auto id = audio.startStream(stream, 64 /*small buffer*/);
    CHECK_TRUE(id.has_value());

    // 非整帧 bytes
    std::uint8_t dummy[3]{0, 0, 0};
    CHECK_TRUE(!audio.appendStreamData(*id, dummy, sizeof(dummy)));
    auto err = audio.lastError();
    CHECK_TRUE(err.has_value());
    CHECK_EQ(err->code, AudioErrorCode::InvalidArgs);

    audio.stopAll();
    audio.shutdown();
}

int main() {
    std::vector<mini_test::TestCase> cases{
        {"Validate PCM buffer", testValidatePcm},
        {"Analyze/normalize/gain", testAnalyzeNormalizeGain},
        {"Trim silence", testTrimSilence},
        {"Stream error & lastError", testStreamErrorLastError},
    };
    return mini_test::run(cases);
}

