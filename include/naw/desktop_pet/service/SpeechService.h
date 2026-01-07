#pragma once

#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/ErrorTypes.h"
#include "naw/desktop_pet/service/utils/AudioProcessor.h"
#include "naw/desktop_pet/service/utils/HttpClient.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace naw::desktop_pet::service {

/**
 * @brief 语音服务：提供STT（语音转文本）、TTS（文本转语音）和VAD（语音活动检测）功能
 *
 * 功能：
 * - STT：支持同步和流式语音转文本
 * - TTS：支持同步和流式文本转语音
 * - VAD：语音活动检测（复用AudioProcessor的VAD功能）
 */
class SpeechService {
public:
    // ========== STT相关结构 ==========
    
    /**
     * @brief STT配置
     */
    struct STTConfig {
        bool enabled{false};
        std::string baseUrl;
        std::string apiKey;
        std::string modelId;
        std::optional<std::string> language;  // 语言代码，如"zh"、"en"
        int timeoutMs{30000};                 // 请求超时时间（毫秒）
        float confidenceThreshold{0.0f};       // 置信度阈值（0.0-1.0）
    };

    /**
     * @brief STT结果
     */
    struct STTResult {
        std::string text;                      // 识别的文本
        float confidence{0.0f};                // 置信度（0.0-1.0）
        std::optional<double> duration;        // 音频时长（秒）
        std::optional<std::string> language;   // 检测到的语言
    };

    /**
     * @brief 流式STT回调
     */
    struct STTStreamCallbacks {
        std::function<void(const std::string& partialText)> onPartialText;  // 部分识别结果
        std::function<void(const STTResult& finalResult)> onFinalResult;   // 最终识别结果
        std::function<void(const ErrorInfo& error)> onError;                // 错误回调
    };

    // ========== TTS相关结构 ==========
    
    /**
     * @brief TTS配置
     */
    struct TTSConfig {
        bool enabled{false};
        std::string baseUrl;
        std::string apiKey;
        std::string modelId;
        std::string voice;                     // 音色ID（如"fnlp/MOSS-TTSD-v0.5:alex"）
        std::string referenceUri;              // 参考音频URI（用于CosyVoice2等）
        std::string referenceText;             // 参考音频对应文本
        std::string responseFormat{"wav"};     // 响应格式：wav/mp3/opus/pcm
        std::optional<int> sampleRate;        // 采样率（如44100）
        std::optional<int> pcmChannels;        // PCM输出声道数（1/2）
        std::optional<float> speed;           // 语速（0.25-4.0）
        std::optional<float> gain;            // 增益（-10.0-10.0）
        std::optional<float> pitch;           // 音调（可选，部分API支持）
        std::optional<float> volume;          // 音量（0.0-1.0）
        bool stream{true};                    // 是否使用流式
        int timeoutMs{60000};                 // 请求超时时间（毫秒）
    };

    /**
     * @brief TTS结果
     */
    struct TTSResult {
        std::vector<std::uint8_t> audioData;   // 音频数据（二进制）
        std::string format;                    // 音频格式（wav/mp3/opus/pcm）
        std::uint32_t sampleRate{0};           // 采样率
        std::uint32_t channels{0};             // 声道数
    };

    /**
     * @brief 流式TTS回调
     */
    struct TTSStreamCallbacks {
        std::function<void(const void* audioChunk, std::size_t bytes)> onAudioChunk;  // 音频数据块
        std::function<void(const TTSResult& finalResult)> onComplete;                 // 完成回调
        std::function<void(const ErrorInfo& error)> onError;                         // 错误回调
    };

    // ========== VAD相关结构 ==========
    
    /**
     * @brief VAD配置（复用AudioProcessor的VADConfig）
     */
    using VADConfig = utils::VADConfig;

    /**
     * @brief VAD回调（复用AudioProcessor的VADCallbacks）
     */
    using VADCallbacks = utils::VADCallbacks;

    // ========== 构造函数和析构函数 ==========
    
    explicit SpeechService(ConfigManager& cfg);
    ~SpeechService();

    // 禁止拷贝/移动
    SpeechService(const SpeechService&) = delete;
    SpeechService& operator=(const SpeechService&) = delete;
    SpeechService(SpeechService&&) = delete;
    SpeechService& operator=(SpeechService&&) = delete;

    // ========== 初始化 ==========
    
    /**
     * @brief 初始化服务
     * @return 是否成功
     */
    bool initialize();

    /**
     * @brief 关闭服务
     */
    void shutdown();

    /**
     * @brief 是否已初始化
     */
    bool isInitialized() const { return initialized_; }

    // ========== STT功能 ==========
    
    /**
     * @brief 语音转文本（同步）
     * @param audioPath 音频文件路径（WAV格式）
     * @param config STT配置（可选，使用默认配置如果为空）
     * @return STT结果，失败返回std::nullopt
     */
    std::optional<STTResult> speechToText(const std::string& audioPath,
                                          const std::optional<STTConfig>& config = std::nullopt);

