#include "naw/desktop_pet/service/utils/AudioProcessor.h"

#include <algorithm>
#include <filesystem>
#include <miniaudio.h>

namespace {
ma_device* toDevice(void* ptr) { return reinterpret_cast<ma_device*>(ptr); }
ma_engine* toEngine(void* ptr) { return reinterpret_cast<ma_engine*>(ptr); }
ma_sound* toSound(void* ptr) { return reinterpret_cast<ma_sound*>(ptr); }
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

std::size_t AudioProcessor::frameSizeBytes(const AudioStreamConfig& cfg) const {
    return ma_get_bytes_per_sample(toMiniaudioFormat(cfg.format)) * cfg.channels;
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

    if (initialized_ && engine_ != nullptr) {
        ma_engine_uninit(toEngine(engine_));
        delete toEngine(engine_);
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

    auto* device = new ma_device();
    if (ma_device_init(nullptr, &deviceConfig, device) != MA_SUCCESS) {
        delete device;
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
    }
    return capturing_;
}

void AudioProcessor::stopCapture() {
    if (!capturing_ || captureDevice_ == nullptr) {
        return;
    }

    auto* device = toDevice(captureDevice_);
    ma_device_stop(device);
    ma_device_uninit(device);
    delete device;
    captureDevice_ = nullptr;
    capturing_ = false;
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

} // namespace naw::desktop_pet::service::utils

