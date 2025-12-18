#include "naw/desktop_pet/service/utils/AudioProcessor.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstring>
#include <cctype>
#include <string>
#include <future>
#include <miniaudio.h>
#include <thread>
#include <iostream>
#include <limits>

namespace {
ma_device* toDevice(void* ptr) { return reinterpret_cast<ma_device*>(ptr); }
ma_engine* toEngine(void* ptr) { return reinterpret_cast<ma_engine*>(ptr); }
ma_sound* toSound(void* ptr) { return reinterpret_cast<ma_sound*>(ptr); }
ma_context* toContext(void* ptr) { return reinterpret_cast<ma_context*>(ptr); }

struct StreamSource {
    ma_data_source_base base{};
    ma_pcm_rb rb{};
    naw::desktop_pet::service::utils::AudioStreamConfig stream{};
    std::atomic<bool> finished{false};
    std::size_t bytesPerFrame{0};
};

static StreamSource* getStreamSource(ma_data_source* ds) {
    return reinterpret_cast<StreamSource*>(ds);
}

static ma_result streamRead(ma_data_source* pDataSource, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead) {
    auto* src = getStreamSource(pDataSource);
    if (src == nullptr || pFramesOut == nullptr) {
        return MA_INVALID_ARGS;
    }

    ma_uint64 totalRead = 0;
    auto* dst = static_cast<std::uint8_t*>(pFramesOut);
    const std::size_t bytesPerFrame = src->bytesPerFrame;

    while (totalRead < frameCount) {
        const ma_uint64 requested = frameCount - totalRead;
        const ma_uint64 available = ma_pcm_rb_available_read(&src->rb);

        if (available == 0) {
            if (src->finished.load(std::memory_order_relaxed)) {
                if (totalRead == 0) {
                    if (pFramesRead) {
                        *pFramesRead = 0;
                    }
                    return MA_AT_END;
                }
                break;
            }
            // 缓冲暂时为空：输出静音以平滑播放
            std::memset(dst + totalRead * bytesPerFrame, 0, static_cast<std::size_t>(requested) * bytesPerFrame);
            totalRead += requested;
            break;
        }

        const ma_uint64 toRead = std::min(requested, available);
        ma_uint32 acquire = static_cast<ma_uint32>(toRead);
        void* pRead = nullptr;
        if (ma_pcm_rb_acquire_read(&src->rb, &acquire, &pRead) != MA_SUCCESS || pRead == nullptr) {
            break;
        }
        std::memcpy(dst + totalRead * bytesPerFrame, pRead, static_cast<std::size_t>(acquire) * bytesPerFrame);
        ma_pcm_rb_commit_read(&src->rb, acquire);
        totalRead += acquire;

        if (acquire < toRead && src->finished.load(std::memory_order_relaxed)) {
            break;
        }
    }

    if (pFramesRead) {
        *pFramesRead = totalRead;
    }
    return MA_SUCCESS;
}

static ma_result streamGetDataFormat(ma_data_source* pDataSource,
                                     ma_format* pFormat,
                                     ma_uint32* pChannels,
                                     ma_uint32* pSampleRate,
                                     ma_channel* /*pChannelMap*/,
                                     size_t /*channelMapCap*/) {
    auto* src = getStreamSource(pDataSource);
    if (src == nullptr) {
        return MA_INVALID_ARGS;
    }
    if (pFormat) {
        *pFormat = src->stream.format == naw::desktop_pet::service::utils::AudioFormat::S16 ? ma_format_s16 : ma_format_f32;
    }
    if (pChannels) {
        *pChannels = src->stream.channels;
    }
    if (pSampleRate) {
        *pSampleRate = src->stream.sampleRate;
    }
    return MA_SUCCESS;
}

static ma_data_source_vtable g_streamVTable{
    streamRead,
    nullptr,            // onSeek
    streamGetDataFormat,
    nullptr,            // onGetCursor
    nullptr             // onGetLength
};

static StreamSource* createStreamSource(const naw::desktop_pet::service::utils::AudioStreamConfig& stream, std::size_t bufferFrames) {
    if (stream.sampleRate == 0 || stream.channels == 0) {
        return nullptr;
    }

    auto* src = new StreamSource();
    src->stream = stream;
    src->bytesPerFrame = ma_get_bytes_per_sample(stream.format == naw::desktop_pet::service::utils::AudioFormat::S16 ? ma_format_s16 : ma_format_f32) * stream.channels;

    ma_data_source_config dsCfg = ma_data_source_config_init();
    dsCfg.vtable = &g_streamVTable;
    if (ma_data_source_init(&dsCfg, &src->base) != MA_SUCCESS) {
        delete src;
        return nullptr;
    }

    // Cast buffer size to the expected width; caller ensures it fits.
    if (ma_pcm_rb_init(stream.format == naw::desktop_pet::service::utils::AudioFormat::S16 ? ma_format_s16 : ma_format_f32,
                       stream.channels,
                       static_cast<ma_uint32>(bufferFrames),
                       nullptr,
                       nullptr,
                       &src->rb) != MA_SUCCESS) {
        ma_data_source_uninit(&src->base);
        delete src;
        return nullptr;
    }

    return src;
}

static void destroyStreamSource(StreamSource* src) {
    if (src == nullptr) {
        return;
    }
    ma_pcm_rb_uninit(&src->rb);
    ma_data_source_uninit(&src->base);
    delete src;
}
} // namespace

