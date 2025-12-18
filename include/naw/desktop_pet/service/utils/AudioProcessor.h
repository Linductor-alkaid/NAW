#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>

#include <miniaudio.h>

namespace naw::desktop_pet::service::utils {

enum class AudioFormat {
    F32,
    S16,
};

enum class AudioErrorCode : std::uint8_t {
    None = 0,
    NotInitialized,
    InvalidArgs,
    NotFound,
    Unsupported,
    DeviceInitFailed,
    DeviceStartFailed,
    DeviceStopFailed,
    DecoderFailed,
    EncoderFailed,
    IoFailed,
    BufferOverflow,
    BufferUnderrun,
    InternalError,
};

struct AudioError {
    AudioErrorCode code{AudioErrorCode::None};
    std::string message{};
};

struct AudioStats {
    // 基础信息
    double durationSeconds{0.0};
    std::uint64_t frames{0};
    std::uint32_t sampleRate{0};
    std::uint32_t channels{0};
    AudioFormat format{AudioFormat::S16};

    // 幅度/能量
    float peakAbs{0.0f}; // [0,1]（按格式归一化后的绝对峰值）
    float rms{0.0f};     // [0,1]（按格式归一化后的 RMS）
    float dbfs{-90.0f};  // 估算 dBFS

    // 质量/剪裁判定
    bool isSilent{false};
    bool isLikelyClipped{false};
    float clippedSampleRatio{0.0f}; // 接近满幅样本占比（粗略）
};

struct AudioStreamConfig {
    AudioFormat format{AudioFormat::S16};   // 默认使用设备常见格式
    std::uint32_t sampleRate{0};            // 0 表示使用设备默认采样率
    std::uint32_t channels{0};              // 0 表示使用设备默认声道数
    std::uint32_t periodSizeInFrames{0}; // 0 表示使用 miniaudio 默认值
};

struct PlaybackOptions {
    bool loop{false};
    float volume{1.0f};
};

struct CaptureOptions {
    AudioStreamConfig stream{};
    bool useDeviceDefault{true}; // 若为 true，则忽略 stream 中的 rate/通道/format，直接用设备默认
    bool storeInMemory{true};
    std::size_t maxFramesInBuffer{48000 * 10}; // 默认最多缓存10秒的PCM
    std::function<void(const void* pcm, std::size_t bytes, std::uint32_t frames)> onData;
    std::function<void(const AudioError& err)> onError; // 可选：错误回调（不会抛异常）
};

struct CapturedBuffer {
    AudioStreamConfig stream{};
    std::vector<std::uint8_t> data;
};

struct VADConfig {
    float startThresholdDb{-35.0f}; // 触发开始的能量阈值
    float stopThresholdDb{-40.0f};  // 结束的能量阈值（应低于 start）
    std::uint32_t startHoldMs{200}; // 超过阈值需持续多久才触发
    std::uint32_t stopHoldMs{600};  // 低于阈值需持续多久才结束
    float maxBufferSeconds{10.0f};  // 环形缓冲秒数上限
    std::string outputWavPath{"vad_capture.wav"}; // 默认输出文件
};

struct VADCallbacks {
    std::function<void()> onTrigger;                             // 触发开始收集时
    std::function<void(const std::string& wavPath)> onComplete;  // 收集完成并写文件后
};

/**
 * @brief 基于 miniaudio 的轻量音频处理器
 * - 支持文件/内存播放：播放、暂停、恢复、停止、seek、音量调节、循环
 * - 支持录音：启动/停止录音，数据回调，内存缓存，并可保存 WAV
 * - 仅针对 Windows 平台配置 WASAPI 优先
 */
class AudioProcessor {
public:
    AudioProcessor();
    ~AudioProcessor();

    AudioProcessor(const AudioProcessor&) = delete;
    AudioProcessor& operator=(const AudioProcessor&) = delete;

    bool initialize(const AudioStreamConfig& playbackConfig = {});
    void shutdown();
    bool isInitialized() const { return initialized_; }

    // ---- 错误观测 ----
    /**
     * @brief 获取最近一次错误（若无错误则返回 std::nullopt）
     * 说明：用于补齐“音频流错误处理”的可观测性；不影响现有返回值语义。
     */
    std::optional<AudioError> lastError() const;
    void clearLastError();