    /**
     * @brief 语音转文本（从PCM数据）
     * @param pcmData PCM音频数据
     * @param streamConfig 音频流配置
     * @param config STT配置（可选）
     * @return STT结果，失败返回std::nullopt
     */
    std::optional<STTResult> speechToText(const std::vector<std::uint8_t>& pcmData,
                                          const utils::AudioStreamConfig& streamConfig,
                                          const std::optional<STTConfig>& config = std::nullopt);

    /**
     * @brief 流式语音转文本
     * @param config STT配置（可选）
     * @param callbacks 流式回调
     * @return 是否成功启动
     */
    bool speechToTextStream(const std::optional<STTConfig>& config,
                           const STTStreamCallbacks& callbacks);

    /**
     * @brief 停止流式STT
     */
    void stopSpeechToTextStream();

    // ========== TTS功能 ==========
    
    /**
     * @brief 文本转语音（同步）
     * @param text 要合成的文本
     * @param config TTS配置（可选，使用默认配置如果为空）
     * @return TTS结果，失败返回std::nullopt
     */
    std::optional<TTSResult> textToSpeech(const std::string& text,
                                         const std::optional<TTSConfig>& config = std::nullopt);

    /**
     * @brief 流式文本转语音
     * @param text 要合成的文本
     * @param config TTS配置（可选）
     * @param callbacks 流式回调
     * @return 是否成功启动
     */
    bool textToSpeechStream(const std::string& text,
                           const std::optional<TTSConfig>& config,
                           const TTSStreamCallbacks& callbacks);

    /**
     * @brief 停止流式TTS
     */
    void stopTextToSpeechStream();

    // ========== VAD功能 ==========
    
    /**
     * @brief 启动被动监听（VAD）
     * @param vadConfig VAD配置
     * @param captureOptions 录音选项
     * @param callbacks VAD回调
     * @return 是否成功启动
     */
    bool startPassiveListening(const VADConfig& vadConfig,
                               const utils::CaptureOptions& captureOptions,
                               const VADCallbacks& callbacks);

    /**
     * @brief 停止被动监听
     */
    void stopPassiveListening();

    /**
     * @brief 是否正在被动监听
     */
    bool isPassiveListening() const;

    /**
     * @brief 删除VAD录音文件
     * @param path 文件路径
     * @return 是否成功删除
     */
    bool removeVadFile(const std::string& path);

    // ========== 配置管理 ==========
    
    /**
     * @brief 从ConfigManager加载STT配置
     */
    std::optional<STTConfig> loadSTTConfig() const;

    /**
     * @brief 从ConfigManager加载TTS配置
     */
    std::optional<TTSConfig> loadTTSConfig() const;

    /**
     * @brief 获取默认STT配置
     */
    STTConfig getDefaultSTTConfig() const;

    /**
     * @brief 获取默认TTS配置
     */
    TTSConfig getDefaultTTSConfig() const;

    // ========== 音频处理器访问 ==========
    
    /**
     * @brief 获取AudioProcessor引用（用于高级操作）
     */
    utils::AudioProcessor& getAudioProcessor() { return audioProcessor_; }
    const utils::AudioProcessor& getAudioProcessor() const { return audioProcessor_; }

private:
    // 从配置加载STT配置（内部实现）
    STTConfig loadSTTConfigInternal() const;
    
    // 从配置加载TTS配置（内部实现）
    TTSConfig loadTTSConfigInternal() const;
    
    // 执行STT API调用
    std::optional<STTResult> executeSTT(const std::string& audioPath,
                                      const STTConfig& config);
    
    // 执行STT API调用（从PCM数据）
    std::optional<STTResult> executeSTTFromPCM(const std::vector<std::uint8_t>& pcmData,
                                               const utils::AudioStreamConfig& streamConfig,
                                               const STTConfig& config);
    
    // 执行TTS API调用（同步）
    std::optional<TTSResult> executeTTS(const std::string& text,
                                      const TTSConfig& config);
    
    // 解析STT响应JSON
    std::optional<STTResult> parseSTTResponse(const std::string& jsonResponse) const;
    
    // 连接URL
    std::string joinUrl(const std::string& base, const std::string& path) const;
    
    // 检查配置占位符
    bool looksLikeEnvPlaceholder(const std::string& s) const;

    ConfigManager& config_;
    utils::AudioProcessor audioProcessor_;
    bool initialized_{false};
    
    // 流式STT状态
    std::atomic<bool> sttStreaming_{false};
    std::mutex sttStreamMutex_;
    std::thread sttStreamThread_;
    std::atomic<bool> sttStreamStop_{false};
    STTStreamCallbacks sttStreamCallbacks_;
    STTConfig sttStreamConfig_;
    std::vector<std::uint8_t> sttStreamBuffer_;
    std::mutex sttStreamBufferMutex_;
    std::chrono::steady_clock::time_point sttLastChunkTime_;
    std::string sttAccumulatedText_;
    
    // 流式TTS状态
    std::atomic<bool> ttsStreaming_{false};
    std::mutex ttsStreamMutex_;
    std::thread ttsStreamThread_;
    
    // 流式STT内部方法
    void sttStreamWorker();
    void processSTTChunk(const std::vector<std::uint8_t>& chunk, const utils::AudioStreamConfig& streamConfig);
};

} // namespace naw::desktop_pet::service