namespace naw::desktop_pet::service::utils {

AudioProcessor::AudioProcessor() = default;

AudioProcessor::~AudioProcessor() { shutdown(); }

ma_format AudioProcessor::toMiniaudioFormat(AudioFormat fmt) const {
    switch (fmt) {
    case AudioFormat::F32:
        return ma_format_f32;
    case AudioFormat::S16:
        return ma_format_s16;
    default:
        return ma_format_f32;
    }
}

static AudioFormat fromMiniaudioFormat(ma_format fmt) {
    return fmt == ma_format_s16 ? AudioFormat::S16 : AudioFormat::F32;
}

static float dbfsFromRms(float rms) {
    if (rms <= 1e-9f) {
        return -90.0f;
    }
    return 20.0f * std::log10(rms);
}

static std::size_t bytesPerSampleFor(naw::desktop_pet::service::utils::AudioFormat fmt) {
    return fmt == naw::desktop_pet::service::utils::AudioFormat::S16 ? sizeof(std::int16_t) : sizeof(float);
}

std::size_t AudioProcessor::frameSizeBytes(const AudioStreamConfig& cfg) const {
    return ma_get_bytes_per_sample(toMiniaudioFormat(cfg.format)) * cfg.channels;
}

std::optional<AudioError> AudioProcessor::lastError() const {
    std::lock_guard<std::mutex> lock(lastErrorMutex_);
    return lastError_;
}

void AudioProcessor::clearLastError() {
    std::lock_guard<std::mutex> lock(lastErrorMutex_);
    lastError_.reset();
}

void AudioProcessor::setLastError(AudioErrorCode code, const std::string& message) const {
    std::lock_guard<std::mutex> lock(lastErrorMutex_);
    lastError_ = AudioError{code, message};
}

void AudioProcessor::reportError(const CaptureOptions& opts, AudioErrorCode code, const std::string& message) const {
    setLastError(code, message);
    if (opts.onError) {
        opts.onError(AudioError{code, message});
    }
}

std::optional<AudioError> AudioProcessor::validatePcmBuffer(const AudioStreamConfig& stream,
                                                            std::size_t pcmBytes,
                                                            std::size_t minFrames,
                                                            std::size_t maxFrames) {
    if (stream.sampleRate == 0 || stream.channels == 0) {
        return AudioError{AudioErrorCode::InvalidArgs, "invalid AudioStreamConfig: sampleRate/channels must be non-zero"};
    }
    if (!(stream.channels >= 1 && stream.channels <= 8)) {
        return AudioError{AudioErrorCode::InvalidArgs, "invalid AudioStreamConfig: channels out of range"};
    }
    if (!(stream.sampleRate >= 8000 && stream.sampleRate <= 192000)) {
        return AudioError{AudioErrorCode::InvalidArgs, "invalid AudioStreamConfig: sampleRate out of range"};
    }
    const std::size_t bps = bytesPerSampleFor(stream.format);
    if (bps == 0) {
        return AudioError{AudioErrorCode::InvalidArgs, "invalid AudioStreamConfig: unknown format"};
    }
    const std::size_t bpf = bps * stream.channels;
    if (bpf == 0) {
        return AudioError{AudioErrorCode::InvalidArgs, "invalid AudioStreamConfig: frame size is zero"};
    }
    if (pcmBytes == 0) {
        return AudioError{AudioErrorCode::InvalidArgs, "pcmBytes is zero"};
    }
    if (pcmBytes % bpf != 0) {
        return AudioError{AudioErrorCode::InvalidArgs, "pcmBytes is not frame-aligned"};
    }
    const std::size_t frames = pcmBytes / bpf;
    if (minFrames > 0 && frames < minFrames) {
        return AudioError{AudioErrorCode::InvalidArgs, "pcm too short"};
    }
    if (maxFrames > 0 && frames > maxFrames) {
        return AudioError{AudioErrorCode::InvalidArgs, "pcm too long"};
    }
    return std::nullopt;
}

AudioStats AudioProcessor::analyzePcm(const AudioStreamConfig& stream, const void* pcm, std::size_t bytes) {
    AudioStats st{};
    st.sampleRate = stream.sampleRate;
    st.channels = stream.channels;
    st.format = stream.format;

    const auto err = validatePcmBuffer(stream, bytes);
    if (err.has_value() || pcm == nullptr) {
        st.isSilent = true;
        st.dbfs = -90.0f;
        return st;
    }

    const std::size_t bps = bytesPerSampleFor(stream.format);
    const std::size_t bpf = bps * stream.channels;
    const std::size_t frames = bytes / bpf;
    const std::size_t samples = frames * stream.channels;
    st.frames = static_cast<std::uint64_t>(frames);
    st.durationSeconds = stream.sampleRate ? (static_cast<double>(frames) / static_cast<double>(stream.sampleRate)) : 0.0;

    double accum = 0.0;
    float peak = 0.0f;
    std::size_t clippedCount = 0;
    const float clipThreshold = 0.999f;

    if (stream.format == AudioFormat::S16) {
        const auto* p = static_cast<const std::int16_t*>(pcm);
        for (std::size_t i = 0; i < samples; ++i) {
            const float v = static_cast<float>(p[i]) / 32768.0f;
            const float a = std::abs(v);
            peak = std::max(peak, a);
            accum += static_cast<double>(v) * static_cast<double>(v);
            if (a >= clipThreshold) {
                clippedCount++;
            }
        }
    } else {
        const auto* p = static_cast<const float*>(pcm);
        for (std::size_t i = 0; i < samples; ++i) {
            const float v = p[i];
            const float a = std::abs(v);
            peak = std::max(peak, a);
            accum += static_cast<double>(v) * static_cast<double>(v);
            if (a >= clipThreshold) {
                clippedCount++;
            }
        }
    }

    const double rms = std::sqrt(accum / static_cast<double>(samples));
    st.peakAbs = peak;
    st.rms = static_cast<float>(rms);
    st.dbfs = dbfsFromRms(static_cast<float>(rms));
    st.clippedSampleRatio = samples ? static_cast<float>(static_cast<double>(clippedCount) / static_cast<double>(samples)) : 0.0f;

    st.isSilent = (st.rms <= 1e-4f);
    st.isLikelyClipped = (st.peakAbs >= 0.999f && st.clippedSampleRatio >= 0.002f);
    return st;
}

bool AudioProcessor::applyGainInPlace(const AudioStreamConfig& stream, std::vector<std::uint8_t>& pcm, float gainDb) {
    const auto err = validatePcmBuffer(stream, pcm.size());
    if (err.has_value()) {
        return false;
    }
    const float gain = std::pow(10.0f, gainDb / 20.0f);
    if (!(gain > 0.0f) || !std::isfinite(gain)) {
        return false;
    }
    const std::size_t bps = bytesPerSampleFor(stream.format);
    const std::size_t samples = pcm.size() / bps;
    if (stream.format == AudioFormat::S16) {
        auto* p = reinterpret_cast<std::int16_t*>(pcm.data());
        for (std::size_t i = 0; i < samples; ++i) {
            const float v = static_cast<float>(p[i]) * gain;
            const float clamped = std::min(32767.0f, std::max(-32768.0f, v));
            p[i] = static_cast<std::int16_t>(std::lrint(clamped));
        }
    } else {
        auto* p = reinterpret_cast<float*>(pcm.data());
        for (std::size_t i = 0; i < samples; ++i) {
            const float v = p[i] * gain;
            p[i] = std::isfinite(v) ? v : 0.0f;
        }
    }
    return true;
}

bool AudioProcessor::normalizePeakInPlace(const AudioStreamConfig& stream, std::vector<std::uint8_t>& pcm, float targetPeakDb) {
    const auto st = analyzePcm(stream, pcm.data(), pcm.size());
    if (st.peakAbs <= 1e-6f) {
        return false;
    }
    const float targetPeak = std::pow(10.0f, targetPeakDb / 20.0f);
    if (!(targetPeak > 0.0f) || !std::isfinite(targetPeak)) {
        return false;
    }
    const float gain = targetPeak / st.peakAbs;
    const float gainDb = 20.0f * std::log10(std::max(1e-9f, gain));
    return applyGainInPlace(stream, pcm, gainDb);
}

bool AudioProcessor::trimSilenceInPlace(const AudioStreamConfig& stream,
                                        std::vector<std::uint8_t>& pcm,
                                        float thresholdDb,
                                        std::uint32_t minKeepMs) {
    const auto err = validatePcmBuffer(stream, pcm.size());
    if (err.has_value()) {
        return false;
    }
    // 使用现有算法裁剪头尾静音
    AudioProcessor tmp;
    if (!tmp.trimSilence(stream, pcm, thresholdDb)) {
        return false;
    }
    if (pcm.empty()) {
        return false;
    }
    if (minKeepMs > 0 && stream.sampleRate > 0) {
        const std::size_t bpf = bytesPerSampleFor(stream.format) * stream.channels;
        const std::size_t minFrames = static_cast<std::size_t>((static_cast<std::uint64_t>(minKeepMs) * stream.sampleRate) / 1000ULL);
        const std::size_t minBytes = minFrames * bpf;
        if (pcm.size() < minBytes) {
            return false;
        }
    }
    return true;
}

bool AudioProcessor::initialize(const AudioStreamConfig& playbackConfig) {
    if (initialized_) {
        return true;
    }

    ma_engine_config cfg = ma_engine_config_init();
    cfg.channels = playbackConfig.channels;
    cfg.sampleRate = playbackConfig.sampleRate;
    cfg.listenerCount = 1;
    cfg.noDevice = false;

    auto* engine = new ma_engine();
    if (ma_engine_init(&cfg, engine) != MA_SUCCESS) {
        delete engine;
        setLastError(AudioErrorCode::DeviceInitFailed, "ma_engine_init failed");
        return false;
    }

    engine_ = engine;
    playbackConfig_ = playbackConfig;
    initialized_ = true;
    return true;
}

void AudioProcessor::shutdown() {
    stopAll();

    if (captureDevice_ != nullptr) {
        stopCapture();
    }
    
    // *** 清理 VAD 文件 ***
    cleanupOldVadFiles();

    if (initialized_ && engine_ != nullptr) {
        ma_engine_uninit(toEngine(engine_));
        delete toEngine(engine_);
    }

    if (captureContext_ != nullptr) {
        ma_context_uninit(toContext(captureContext_));
        delete toContext(captureContext_);
        captureContext_ = nullptr;
    }

    engine_ = nullptr;
    initialized_ = false;
}

bool AudioProcessor::addSoundHandle(std::uint32_t id, void* sound) {
    std::lock_guard<std::mutex> lock(soundMutex_);
    auto handle = std::make_unique<SoundHandle>();
    handle->sound = sound;
    sounds_.emplace(id, std::move(handle));
    return true;
}

void AudioProcessor::removeSoundHandle(std::uint32_t id) {
    std::lock_guard<std::mutex> lock(soundMutex_);
    sounds_.erase(id);
}

std::optional<std::uint32_t> AudioProcessor::playFile(const std::string& path, const PlaybackOptions& opts) {
    if (!initialized_) {
        return std::nullopt;
    }

    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }

