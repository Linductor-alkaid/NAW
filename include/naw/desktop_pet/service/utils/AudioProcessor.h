#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <miniaudio.h>

namespace naw::desktop_pet::service::utils {

enum class AudioFormat {
    F32,
    S16,
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
};

struct CapturedBuffer {
    AudioStreamConfig stream{};
    std::vector<std::uint8_t> data;
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

    // ---- 播放 ----
    std::optional<std::uint32_t> playFile(const std::string& path, const PlaybackOptions& opts = {});
    std::optional<std::uint32_t> playMemory(const void* data, std::size_t size, const PlaybackOptions& opts = {});
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

    // ---- 格式与转换 ----
    std::optional<AudioStreamConfig> probeFile(const std::string& path) const;
    std::optional<CapturedBuffer> decodeFileToPCM(const std::string& path,
                                                  std::optional<AudioStreamConfig> target = std::nullopt) const;
    bool writePcmToWav(const std::string& path,
                       const AudioStreamConfig& stream,
                       const std::vector<std::uint8_t>& pcm) const;

private:
    struct SoundHandle {
        void* sound{nullptr};   // 实际类型为 ma_sound*
        void* decoder{nullptr}; // 实际类型为 ma_decoder*，仅内存播放使用
        std::uint64_t pausedFrame{0};
        bool paused{false};
    };

    mutable std::mutex soundMutex_;
    std::unordered_map<std::uint32_t, std::unique_ptr<SoundHandle>> sounds_;
    std::atomic<std::uint32_t> nextSoundId_{1};

    // 播放上下文
    void* engine_{nullptr}; // 实际类型为 ma_engine*
    AudioStreamConfig playbackConfig_{};
    bool initialized_{false};

    // 录音上下文
    void* captureDevice_{nullptr}; // 实际类型为 ma_device*
    CaptureOptions captureOptions_{};
    mutable std::mutex captureMutex_;
    std::vector<std::uint8_t> captureBuffer_;
    bool capturing_{false};

    // 内部工具
    ma_format toMiniaudioFormat(AudioFormat fmt) const;
    std::size_t frameSizeBytes(const AudioStreamConfig& cfg) const;
    bool addSoundHandle(std::uint32_t id, void* sound);
    void removeSoundHandle(std::uint32_t id);

    // 回调桥接
    static void dataCallbackCapture(void* pUserData,
                                    const void* pInput,
                                    std::uint32_t frameCount);
    void onCaptureFrames(const void* pInput, std::uint32_t frameCount);
};

} // namespace naw::desktop_pet::service::utils

