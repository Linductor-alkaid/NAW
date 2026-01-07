#include "naw/desktop_pet/service/SpeechService.h"

#include "naw/desktop_pet/service/ConfigManager.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using naw::desktop_pet::service::ConfigManager;
using naw::desktop_pet::service::SpeechService;
using naw::desktop_pet::service::utils::AudioFormat;
using naw::desktop_pet::service::utils::AudioStreamConfig;

// 轻量断言工具（与 AudioProcessorTest 保持一致风格）
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

#define CHECK_NE(a, b)                                                                            \
    do {                                                                                          \
        const auto _va = (a);                                                                     \
        const auto _vb = (b);                                                                     \
        if ((_va == _vb)) {                                                                      \
            throw mini_test::AssertionFailed(std::string("CHECK_NE failed: ") + #a " == " #b);    \
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

// 辅助函数：创建测试用的正弦波PCM数据
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

// 辅助函数：创建测试用的WAV文件
static std::string createTestWavFile(const std::string& filename, std::uint32_t sampleRate = 16000, std::uint32_t channels = 1) {
    auto pcm = makeS16SinePcm(sampleRate, channels, 0.5, 440.0, 0.3);
    
    AudioStreamConfig streamConfig;
    streamConfig.format = AudioFormat::S16;
    streamConfig.sampleRate = sampleRate;
    streamConfig.channels = channels;
    
    // 使用临时目录
    std::string tempDir = std::filesystem::temp_directory_path().string();
    std::string wavPath = tempDir + "/" + filename;
    
    // 创建SpeechService来写入WAV（需要先初始化AudioProcessor）
    ConfigManager cfg;
    SpeechService service(cfg);
    if (service.initialize()) {
        service.getAudioProcessor().writePcmToWav(wavPath, streamConfig, pcm);
        service.shutdown();
    }
    
    return wavPath;
}

// ========== 测试用例 ==========

static void testInitialization() {
    ConfigManager cfg;
    SpeechService service(cfg);
    
    CHECK_TRUE(!service.isInitialized());
    CHECK_TRUE(service.initialize());
    CHECK_TRUE(service.isInitialized());
    
    service.shutdown();
    CHECK_TRUE(!service.isInitialized());
}

static void testSTTConfigLoading() {
    ConfigManager cfg;
    
    // 创建测试配置
    nlohmann::json config;
    config["multimodal"]["stt"]["enabled"] = true;
    config["multimodal"]["stt"]["base_url"] = "https://api.example.com";
    config["multimodal"]["stt"]["api_key"] = "test_key";
    config["multimodal"]["stt"]["model_id"] = "test_model";
    config["multimodal"]["stt"]["language"] = "zh";
    config["multimodal"]["stt"]["timeout_ms"] = 30000;
    config["multimodal"]["stt"]["confidence_threshold"] = 0.5f;
    
    // 写入临时配置文件
    std::string configPath = (std::filesystem::temp_directory_path() / "test_speech_config.json").string();
    std::ofstream ofs(configPath);
    ofs << config.dump(2);
    ofs.close();
    
    // 加载配置
    naw::desktop_pet::service::ErrorInfo err;
    CHECK_TRUE(cfg.loadFromFile(configPath, &err));
    
    SpeechService service(cfg);
    CHECK_TRUE(service.initialize());
    
    auto sttConfig = service.loadSTTConfig();
    CHECK_TRUE(sttConfig.has_value());
    CHECK_EQ(sttConfig->enabled, true);
    CHECK_EQ(sttConfig->baseUrl, "https://api.example.com");
    CHECK_EQ(sttConfig->apiKey, "test_key");
    CHECK_EQ(sttConfig->modelId, "test_model");
    CHECK_TRUE(sttConfig->language.has_value());
    CHECK_EQ(*sttConfig->language, "zh");
    CHECK_EQ(sttConfig->timeoutMs, 30000);
    CHECK_EQ(sttConfig->confidenceThreshold, 0.5f);
    
    service.shutdown();
    
    // 清理
    std::filesystem::remove(configPath);
}

static void testTTSConfigLoading() {
    ConfigManager cfg;
    
    // 创建测试配置
    nlohmann::json config;
    config["multimodal"]["tts"]["enabled"] = true;
    config["multimodal"]["tts"]["base_url"] = "https://api.example.com";
    config["multimodal"]["tts"]["api_key"] = "test_key";
    config["multimodal"]["tts"]["model_id"] = "test_model";
    config["multimodal"]["tts"]["voice"] = "test_voice";
    config["multimodal"]["tts"]["response_format"] = "wav";
    config["multimodal"]["tts"]["sample_rate"] = 44100;
    config["multimodal"]["tts"]["speed"] = 1.0f;
    
    // 写入临时配置文件
    std::string configPath = (std::filesystem::temp_directory_path() / "test_tts_config.json").string();
    std::ofstream ofs(configPath);
    ofs << config.dump(2);
    ofs.close();
    
    // 加载配置
    naw::desktop_pet::service::ErrorInfo err;
    CHECK_TRUE(cfg.loadFromFile(configPath, &err));
    
    SpeechService service(cfg);
    CHECK_TRUE(service.initialize());
    
    auto ttsConfig = service.loadTTSConfig();
    CHECK_TRUE(ttsConfig.has_value());
    CHECK_EQ(ttsConfig->enabled, true);
    CHECK_EQ(ttsConfig->baseUrl, "https://api.example.com");
    CHECK_EQ(ttsConfig->apiKey, "test_key");
    CHECK_EQ(ttsConfig->modelId, "test_model");
    CHECK_EQ(ttsConfig->voice, "test_voice");
    CHECK_EQ(ttsConfig->responseFormat, "wav");
    CHECK_TRUE(ttsConfig->sampleRate.has_value());
    CHECK_EQ(*ttsConfig->sampleRate, 44100);
    CHECK_TRUE(ttsConfig->speed.has_value());
    CHECK_EQ(*ttsConfig->speed, 1.0f);
    
    service.shutdown();
    
    // 清理
    std::filesystem::remove(configPath);
}

static void testSTTFromPCM() {
    ConfigManager cfg;
    SpeechService service(cfg);
    CHECK_TRUE(service.initialize());
    
    // 创建测试PCM数据
    AudioStreamConfig streamConfig;
    streamConfig.format = AudioFormat::S16;
    streamConfig.sampleRate = 16000;
    streamConfig.channels = 1;
    
    auto pcmData = makeS16SinePcm(16000, 1, 0.5, 440.0, 0.3);
    
    // 配置STT（使用无效的API配置，应该返回nullopt）
    SpeechService::STTConfig sttConfig;
    sttConfig.enabled = true;
    sttConfig.baseUrl = "https://invalid-api.example.com";
    sttConfig.apiKey = "invalid_key";
    sttConfig.modelId = "invalid_model";
    
    // 由于API无效，应该返回nullopt（不抛出异常）
    auto result = service.speechToText(pcmData, streamConfig, sttConfig);
    // 预期返回nullopt（因为API调用会失败）
    CHECK_TRUE(!result.has_value());
    
    service.shutdown();
}

static void testTTSBasic() {
    ConfigManager cfg;
    SpeechService service(cfg);
    CHECK_TRUE(service.initialize());
    
    // 配置TTS（使用无效的API配置，应该返回nullopt）
    SpeechService::TTSConfig ttsConfig;
    ttsConfig.enabled = true;
    ttsConfig.baseUrl = "https://invalid-api.example.com";
    ttsConfig.apiKey = "invalid_key";
    ttsConfig.modelId = "invalid_model";
    ttsConfig.voice = "test_voice";
    ttsConfig.responseFormat = "wav";
    
    // 由于API无效，应该返回nullopt（不抛出异常）
    auto result = service.textToSpeech("Hello, world!", ttsConfig);
    // 预期返回nullopt（因为API调用会失败）
    CHECK_TRUE(!result.has_value());
    
    service.shutdown();
}

static void testVADIntegration() {
    ConfigManager cfg;
    SpeechService service(cfg);
    CHECK_TRUE(service.initialize());
    
    // 测试VAD配置
    SpeechService::VADConfig vadConfig;
    vadConfig.startThresholdDb = -35.0f;
    vadConfig.stopThresholdDb = -40.0f;
    vadConfig.startHoldMs = 200;
    vadConfig.stopHoldMs = 600;
    vadConfig.maxBufferSeconds = 10.0f;
    vadConfig.outputWavPath = "test_vad.wav";
    
    // 测试CaptureOptions
    naw::desktop_pet::service::utils::CaptureOptions captureOptions;
    captureOptions.useDeviceDefault = true;
    captureOptions.stream.format = AudioFormat::S16;
    captureOptions.storeInMemory = false;
    
    // 测试VADCallbacks
    SpeechService::VADCallbacks callbacks;
    bool triggered = false;
    callbacks.onTrigger = [&triggered]() {
        triggered = true;
    };
    
    bool completed = false;
    std::string completedPath;
    callbacks.onComplete = [&completed, &completedPath](const std::string& path) {
        completed = true;
        completedPath = path;
    };
    
    // 注意：实际启动VAD需要音频设备，这里只测试接口不抛出异常
    // 由于可能没有音频设备，startPassiveListening可能返回false
    bool started = service.startPassiveListening(vadConfig, captureOptions, callbacks);
    // 无论是否成功启动，都不应该抛出异常
    
    if (started) {
        CHECK_TRUE(service.isPassiveListening());
        service.stopPassiveListening();
        CHECK_TRUE(!service.isPassiveListening());
    }
    
    service.shutdown();
}

static void testStreamingTTS() {
    ConfigManager cfg;
    SpeechService service(cfg);
    CHECK_TRUE(service.initialize());
    
    // 配置TTS（使用无效的API配置）
    SpeechService::TTSConfig ttsConfig;
    ttsConfig.enabled = true;
    ttsConfig.baseUrl = "https://invalid-api.example.com";
    ttsConfig.apiKey = "invalid_key";
    ttsConfig.modelId = "invalid_model";
    ttsConfig.voice = "test_voice";
    ttsConfig.responseFormat = "pcm";
    ttsConfig.stream = true;
    
    // 测试流式TTS回调
    SpeechService::TTSStreamCallbacks callbacks;
    bool chunkReceived = false;
    bool completed = false;
    bool errorReceived = false;
    
    callbacks.onAudioChunk = [&chunkReceived](const void* data, std::size_t bytes) {
        chunkReceived = true;
    };
    
    callbacks.onComplete = [&completed](const SpeechService::TTSResult&) {
        completed = true;
    };
    
    callbacks.onError = [&errorReceived](const naw::desktop_pet::service::ErrorInfo&) {
        errorReceived = true;
    };
    
    // 启动流式TTS（预期会失败，因为API无效）
    bool started = service.textToSpeechStream("Hello", ttsConfig, callbacks);
    
    if (started) {
        // 等待一段时间让流式处理完成或失败
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 停止流式TTS
        service.stopTextToSpeechStream();
    }
    
    service.shutdown();
}

static void testStreamingSTT() {
    ConfigManager cfg;
    SpeechService service(cfg);
    CHECK_TRUE(service.initialize());
    
    // 配置STT（使用无效的API配置）
    SpeechService::STTConfig sttConfig;
    sttConfig.enabled = true;
    sttConfig.baseUrl = "https://invalid-api.example.com";
    sttConfig.apiKey = "invalid_key";
    sttConfig.modelId = "invalid_model";
    
    // 测试流式STT回调
    SpeechService::STTStreamCallbacks callbacks;
    bool partialReceived = false;
    bool finalReceived = false;
    bool errorReceived = false;
    
    callbacks.onPartialText = [&partialReceived](const std::string& text) {
        partialReceived = true;
    };
    
    callbacks.onFinalResult = [&finalReceived](const SpeechService::STTResult& result) {
        finalReceived = true;
    };
    
    callbacks.onError = [&errorReceived](const naw::desktop_pet::service::ErrorInfo&) {
        errorReceived = true;
    };
    
    // 启动流式STT（预期会失败，因为API无效或没有音频设备）
    bool started = service.speechToTextStream(sttConfig, callbacks);
    
    if (started) {
        // 等待一段时间
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 停止流式STT
        service.stopSpeechToTextStream();
    }
    
    service.shutdown();
}

static void testAudioProcessorAccess() {
    ConfigManager cfg;
    SpeechService service(cfg);
    CHECK_TRUE(service.initialize());
    
    // 测试可以访问AudioProcessor
    auto& audioProcessor = service.getAudioProcessor();
    CHECK_TRUE(audioProcessor.isInitialized());
    
    const auto& constAudioProcessor = static_cast<const SpeechService&>(service).getAudioProcessor();
    CHECK_TRUE(constAudioProcessor.isInitialized());
    
    service.shutdown();
}

static void testDefaultConfigs() {
    ConfigManager cfg;
    SpeechService service(cfg);
    CHECK_TRUE(service.initialize());
    
    // 测试默认配置
    auto defaultSTT = service.getDefaultSTTConfig();
    auto defaultTTS = service.getDefaultTTSConfig();
    
    // 默认配置应该存在（即使未启用）
    // 这里只检查不会抛出异常
    
    service.shutdown();
}

int main() {
    std::vector<mini_test::TestCase> tests{
        {"Initialization", testInitialization},
        {"STT Config Loading", testSTTConfigLoading},
        {"TTS Config Loading", testTTSConfigLoading},
        {"STT From PCM", testSTTFromPCM},
        {"TTS Basic", testTTSBasic},
        {"VAD Integration", testVADIntegration},
        {"Streaming TTS", testStreamingTTS},
        {"Streaming STT", testStreamingSTT},
        {"AudioProcessor Access", testAudioProcessorAccess},
        {"Default Configs", testDefaultConfigs},
    };
    
    return mini_test::run(tests);
}