    auto* sound = new ma_sound();
    if (ma_sound_init_from_file(toEngine(engine_), path.c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_STREAM, nullptr, nullptr, sound) != MA_SUCCESS) {
        delete sound;
        return std::nullopt;
    }

    ma_sound_set_looping(sound, opts.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(sound, opts.volume);

    const auto id = nextSoundId_.fetch_add(1);
    addSoundHandle(id, sound);
    ma_sound_start(sound);
    return id;
}

std::optional<std::uint32_t> AudioProcessor::playMemory(const void* data, std::size_t size, const PlaybackOptions& opts) {
    if (!initialized_ || data == nullptr || size == 0) {
        return std::nullopt;
    }

    auto* decoder = new ma_decoder();
    ma_decoder_config decCfg = ma_decoder_config_init(ma_format_f32,
                                                      playbackConfig_.channels,
                                                      playbackConfig_.sampleRate);
    if (ma_decoder_init_memory(data, size, &decCfg, decoder) != MA_SUCCESS) {
        delete decoder;
        return std::nullopt;
    }

    auto* sound = new ma_sound();
    if (ma_sound_init_from_data_source(toEngine(engine_), decoder, MA_SOUND_FLAG_DECODE, nullptr, sound) != MA_SUCCESS) {
        ma_decoder_uninit(decoder);
        delete decoder;
        delete sound;
        return std::nullopt;
    }

    ma_sound_set_looping(sound, opts.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(sound, opts.volume);

    const auto id = nextSoundId_.fetch_add(1);
    addSoundHandle(id, sound);
    {
        std::lock_guard<std::mutex> lock(soundMutex_);
        auto it = sounds_.find(id);
        if (it != sounds_.end()) {
            it->second->decoder = decoder;
        }
    }
    ma_sound_start(sound);
    return id;
}

std::optional<std::uint32_t> AudioProcessor::startStream(const AudioStreamConfig& stream,
                                                         std::size_t bufferFrames,
                                                         const PlaybackOptions& opts) {
    if (!initialized_) {
        return std::nullopt;
    }
    AudioStreamConfig cfg = stream;
    if (cfg.sampleRate == 0) {
        cfg.sampleRate = playbackConfig_.sampleRate != 0 ? playbackConfig_.sampleRate : 48000;
    }
    if (cfg.channels == 0) {
        cfg.channels = playbackConfig_.channels != 0 ? playbackConfig_.channels : 2;
    }
    const auto bpf = frameSizeBytes(cfg);
    if (bpf == 0 || bufferFrames == 0) {
        return std::nullopt;
    }

    auto* src = createStreamSource(cfg, bufferFrames);
    if (src == nullptr) {
        return std::nullopt;
    }

    auto* sound = new ma_sound();
    if (ma_sound_init_from_data_source(toEngine(engine_), &src->base, MA_SOUND_FLAG_DECODE, nullptr, sound) != MA_SUCCESS) {
        destroyStreamSource(src);
        delete sound;
        return std::nullopt;
    }

    ma_sound_set_looping(sound, opts.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(sound, opts.volume);

    const auto id = nextSoundId_.fetch_add(1);
    addSoundHandle(id, sound);
    {
        std::lock_guard<std::mutex> lock(soundMutex_);
        auto it = sounds_.find(id);
        if (it != sounds_.end()) {
            it->second->streamSource = src;
        }
    }
    ma_sound_start(sound);
    return id;
}

bool AudioProcessor::appendStreamData(std::uint32_t soundId, const void* pcm, std::size_t bytes) {
    if (pcm == nullptr || bytes == 0) {
        setLastError(AudioErrorCode::InvalidArgs, "appendStreamData: pcm is null or bytes is zero");
        return false;
    }
    std::lock_guard<std::mutex> lock(soundMutex_);
    auto it = sounds_.find(soundId);
    if (it == sounds_.end() || it->second->streamSource == nullptr) {
        setLastError(AudioErrorCode::NotFound, "appendStreamData: soundId not found or not a stream");
        return false;
    }
    auto* src = reinterpret_cast<StreamSource*>(it->second->streamSource);
    if (bytes % src->bytesPerFrame != 0) {
        setLastError(AudioErrorCode::InvalidArgs, "appendStreamData: bytes is not frame-aligned");
        return false;
    }
    const ma_uint64 frames = static_cast<ma_uint64>(bytes / src->bytesPerFrame);
    const ma_uint64 writable = ma_pcm_rb_available_write(&src->rb);
    if (writable < frames) {
        setLastError(AudioErrorCode::BufferOverflow, "appendStreamData: ring buffer is full (insufficient writable frames)");
        return false; // 缓冲不足，调用方可重试
    }

    ma_uint64 totalWritten = 0;
    const auto* srcBytes = static_cast<const std::uint8_t*>(pcm);
    while (totalWritten < frames) {
        ma_uint32 acquire = static_cast<ma_uint32>(frames - totalWritten);
        void* pWrite = nullptr;
        if (ma_pcm_rb_acquire_write(&src->rb, &acquire, &pWrite) != MA_SUCCESS || pWrite == nullptr || acquire == 0) {
            setLastError(AudioErrorCode::InternalError, "appendStreamData: ma_pcm_rb_acquire_write failed");
            break;
        }
        std::memcpy(pWrite, srcBytes + totalWritten * src->bytesPerFrame, static_cast<std::size_t>(acquire) * src->bytesPerFrame);
        ma_pcm_rb_commit_write(&src->rb, acquire);
        totalWritten += acquire;
    }
    if (totalWritten != frames) {
        setLastError(AudioErrorCode::InternalError, "appendStreamData: partial write");
    }
    return totalWritten == frames;
}

void AudioProcessor::finishStream(std::uint32_t soundId) {
    std::lock_guard<std::mutex> lock(soundMutex_);
    auto it = sounds_.find(soundId);
    if (it == sounds_.end() || it->second->streamSource == nullptr) {
        return;
    }
    auto* src = reinterpret_cast<StreamSource*>(it->second->streamSource);
    src->finished.store(true, std::memory_order_relaxed);
}

bool AudioProcessor::pause(std::uint32_t soundId) {
    std::lock_guard<std::mutex> lock(soundMutex_);
    auto it = sounds_.find(soundId);
    if (it == sounds_.end()) {
        return false;
    }
    ma_uint64 cursor = 0;
    ma_sound_get_cursor_in_pcm_frames(toSound(it->second->sound), &cursor);
    ma_sound_stop(toSound(it->second->sound));
    it->second->pausedFrame = cursor;
    it->second->paused = true;
    return true;
}

bool AudioProcessor::resume(std::uint32_t soundId) {
    std::lock_guard<std::mutex> lock(soundMutex_);
    auto it = sounds_.find(soundId);
    if (it == sounds_.end()) {
        return false;
    }
    if (it->second->paused) {
        ma_sound_seek_to_pcm_frame(toSound(it->second->sound), it->second->pausedFrame);
    }
    ma_sound_start(toSound(it->second->sound));
    it->second->paused = false;
    return true;
}

bool AudioProcessor::stop(std::uint32_t soundId) {
    std::unique_ptr<SoundHandle> handle;
    {
        std::lock_guard<std::mutex> lock(soundMutex_);
        auto it = sounds_.find(soundId);
        if (it == sounds_.end()) {
            return false;
        }
        handle = std::move(it->second);
        sounds_.erase(it);
    }

    auto* sound = toSound(handle->sound);
    ma_sound_stop(sound);
    ma_sound_uninit(sound);
    delete sound;

    if (handle->decoder != nullptr) {
        ma_decoder_uninit(reinterpret_cast<ma_decoder*>(handle->decoder));
        delete reinterpret_cast<ma_decoder*>(handle->decoder);
    }
    if (handle->streamSource != nullptr) {
        destroyStreamSource(reinterpret_cast<StreamSource*>(handle->streamSource));
    }
    return true;
}

bool AudioProcessor::setVolume(std::uint32_t soundId, float volume) {
    std::lock_guard<std::mutex> lock(soundMutex_);
    auto it = sounds_.find(soundId);
    if (it == sounds_.end()) {
        return false;
    }
    ma_sound_set_volume(toSound(it->second->sound), volume);
    return true;
}

bool AudioProcessor::seek(std::uint32_t soundId, std::uint64_t pcmFrame) {
    std::lock_guard<std::mutex> lock(soundMutex_);
    auto it = sounds_.find(soundId);
    if (it == sounds_.end()) {
        return false;
    }
    return ma_sound_seek_to_pcm_frame(toSound(it->second->sound), pcmFrame) == MA_SUCCESS;
}

void AudioProcessor::stopAll() {
    std::vector<std::uint32_t> ids;
    {
        std::lock_guard<std::mutex> lock(soundMutex_);
        ids.reserve(sounds_.size());
        for (auto& kv : sounds_) {
            ids.push_back(kv.first);
        }
    }

    for (auto id : ids) {
        stop(id);
    }
}

void AudioProcessor::dataCallbackCapture(void* pUserData, const void* pInput, std::uint32_t frameCount) {
    auto* self = reinterpret_cast<AudioProcessor*>(pUserData);
    if (self != nullptr) {
        self->onCaptureFrames(pInput, frameCount);
    }
}

void AudioProcessor::onCaptureFrames(const void* pInput, std::uint32_t frameCount) {
    if (!capturing_ || pInput == nullptr) {
        return;
    }

    const auto bytesPerFrame = frameSizeBytes(captureOptions_.stream);
    const auto bytesToCopy = static_cast<std::size_t>(frameCount) * bytesPerFrame;

    if (passiveListening_) {
        // VAD 模式：写环形缓冲，做能量检测和状态机
        const float db = computeDb(pInput, frameCount);
        lastDb_ = db;
        pushRing(pInput, bytesToCopy);

        // 状态机
        if (vadState_ == VADState::Listening) {
            if (db >= vadConfig_.startThresholdDb) {
                currentAboveFrames_ += frameCount;
                currentBelowFrames_ = 0;
                if (currentAboveFrames_ >= startHoldFrames_) {
                    // 触发收集
                    vadState_ = VADState::Collecting;
                    collectingBuffer_.clear();
                    // 预留近 maxBufferSeconds 的内容
                    const auto ringBytes = ring_.sizeBytes;
                    collectingBuffer_.reserve(ringBytes + bytesToCopy * 2);

                    // 复制环形缓冲中的历史数据
                    if (ringBytes > 0) {
                        const std::size_t cap = ring_.capacityBytes;
                        const std::size_t start = (ring_.writePos + cap - ring_.sizeBytes) % cap;
                        if (start + ring_.sizeBytes <= cap) {
                            collectingBuffer_.insert(collectingBuffer_.end(),
                                                     ring_.data.begin() + start,
                                                     ring_.data.begin() + start + ring_.sizeBytes);
                        } else {
                            const std::size_t first = cap - start;
                            collectingBuffer_.insert(collectingBuffer_.end(),
                                                     ring_.data.begin() + start,
                                                     ring_.data.end());
                            collectingBuffer_.insert(collectingBuffer_.end(),
                                                     ring_.data.begin(),
                                                     ring_.data.begin() + (ring_.sizeBytes - first));
                        }
                    }

                    // 本帧追加
                    collectingBuffer_.insert(collectingBuffer_.end(),
                                             static_cast<const std::uint8_t*>(pInput),
                                             static_cast<const std::uint8_t*>(pInput) + bytesToCopy);

                    if (vadCallbacks_.onTrigger) {
                        vadCallbacks_.onTrigger();
                    }
                    currentAboveFrames_ = 0;
                    currentBelowFrames_ = 0;
                }
            } else {
                currentAboveFrames_ = 0;
            }
            return;
        }

        if (vadState_ == VADState::Collecting) {
            // 收集中：持续写入
            collectingBuffer_.insert(collectingBuffer_.end(),
                                     static_cast<const std::uint8_t*>(pInput),
                                     static_cast<const std::uint8_t*>(pInput) + bytesToCopy);

            if (db <= vadConfig_.stopThresholdDb) {
                currentBelowFrames_ += frameCount;
            } else {
                currentBelowFrames_ = 0;
            }

            if (currentBelowFrames_ >= stopHoldFrames_) {
                // *** 关键修复：生成唯一文件名 ***
                AudioStreamConfig stream = captureOptions_.stream;
                std::vector<std::uint8_t> pcm = std::move(collectingBuffer_);
                // 裁剪前后静音，避免播放空白
                if (!trimSilence(stream, pcm, vadConfig_.stopThresholdDb)) {
                    std::cout << "[VAD] skip silent segment\n";
                    currentBelowFrames_ = 0;
                    currentAboveFrames_ = 0;
                    vadState_ = VADState::Listening;
                    return;
                }
                
                // 生成唯一的输出路径（带时间戳和计数器）
                const std::string outPath = generateUniqueVadPath(vadConfig_.outputWavPath);
                auto writePromise = std::make_shared<std::promise<void>>();
                std::shared_future<void> ready = writePromise->get_future().share();

                // 记录文件路径及任务，以便后续清理/等待
                {
                    std::lock_guard<std::mutex> lock(vadFilesMutex_);
                    vadCapturedFiles_.push_back(VADFileRecord{outPath, ready});
                }

                // 在独立线程中写文件并触发回调
                std::thread([this, stream, outPath, data = std::move(pcm), writePromise]() mutable {
                    try {
                        writePcmToWav(outPath, stream, data);
                    } catch (...) {
                        // 保证 promise 被兑现，避免等待卡住
                    }
                    writePromise->set_value();
                    if (vadCallbacks_.onComplete) {
                        vadCallbacks_.onComplete(outPath);
                    }
                }).detach();
                
                currentBelowFrames_ = 0;
                currentAboveFrames_ = 0;
                // 录音完成后，仅保留极短前置历史，避免后续触发从过久前播放
                resetRingAfterCapture(frameSizeBytes(stream), 0.5); // 保留0.5s
                vadState_ = VADState::Listening;
            }
            return;
        }
        return;
    }

    // 非 VAD 模式：普通录音
    if (captureOptions_.storeInMemory) {
        std::lock_guard<std::mutex> lock(captureMutex_);
        const auto currentFrames = captureBuffer_.size() / bytesPerFrame;
        const auto maxFrames = captureOptions_.maxFramesInBuffer;
        const auto writableFrames = static_cast<std::size_t>(std::max<std::int64_t>(0, static_cast<std::int64_t>(maxFrames) - static_cast<std::int64_t>(currentFrames)));
        const auto framesToCopy = std::min<std::size_t>(writableFrames, frameCount);
        const auto bytesCopy = framesToCopy * bytesPerFrame;
        if (bytesCopy > 0) {
            const auto* src = static_cast<const std::uint8_t*>(pInput);
            captureBuffer_.insert(captureBuffer_.end(), src, src + bytesCopy);
        }
    }

    if (captureOptions_.onData) {
        captureOptions_.onData(pInput, bytesToCopy, frameCount);
    }
}

bool AudioProcessor::startCapture(const CaptureOptions& opts) {
    if (capturing_) {
        return true;
    }

    // 确保存在用于枚举输入设备的上下文
    if (captureContext_ == nullptr) {
        auto* ctx = new ma_context();
        if (ma_context_init(nullptr, 0, nullptr, ctx) != MA_SUCCESS) {
            delete ctx;
            reportError(opts, AudioErrorCode::DeviceInitFailed, "startCapture: ma_context_init failed");
            return false;
        }
        captureContext_ = ctx;
    }
    auto* ctx = toContext(captureContext_);

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
    bool useDefault = opts.useDeviceDefault;
    deviceConfig.sampleRate = useDefault ? 0 : opts.stream.sampleRate;
    deviceConfig.capture.format = useDefault ? ma_format_unknown : toMiniaudioFormat(opts.stream.format);
    deviceConfig.capture.channels = useDefault ? 0 : opts.stream.channels;
    deviceConfig.dataCallback = [](ma_device* device, void* /*pOutput*/, const void* pInput, ma_uint32 frameCount) {
        dataCallbackCapture(device->pUserData, pInput, frameCount);
    };
    deviceConfig.pUserData = this;
    const std::uint32_t period = opts.stream.periodSizeInFrames == 0 ? 2048 : opts.stream.periodSizeInFrames;
    deviceConfig.periodSizeInFrames = period;

    // 如果默认输入是 loopback（扬声器），尝试选第一个非 loopback 设备，避免把播放声当作输入
    ma_device_info* playbackInfos = nullptr;
    ma_uint32 playbackCount = 0;
    ma_device_info* captureInfos = nullptr;
    ma_uint32 captureCount = 0;
    if (ma_context_get_devices(ctx, &playbackInfos, &playbackCount, &captureInfos, &captureCount) == MA_SUCCESS &&
        captureInfos != nullptr && captureCount > 0) {
        const ma_device_info* chosen = &captureInfos[0]; // 默认设备
        auto isLoopbackName = [](const ma_device_info& info) {
            std::string name(info.name);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return name.find("loopback") != std::string::npos;
        };
        if (isLoopbackName(*chosen)) {
            for (ma_uint32 i = 0; i < captureCount; ++i) {
                if (!isLoopbackName(captureInfos[i])) {
                    chosen = &captureInfos[i];
                    break;
                }
            }
        }
        deviceConfig.capture.pDeviceID = &chosen->id;
    }

    auto* device = new ma_device();
    if (ma_device_init(ctx, &deviceConfig, device) != MA_SUCCESS) {
        delete device;
        ma_context_uninit(ctx);
        delete ctx;
        captureContext_ = nullptr;
        reportError(opts, AudioErrorCode::DeviceInitFailed, "startCapture: ma_device_init failed");
        return false;
    }

    captureDevice_ = device;
    captureOptions_ = opts;
    // 更新为设备实际参数（避免重采样失真）
    captureOptions_.stream.sampleRate = device->sampleRate;
    captureOptions_.stream.channels = device->capture.channels;
    captureOptions_.stream.format = fromMiniaudioFormat(device->capture.format);
    captureOptions_.stream.periodSizeInFrames = period;
    {
        std::lock_guard<std::mutex> lock(captureMutex_);
        captureBuffer_.clear();
        if (captureOptions_.storeInMemory) {
            const auto bpf = frameSizeBytes(captureOptions_.stream);
            captureBuffer_.reserve(captureOptions_.maxFramesInBuffer * bpf);
        }
    }
    capturing_ = ma_device_start(device) == MA_SUCCESS;
    if (!capturing_) {
        ma_device_uninit(device);
        delete device;
        captureDevice_ = nullptr;
        reportError(opts, AudioErrorCode::DeviceStartFailed, "startCapture: ma_device_start failed");
    }
    return capturing_;
}

void AudioProcessor::stopCapture() {
    if (!capturing_ || captureDevice_ == nullptr) {
        return;
    }

    auto* device = toDevice(captureDevice_);
    if (ma_device_stop(device) != MA_SUCCESS) {
        reportError(captureOptions_, AudioErrorCode::DeviceStopFailed, "stopCapture: ma_device_stop failed");
    }
    ma_device_uninit(device);
    delete device;
    captureDevice_ = nullptr;
    capturing_ = false;

    if (captureContext_ != nullptr) {
        ma_context_uninit(toContext(captureContext_));
        delete toContext(captureContext_);
        captureContext_ = nullptr;
    }
}

CapturedBuffer AudioProcessor::capturedBuffer() const {
    std::lock_guard<std::mutex> lock(captureMutex_);
    return CapturedBuffer{captureOptions_.stream, captureBuffer_};
}

bool AudioProcessor::saveCapturedWav(const std::string& path) const {
    std::lock_guard<std::mutex> lock(captureMutex_);
    if (captureBuffer_.empty()) {
        return false;
    }

    ma_encoder_config encCfg = ma_encoder_config_init(ma_encoding_format_wav,
                                                      toMiniaudioFormat(captureOptions_.stream.format),
                                                      captureOptions_.stream.channels,
                                                      captureOptions_.stream.sampleRate);
    ma_encoder encoder;
    if (ma_encoder_init_file(path.c_str(), &encCfg, &encoder) != MA_SUCCESS) {
        return false;
    }

    const auto frames = captureBuffer_.size() / frameSizeBytes(captureOptions_.stream);
    ma_uint64 framesWritten = 0;
    const auto result = ma_encoder_write_pcm_frames(&encoder, captureBuffer_.data(), frames, &framesWritten);
    ma_encoder_uninit(&encoder);
    return result == MA_SUCCESS && framesWritten == frames;
}

std::optional<AudioStreamConfig> AudioProcessor::probeFile(const std::string& path) const {
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }

    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), nullptr, &decoder) != MA_SUCCESS) {
        return std::nullopt;
    }

    AudioStreamConfig cfg{};
    cfg.sampleRate = decoder.outputSampleRate;
    cfg.channels = decoder.outputChannels;
    cfg.format = decoder.outputFormat == ma_format_s16 ? AudioFormat::S16 : AudioFormat::F32;

    ma_decoder_uninit(&decoder);
    return cfg;
}