    // ---- 播放 ----
    std::optional<std::uint32_t> playFile(const std::string& path, const PlaybackOptions& opts = {});
    std::optional<std::uint32_t> playMemory(const void* data, std::size_t size, const PlaybackOptions& opts = {});
    /**
     * @brief 启动基于内存块的流式播放（面向 TTS PCM/WAV 数据）
     * @param stream PCM 参数（采样率/声道/格式必须有效且非 0）
     * @param bufferFrames 内部环形缓冲的帧容量，默认 1 秒（约 48000 帧）
     * @param opts 播放选项（音量/loop，loop 对流式场景通常不建议）
     * @return soundId：成功返回可用于 append/finish 的 id
     */
    std::optional<std::uint32_t> startStream(const AudioStreamConfig& stream,
                                             std::size_t bufferFrames = 48000,
                                             const PlaybackOptions& opts = {});
    /**
     * @brief 追加流式 PCM 数据（需与 startStream 的 stream 参数一致）
     * @param soundId startStream 返回的 id
     * @param pcm PCM 数据指针
     * @param bytes 字节数（需为整帧对齐）
     * @return 是否成功写入；缓冲不足时返回 false
     */
    bool appendStreamData(std::uint32_t soundId, const void* pcm, std::size_t bytes);
    /**
     * @brief 标记流式播放已推送完成，耗尽缓冲后自然结束
     */
    void finishStream(std::uint32_t soundId);
    bool pause(std::uint32_t soundId);
    bool resume(std::uint32_t soundId);
    bool stop(std::uint32_t soundId);
    bool setVolume(std::uint32_t soundId, float volume);
    bool seek(std::uint32_t soundId, std::uint64_t pcmFrame);
    void stopAll();

    // ---- 录音 ----
    bool startCapture(const CaptureOptions& opts);
    void stopCapture();
    bool isCapturing() const { return capturing_; }
    CapturedBuffer capturedBuffer() const;
    bool saveCapturedWav(const std::string& path) const;

    // ---- 预处理/验证/分析（纯内存，便于测试）----
    static std::optional<AudioError> validatePcmBuffer(const AudioStreamConfig& stream,
                                                       std::size_t pcmBytes,
                                                       std::size_t minFrames = 0,
                                                       std::size_t maxFrames = 0);
    static AudioStats analyzePcm(const AudioStreamConfig& stream,
                                 const void* pcm,
                                 std::size_t bytes);
    static bool applyGainInPlace(const AudioStreamConfig& stream,
                                 std::vector<std::uint8_t>& pcm,
                                 float gainDb);
    static bool normalizePeakInPlace(const AudioStreamConfig& stream,
                                     std::vector<std::uint8_t>& pcm,
                                     float targetPeakDb = -1.0f);
    static bool trimSilenceInPlace(const AudioStreamConfig& stream,
                                   std::vector<std::uint8_t>& pcm,
                                   float thresholdDb,
                                   std::uint32_t minKeepMs = 0);

    // ---- 格式与转换 ----
    std::optional<AudioStreamConfig> probeFile(const std::string& path) const;
    std::optional<CapturedBuffer> decodeFileToPCM(const std::string& path,
                                                  std::optional<AudioStreamConfig> target = std::nullopt) const;
    bool writePcmToWav(const std::string& path,
                       const AudioStreamConfig& stream,
                       const std::vector<std::uint8_t>& pcm) const;

    // ---- 静默监听/VAD ----
    bool startPassiveListening(const VADConfig& vadCfg,
                               const CaptureOptions& baseCapture,
                               const VADCallbacks& cbs = {});
    void stopPassiveListening();
    bool isPassiveListening() const { return passiveListening_; }
    /**
     * @brief 删除指定的 VAD 录音文件（会等待写入完成）
     * @return 删除是否成功或文件不存在
     */
    bool removeVadFile(const std::string& path);

private:
    struct SoundHandle {
        void* sound{nullptr};   // 实际类型为 ma_sound*
        void* decoder{nullptr}; // 实际类型为 ma_decoder*，仅内存播放使用
        void* streamSource{nullptr}; // 实际类型为 StreamSource*，流式播放使用
        std::uint64_t pausedFrame{0};
        bool paused{false};
    };