std::optional<CapturedBuffer> AudioProcessor::decodeFileToPCM(const std::string& path,
                                                              std::optional<AudioStreamConfig> target) const {
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }

    ma_decoder_config decCfg = ma_decoder_config_init(ma_format_unknown, 0, 0);
    if (target.has_value()) {
        decCfg.format = toMiniaudioFormat(target->format);
        decCfg.channels = target->channels;
        decCfg.sampleRate = target->sampleRate;
    }

    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), target.has_value() ? &decCfg : nullptr, &decoder) != MA_SUCCESS) {
        return std::nullopt;
    }

    ma_uint64 totalFrames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
    const auto bytesPerFrame = ma_get_bytes_per_frame(decoder.outputFormat, decoder.outputChannels);
    std::vector<std::uint8_t> pcm(static_cast<std::size_t>(totalFrames) * bytesPerFrame);

    ma_uint64 framesRead = 0;
    ma_decoder_read_pcm_frames(&decoder, pcm.data(), totalFrames, &framesRead);
    pcm.resize(static_cast<std::size_t>(framesRead) * bytesPerFrame);

    AudioStreamConfig outCfg{};
    outCfg.format = decoder.outputFormat == ma_format_s16 ? AudioFormat::S16 : AudioFormat::F32;
    outCfg.sampleRate = decoder.outputSampleRate;
    outCfg.channels = decoder.outputChannels;

    ma_decoder_uninit(&decoder);
    return CapturedBuffer{outCfg, std::move(pcm)};
}

bool AudioProcessor::writePcmToWav(const std::string& path,
                                   const AudioStreamConfig& stream,
                                   const std::vector<std::uint8_t>& pcm) const {
    if (pcm.empty()) {
        return false;
    }

    const auto frameSize = frameSizeBytes(stream);
    if (frameSize == 0) {
        return false;
    }
    const auto frames = pcm.size() / frameSize;

    ma_encoder_config encCfg = ma_encoder_config_init(ma_encoding_format_wav,
                                                      toMiniaudioFormat(stream.format),
                                                      stream.channels,
                                                      stream.sampleRate);
    ma_encoder encoder;
    if (ma_encoder_init_file(path.c_str(), &encCfg, &encoder) != MA_SUCCESS) {
        return false;
    }

    ma_uint64 framesWritten = 0;
    const auto result = ma_encoder_write_pcm_frames(&encoder, pcm.data(), frames, &framesWritten);
    ma_encoder_uninit(&encoder);
    return result == MA_SUCCESS && framesWritten == frames;
}

float AudioProcessor::computeDb(const void* pcm, std::uint32_t frames) const {
    if (pcm == nullptr || frames == 0) {
        return -90.0f;
    }

    const auto fmt = captureOptions_.stream.format;
    const auto channels = captureOptions_.stream.channels;
    const auto samples = static_cast<std::size_t>(frames) * channels;

    double accum = 0.0;

    if (fmt == AudioFormat::S16) {
        const auto* p = static_cast<const std::int16_t*>(pcm);
        for (std::size_t i = 0; i < samples; ++i) {
            const float v = static_cast<float>(p[i]) / 32768.0f;
            accum += v * v;
        }
    } else {
        const auto* p = static_cast<const float*>(pcm);
        for (std::size_t i = 0; i < samples; ++i) {
            const float v = p[i];
            accum += v * v;
        }
    }

    const double rms = std::sqrt(accum / static_cast<double>(samples));
    return dbfsFromRms(static_cast<float>(rms));
}