    mutable std::mutex soundMutex_;
    std::unordered_map<std::uint32_t, std::unique_ptr<SoundHandle>> sounds_;
    std::atomic<std::uint32_t> nextSoundId_{1};

    // 错误记录（线程安全）
    mutable std::mutex lastErrorMutex_;
    mutable std::optional<AudioError> lastError_{};

    // 播放上下文
    void* engine_{nullptr}; // 实际类型为 ma_engine*
    AudioStreamConfig playbackConfig_{};
    bool initialized_{false};

    // 录音上下文
    void* captureContext_{nullptr}; // 实际类型为 ma_context*
    void* captureDevice_{nullptr}; // 实际类型为 ma_device*
    CaptureOptions captureOptions_{};
    mutable std::mutex captureMutex_;
    std::vector<std::uint8_t> captureBuffer_;
    bool capturing_{false};

    // VAD/环形缓冲
    struct RingBuffer {
        std::vector<std::uint8_t> data;
        std::size_t writePos{0};
        std::size_t sizeBytes{0};
        std::size_t capacityBytes{0};
    };

    enum class VADState {
        Idle,
        Listening,
        Collecting,
    };

    VADState vadState_{VADState::Idle};
    VADConfig vadConfig_{};
    VADCallbacks vadCallbacks_{};
    RingBuffer ring_;
    std::vector<std::uint8_t> collectingBuffer_;
    std::atomic<bool> passiveListening_{false};
    std::uint64_t startHoldFrames_{0};
    std::uint64_t stopHoldFrames_{0};
    std::uint64_t currentAboveFrames_{0};
    std::uint64_t currentBelowFrames_{0};
    float lastDb_{-90.0f};

    // *** VAD 文件管理 - 新增 ***
    struct VADFileRecord {
        std::string path;
        std::shared_future<void> ready; // 写文件完成信号
    };
    std::atomic<std::uint32_t> vadCaptureCounter_{0};      // VAD 录音计数器
    std::vector<VADFileRecord> vadCapturedFiles_;          // 记录所有生成的 VAD 文件及写入状态
    std::mutex vadFilesMutex_;                             // 保护 vadCapturedFiles_

    // 内部工具
    ma_format toMiniaudioFormat(AudioFormat fmt) const;
    std::size_t frameSizeBytes(const AudioStreamConfig& cfg) const;
    bool addSoundHandle(std::uint32_t id, void* sound);
    void removeSoundHandle(std::uint32_t id);
    void setLastError(AudioErrorCode code, const std::string& message) const;
    void reportError(const CaptureOptions& opts, AudioErrorCode code, const std::string& message) const;
    float computeDb(const void* pcm, std::uint32_t frames) const;
    void ensureRingCapacity(std::size_t bytes, std::size_t bytesPerFrame);
    void pushRing(const void* pcm, std::size_t bytes);
    void appendCollecting(const void* pcm, std::size_t bytes);
    bool trimSilence(const AudioStreamConfig& stream,
                     std::vector<std::uint8_t>& pcm,
                     float thresholdDb) const;
    void resetRingAfterCapture(std::size_t bytesPerFrame, double prerollSeconds);

    // *** VAD 文件管理函数 - 新增 ***
    /**
     * @brief 清理所有已记录的 VAD 录音文件
     */
    void cleanupOldVadFiles();
    
    /**
     * @brief 根据基础路径生成唯一的 VAD 输出文件名（带时间戳和计数器）
     * @param basePath 基础文件路径（例如 "vad_capture.wav"）
     * @return 唯一的文件路径（例如 "vad_capture_1703001234567_0.wav"）
     */
    std::string generateUniqueVadPath(const std::string& basePath);

    // 回调桥接
    static void dataCallbackCapture(void* pUserData,
                                    const void* pInput,
                                    std::uint32_t frameCount);
    void onCaptureFrames(const void* pInput, std::uint32_t frameCount);
};

} // namespace naw::desktop_pet::service::utils