bool AudioProcessor::trimSilence(const AudioStreamConfig& stream,
                                 std::vector<std::uint8_t>& pcm,
                                 float thresholdDb) const {
    if (pcm.empty()) {
        return false;
    }
    const auto frameSize = frameSizeBytes(stream);
    if (frameSize == 0 || stream.channels == 0) {
        return false;
    }
    const float threshold = std::pow(10.0f, thresholdDb / 20.0f);
    const std::size_t bytesPerSample = ma_get_bytes_per_sample(toMiniaudioFormat(stream.format));
    const std::size_t totalSamples = pcm.size() / bytesPerSample;

    auto sampleValue = [&](std::size_t idx) -> float {
        if (stream.format == AudioFormat::S16) {
            const auto* p = reinterpret_cast<const std::int16_t*>(pcm.data());
            return std::abs(static_cast<float>(p[idx]) / 32768.0f);
        }
        const auto* p = reinterpret_cast<const float*>(pcm.data());
        return std::abs(p[idx]);
    };

    std::size_t first = totalSamples;
    for (std::size_t i = 0; i < totalSamples; ++i) {
        if (sampleValue(i) >= threshold) {
            first = i;
            break;
        }
    }
    if (first == totalSamples) {
        pcm.clear();
        return false; // 全部静音
    }

    std::size_t last = 0;
    for (std::size_t i = totalSamples; i-- > 0;) {
        if (sampleValue(i) >= threshold) {
            last = i;
            break;
        }
    }

    const std::size_t startFrame = first / stream.channels;
    const std::size_t endFrame = last / stream.channels;
    const std::size_t startByte = startFrame * frameSize;
    const std::size_t endByteExclusive = (endFrame + 1) * frameSize;

    if (startByte == 0 && endByteExclusive == pcm.size()) {
        return true; // 无需裁剪
    }

    std::vector<std::uint8_t> trimmed;
    trimmed.insert(trimmed.end(),
                   pcm.begin() + static_cast<std::ptrdiff_t>(startByte),
                   pcm.begin() + static_cast<std::ptrdiff_t>(endByteExclusive));
    pcm.swap(trimmed);
    return true;
}

void AudioProcessor::ensureRingCapacity(std::size_t bytes, std::size_t bytesPerFrame) {
    const std::uint32_t sr = captureOptions_.stream.sampleRate == 0 ? 48000u : captureOptions_.stream.sampleRate;
    const std::size_t target = bytesPerFrame * static_cast<std::size_t>(vadConfig_.maxBufferSeconds * sr + 0.5);
    const std::size_t cap = std::max<std::size_t>(bytes, target);
    ring_.data.assign(cap, 0);
    ring_.capacityBytes = cap;
    ring_.sizeBytes = 0;
    ring_.writePos = 0;
}

void AudioProcessor::pushRing(const void* pcm, std::size_t bytes) {
    if (ring_.capacityBytes == 0) {
        return;
    }
    const auto* src = static_cast<const std::uint8_t*>(pcm);
    std::size_t remaining = bytes;
    while (remaining > 0) {
        const std::size_t space = ring_.capacityBytes - ring_.writePos;
        const std::size_t chunk = std::min(space, remaining);
        std::copy(src, src + chunk, ring_.data.begin() + ring_.writePos);
        ring_.writePos = (ring_.writePos + chunk) % ring_.capacityBytes;
        src += chunk;
        remaining -= chunk;
        ring_.sizeBytes = std::min(ring_.sizeBytes + chunk, ring_.capacityBytes);
    }
}

void AudioProcessor::appendCollecting(const void* pcm, std::size_t bytes) {
    const auto* src = static_cast<const std::uint8_t*>(pcm);
    collectingBuffer_.insert(collectingBuffer_.end(), src, src + bytes);
}

void AudioProcessor::resetRingAfterCapture(std::size_t bytesPerFrame, double prerollSeconds) {
    if (ring_.capacityBytes == 0) {
        return;
    }
    const std::uint32_t sr = captureOptions_.stream.sampleRate == 0 ? 48000u : captureOptions_.stream.sampleRate;
    const std::size_t targetBytes = static_cast<std::size_t>(std::max(0.0, prerollSeconds) * sr * bytesPerFrame + 0.5);
    if (targetBytes == 0) {
        ring_.sizeBytes = 0;
        ring_.writePos = 0;
        return;
    }

    // 如果当前环形缓冲小于目标，直接清空
    if (ring_.sizeBytes <= targetBytes) {
        return;
    }

    // 仅保留最近 targetBytes 的数据
    const std::size_t cap = ring_.capacityBytes;
    const std::size_t start = (ring_.writePos + cap - targetBytes) % cap;
    std::vector<std::uint8_t> temp(targetBytes, 0);
    if (start + targetBytes <= cap) {
        std::copy(ring_.data.begin() + static_cast<std::ptrdiff_t>(start),
                  ring_.data.begin() + static_cast<std::ptrdiff_t>(start + targetBytes),
                  temp.begin());
    } else {
        const std::size_t first = cap - start;
        std::copy(ring_.data.begin() + static_cast<std::ptrdiff_t>(start),
                  ring_.data.end(),
                  temp.begin());
        std::copy(ring_.data.begin(),
                  ring_.data.begin() + static_cast<std::ptrdiff_t>(targetBytes - first),
                  temp.begin() + static_cast<std::ptrdiff_t>(first));
    }
    ring_.data.swap(temp);
    ring_.capacityBytes = targetBytes;
    ring_.sizeBytes = targetBytes;
    ring_.writePos = targetBytes % ring_.capacityBytes;
}

bool AudioProcessor::startPassiveListening(const VADConfig& vadCfg,
    const CaptureOptions& baseCapture,
    const VADCallbacks& cbs) {
    stopPassiveListening();

    // *** 清理之前的录音文件 ***
    cleanupOldVadFiles();

    VADConfig cfg = vadCfg;
    if (cfg.stopThresholdDb > cfg.startThresholdDb) {
    cfg.stopThresholdDb = cfg.startThresholdDb - 5.0f;
    }
    vadConfig_ = cfg;
    vadCallbacks_ = cbs;
    collectingBuffer_.clear();
    currentAboveFrames_ = 0;
    currentBelowFrames_ = 0;
    lastDb_ = -90.0f;
    vadState_ = VADState::Idle;

    CaptureOptions opts = baseCapture;
    opts.storeInMemory = false;
    opts.onData = nullptr;

    if (!startCapture(opts)) {
    return false;
    }

    const auto bytesPerFrame = frameSizeBytes(captureOptions_.stream);
    const std::size_t minBytes = static_cast<std::size_t>(captureOptions_.stream.sampleRate * bytesPerFrame);
    ensureRingCapacity(minBytes, bytesPerFrame);

    const double sr = captureOptions_.stream.sampleRate == 0 ? 48000.0 : static_cast<double>(captureOptions_.stream.sampleRate);
    startHoldFrames_ = static_cast<std::uint64_t>((static_cast<double>(vadConfig_.startHoldMs) / 1000.0) * sr);
    stopHoldFrames_ = static_cast<std::uint64_t>((static_cast<double>(vadConfig_.stopHoldMs) / 1000.0) * sr);

    vadState_ = VADState::Listening;
    passiveListening_.store(true);
    return true;
}

void AudioProcessor::stopPassiveListening() {
    if (!passiveListening_) {
        return;
    }
    passiveListening_.store(false);
    vadState_ = VADState::Idle;
    collectingBuffer_.clear();
    ring_.data.clear();
    ring_.capacityBytes = 0;
    ring_.sizeBytes = 0;
    ring_.writePos = 0;
    currentAboveFrames_ = 0;
    currentBelowFrames_ = 0;
    stopCapture();
    
    // *** 停止时清理所有录音文件 ***
    cleanupOldVadFiles();
}

void AudioProcessor::cleanupOldVadFiles() {
    std::vector<VADFileRecord> records;
    {
        std::lock_guard<std::mutex> lock(vadFilesMutex_);
        records.swap(vadCapturedFiles_);
    }

    for (auto& record : records) {
        try {
            if (record.ready.valid()) {
                record.ready.wait();
            }
            if (std::filesystem::exists(record.path)) {
                std::filesystem::remove(record.path);
                std::cout << "[VAD] Cleaned up: " << record.path << "\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "[VAD] Failed to delete " << record.path << ": " << e.what() << "\n";
        }
    }
}

std::string AudioProcessor::generateUniqueVadPath(const std::string& basePath) {
    // 获取当前时间戳（毫秒）
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    // 获取递增的计数器
    uint32_t captureId = vadCaptureCounter_.fetch_add(1, std::memory_order_relaxed);
    
    // 构建唯一文件名
    std::string outPath = basePath;
    size_t dotPos = outPath.find_last_of('.');
    
    if (dotPos != std::string::npos) {
        // 有扩展名：在扩展名前插入时间戳和计数器
        // 例如：vad_capture.wav -> vad_capture_1703001234567_0.wav
        outPath = outPath.substr(0, dotPos) + "_" + 
                  std::to_string(timestamp) + "_" +
                  std::to_string(captureId) + 
                  outPath.substr(dotPos);
    } else {
        // 无扩展名：直接追加
        outPath += "_" + std::to_string(timestamp) + "_" + std::to_string(captureId);
    }
    
    return outPath;
}

bool AudioProcessor::removeVadFile(const std::string& path) {
    std::optional<VADFileRecord> record;
    {
        std::lock_guard<std::mutex> lock(vadFilesMutex_);
        auto it = std::find_if(vadCapturedFiles_.begin(), vadCapturedFiles_.end(),
                               [&](const VADFileRecord& r) { return r.path == path; });
        if (it != vadCapturedFiles_.end()) {
            record = *it;
            vadCapturedFiles_.erase(it);
        }
    }

    // 等待写入完成
    if (record.has_value() && record->ready.valid()) {
        record->ready.wait();
    }

    try {
        if (std::filesystem::exists(path)) {
            std::filesystem::remove(path);
            std::cout << "[VAD] Deleted: " << path << "\n";
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[VAD] Failed to delete " << path << ": " << e.what() << "\n";
        return false;
    }
}

} // namespace naw::desktop_pet::service::utils

