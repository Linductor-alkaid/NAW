#include "naw/desktop_pet/service/SpeechService.h"

#include "naw/desktop_pet/service/utils/HttpTypes.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>

namespace naw::desktop_pet::service {

// ========== 构造函数和析构函数 ==========

SpeechService::SpeechService(ConfigManager& cfg)
    : config_(cfg)
{
}

SpeechService::~SpeechService() {
    shutdown();
}

// ========== 初始化 ==========

bool SpeechService::initialize() {
    if (initialized_) {
        return true;
    }
    
    if (!audioProcessor_.initialize()) {
        return false;
    }
    
    initialized_ = true;
    return true;
}

void SpeechService::shutdown() {
    if (!initialized_) {
        return;
    }
    
    // 先停止所有流式操作
    stopSpeechToTextStream();
    stopTextToSpeechStream();
    stopPassiveListening();
    
    // 确保所有线程都已结束
    {
        std::lock_guard<std::mutex> lock(ttsStreamMutex_);
        if (ttsStreamThread_.joinable()) {
            ttsStreamThread_.join();
        }
    }
    {
        std::lock_guard<std::mutex> lock(sttStreamMutex_);
        if (sttStreamThread_.joinable()) {
            sttStreamThread_.join();
        }
    }
    
    audioProcessor_.shutdown();
    initialized_ = false;
}

// ========== STT功能 ==========

std::optional<SpeechService::STTResult> SpeechService::speechToText(
    const std::string& audioPath,
    const std::optional<STTConfig>& config) {
    
    if (!initialized_) {
        return std::nullopt;
    }
    
    STTConfig sttConfig = config.has_value() ? *config : loadSTTConfigInternal();
    if (!sttConfig.enabled || sttConfig.baseUrl.empty() || sttConfig.apiKey.empty() || sttConfig.modelId.empty()) {
        return std::nullopt;
    }
    
    return executeSTT(audioPath, sttConfig);
}

std::optional<SpeechService::STTResult> SpeechService::speechToText(
    const std::vector<std::uint8_t>& pcmData,
    const utils::AudioStreamConfig& streamConfig,
    const std::optional<STTConfig>& config) {
    
    if (!initialized_ || pcmData.empty()) {
        return std::nullopt;
    }
    
    STTConfig sttConfig = config.has_value() ? *config : loadSTTConfigInternal();
    if (!sttConfig.enabled || sttConfig.baseUrl.empty() || sttConfig.apiKey.empty() || sttConfig.modelId.empty()) {
        return std::nullopt;
    }
    
    return executeSTTFromPCM(pcmData, streamConfig, sttConfig);
}

bool SpeechService::speechToTextStream(
    const std::optional<STTConfig>& config,
    const STTStreamCallbacks& callbacks) {
    
    if (!initialized_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(sttStreamMutex_);
    if (sttStreaming_.load()) {
        return false; // 已经在流式处理中
    }
    
    STTConfig sttConfig = config.has_value() ? *config : loadSTTConfigInternal();
    if (!sttConfig.enabled || sttConfig.baseUrl.empty() || sttConfig.apiKey.empty() || sttConfig.modelId.empty()) {
        return false;
    }
    
    sttStreamConfig_ = sttConfig;
    sttStreamCallbacks_ = callbacks;
    sttStreamStop_.store(false);
    sttStreaming_.store(true);
    sttStreamBuffer_.clear();
    sttAccumulatedText_.clear();
    sttLastChunkTime_ = std::chrono::steady_clock::now();
    
    // 启动工作线程
    if (sttStreamThread_.joinable()) {
        sttStreamThread_.join();
    }
    sttStreamThread_ = std::thread(&SpeechService::sttStreamWorker, this);
    
    return true;
}

void SpeechService::stopSpeechToTextStream() {
    std::thread threadToJoin;
    {
        std::lock_guard<std::mutex> lock(sttStreamMutex_);
        if (!sttStreaming_.load()) {
            return;
        }
        sttStreamStop_.store(true);
        sttStreaming_.store(false);
        
        // 移动线程对象到局部变量，在锁外 join
        if (sttStreamThread_.joinable()) {
            threadToJoin = std::move(sttStreamThread_);
        }
    }
    
    // 停止录音（在锁外执行，避免死锁）
    audioProcessor_.stopCapture();
    
    // 等待工作线程结束（在锁外等待，避免死锁）
    if (threadToJoin.joinable()) {
        threadToJoin.join();
    }
}

// ========== TTS功能 ==========

std::optional<SpeechService::TTSResult> SpeechService::textToSpeech(
    const std::string& text,
    const std::optional<TTSConfig>& config) {
    
    if (!initialized_ || text.empty()) {
        return std::nullopt;
    }
    
    TTSConfig ttsConfig = config.has_value() ? *config : loadTTSConfigInternal();
    if (!ttsConfig.enabled || ttsConfig.baseUrl.empty() || ttsConfig.apiKey.empty() || ttsConfig.modelId.empty()) {
        return std::nullopt;
    }
    
    return executeTTS(text, ttsConfig);
}

bool SpeechService::textToSpeechStream(
    const std::string& text,
    const std::optional<TTSConfig>& config,
    const TTSStreamCallbacks& callbacks) {
    
    if (!initialized_ || text.empty()) {
        return false;
    }
    
    std::thread oldThread;
    {
        std::lock_guard<std::mutex> lock(ttsStreamMutex_);
        if (ttsStreaming_.load()) {
            return false; // 已经在流式处理中
        }
        
        // 如果之前的线程还在运行，移动它到 oldThread（在锁外 join）
        if (ttsStreamThread_.joinable()) {
            oldThread = std::move(ttsStreamThread_);
        }
        
        TTSConfig ttsConfig = config.has_value() ? *config : loadTTSConfigInternal();
        if (!ttsConfig.enabled || ttsConfig.baseUrl.empty() || ttsConfig.apiKey.empty() || ttsConfig.modelId.empty()) {
            // 配置无效，但需要先处理旧线程
            if (oldThread.joinable()) {
                // 在锁外 join
            }
            return false;
        }
        
        // 启动新线程（在锁内启动，但线程会立即开始执行）
        ttsStreaming_.store(true);
        ttsStreamThread_ = std::thread([this, text, ttsConfig, callbacks]() {
        try {
            utils::HttpClient client(ttsConfig.baseUrl);
            
            nlohmann::json body;
            body["model"] = ttsConfig.modelId;
            body["input"] = text;
            body["response_format"] = "pcm"; // 流式必须使用PCM
            body["stream"] = true;
            if (ttsConfig.sampleRate.has_value()) {
                body["sample_rate"] = *ttsConfig.sampleRate;
            }
            if (ttsConfig.speed.has_value()) {
                body["speed"] = *ttsConfig.speed;
            }
            if (ttsConfig.gain.has_value()) {
                body["gain"] = *ttsConfig.gain;
            }
            
            if (!ttsConfig.voice.empty() && ttsConfig.voice != "default") {
                body["voice"] = ttsConfig.voice;
            } else if (!ttsConfig.referenceUri.empty()) {
                body["voice"] = ttsConfig.referenceUri;
            }
            
            utils::HttpRequest req;
            req.method = utils::HttpMethod::POST;
            req.url = joinUrl(ttsConfig.baseUrl, "/audio/speech");
            req.timeoutMs = ttsConfig.timeoutMs;
            req.followRedirects = true;
            req.body = body.dump();
            req.headers["Authorization"] = "Bearer " + ttsConfig.apiKey;
            req.headers["Content-Type"] = "application/json";
            
            std::vector<std::uint8_t> audioBuffer;
            audioBuffer.reserve(4096);
            
            // 使用按值捕获 callbacks，按引用捕获 audioBuffer（audioBuffer 在 lambda 生命周期内有效）
            req.streamHandler = [this, &audioBuffer, callbacks](std::string_view chunk) {
                if (!ttsStreaming_.load()) {
                    return; // 已停止
                }
                
                if (chunk.empty()) {
                    return;
                }
                
                // 检查是否是JSON错误响应
                if (chunk.size() > 0 && (chunk[0] == '{' || chunk[0] == '[')) {
                    // 可能是错误响应，但继续处理
                }
                
                // 追加音频数据
                const std::uint8_t* data = reinterpret_cast<const std::uint8_t*>(chunk.data());
                audioBuffer.insert(audioBuffer.end(), data, data + chunk.size());
                
                // 回调音频块
                if (callbacks.onAudioChunk) {
                    try {
                        callbacks.onAudioChunk(data, chunk.size());
                    } catch (...) {
                        // 忽略回调中的异常，避免崩溃
                    }
                }
            };
            
            auto resp = client.executeStream(req);
            
            if (!resp.isSuccess()) {
                ttsStreaming_.store(false);
                if (callbacks.onError) {
                    ErrorInfo err;
                    err.errorType = ErrorType::NetworkError;
                    err.message = "TTS stream failed: status=" + std::to_string(resp.statusCode) + " error=" + resp.error;
                    callbacks.onError(err);
                }
                return;
            }
            
            ttsStreaming_.store(false);
            
            // 完成回调
            if (callbacks.onComplete) {
                TTSResult result;
                result.audioData = std::move(audioBuffer);
                result.format = "pcm";
                result.sampleRate = ttsConfig.sampleRate.has_value() ? static_cast<std::uint32_t>(*ttsConfig.sampleRate) : 44100;
                result.channels = ttsConfig.pcmChannels.has_value() ? static_cast<std::uint32_t>(*ttsConfig.pcmChannels) : 1;
                callbacks.onComplete(result);
            }
        } catch (...) {
            // 捕获所有异常，避免线程崩溃
            ttsStreaming_.store(false);
            if (callbacks.onError) {
                try {
                    ErrorInfo err;
                    err.errorType = ErrorType::UnknownError;
                    err.message = "TTS stream thread exception";
                    callbacks.onError(err);
                } catch (...) {
                    // 忽略回调中的异常
                }
            }
        }
        }); // 闭合 lambda 和 thread 构造函数
    } // 锁在这里释放
    
    // 在锁外等待旧线程结束
    if (oldThread.joinable()) {
        oldThread.join();
    }
    
    return true;
}

void SpeechService::stopTextToSpeechStream() {
    std::thread threadToJoin;
    {
        std::lock_guard<std::mutex> lock(ttsStreamMutex_);
        ttsStreaming_.store(false);
        
        // 移动线程对象到局部变量，在锁外 join
        if (ttsStreamThread_.joinable()) {
            threadToJoin = std::move(ttsStreamThread_);
        }
    }
    
    // 等待工作线程结束（在锁外等待，避免死锁）
    if (threadToJoin.joinable()) {
        threadToJoin.join();
    }
}

// ========== VAD功能 ==========

bool SpeechService::startPassiveListening(
    const VADConfig& vadConfig,
    const utils::CaptureOptions& captureOptions,
    const VADCallbacks& callbacks) {
    
    if (!initialized_) {
        return false;
    }
    
    return audioProcessor_.startPassiveListening(vadConfig, captureOptions, callbacks);
}

void SpeechService::stopPassiveListening() {
    if (!initialized_) {
        return;
    }
    
    audioProcessor_.stopPassiveListening();
}

bool SpeechService::isPassiveListening() const {
    return audioProcessor_.isPassiveListening();
}

bool SpeechService::removeVadFile(const std::string& path) {
    return audioProcessor_.removeVadFile(path);
}

// ========== 配置管理 ==========

std::optional<SpeechService::STTConfig> SpeechService::loadSTTConfig() const {
    auto config = loadSTTConfigInternal();
    if (config.enabled && !config.baseUrl.empty() && !config.apiKey.empty() && !config.modelId.empty()) {
        return config;
    }
    return std::nullopt;
}

std::optional<SpeechService::TTSConfig> SpeechService::loadTTSConfig() const {
    auto config = loadTTSConfigInternal();
    if (config.enabled && !config.baseUrl.empty() && !config.apiKey.empty() && !config.modelId.empty()) {
        return config;
    }
    return std::nullopt;
}

SpeechService::STTConfig SpeechService::getDefaultSTTConfig() const {
    return loadSTTConfigInternal();
}

SpeechService::TTSConfig SpeechService::getDefaultTTSConfig() const {
    return loadTTSConfigInternal();
}

// ========== 私有方法实现 ==========

SpeechService::STTConfig SpeechService::loadSTTConfigInternal() const {
    STTConfig config;
    
    if (auto j = config_.get("multimodal.stt.enabled"); j && j->is_boolean()) {
        config.enabled = j->get<bool>();
    }
    
    if (auto j = config_.get("multimodal.stt.base_url"); j && j->is_string()) {
        config.baseUrl = j->get<std::string>();
        if (looksLikeEnvPlaceholder(config.baseUrl)) {
            config.baseUrl.clear();
        }
    }
    
    if (auto j = config_.get("multimodal.stt.api_key"); j && j->is_string()) {
        config.apiKey = j->get<std::string>();
    }
    
    if (auto j = config_.get("multimodal.stt.model_id"); j && j->is_string()) {
        config.modelId = j->get<std::string>();
    }
    
    if (auto j = config_.get("multimodal.stt.language"); j && j->is_string()) {
        config.language = j->get<std::string>();
    }
    
    if (auto j = config_.get("multimodal.stt.timeout_ms"); j && j->is_number_integer()) {
        config.timeoutMs = j->get<int>();
    }
    
    if (auto j = config_.get("multimodal.stt.confidence_threshold"); j && j->is_number()) {
        config.confidenceThreshold = j->get<float>();
    }
    
    // Fallback到api.base_url和api.api_key
    if (config.baseUrl.empty()) {
        if (auto j = config_.get("api.base_url"); j && j->is_string()) {
            config.baseUrl = j->get<std::string>();
        }
    }
    
    if (config.apiKey.empty() || looksLikeEnvPlaceholder(config.apiKey)) {
        if (auto j = config_.get("api.api_key"); j && j->is_string()) {
            config.apiKey = j->get<std::string>();
        }
    }
    
    return config;
}

SpeechService::TTSConfig SpeechService::loadTTSConfigInternal() const {
    TTSConfig config;
    
    if (auto j = config_.get("multimodal.tts.enabled"); j && j->is_boolean()) {
        config.enabled = j->get<bool>();
    }
    
    if (auto j = config_.get("multimodal.tts.base_url"); j && j->is_string()) {
        config.baseUrl = j->get<std::string>();
        if (looksLikeEnvPlaceholder(config.baseUrl)) {
            config.baseUrl.clear();
        }
    }
    
    if (auto j = config_.get("multimodal.tts.api_key"); j && j->is_string()) {
        config.apiKey = j->get<std::string>();
    }
    
    if (auto j = config_.get("multimodal.tts.model_id"); j && j->is_string()) {
        config.modelId = j->get<std::string>();
    }
    
    if (auto j = config_.get("multimodal.tts.voice"); j && j->is_string()) {
        config.voice = j->get<std::string>();
    }
    
    if (auto j = config_.get("multimodal.tts.reference_uri"); j && j->is_string()) {
        config.referenceUri = j->get<std::string>();
    }
    
    if (auto j = config_.get("multimodal.tts.reference_text"); j && j->is_string()) {
        config.referenceText = j->get<std::string>();
    }
    
    if (auto j = config_.get("multimodal.tts.response_format"); j && j->is_string()) {
        config.responseFormat = j->get<std::string>();
    }
    
    if (auto j = config_.get("multimodal.tts.sample_rate"); j && j->is_number_integer()) {
        config.sampleRate = j->get<int>();
    }
    
    if (auto j = config_.get("multimodal.tts.pcm_channels"); j && j->is_number_integer()) {
        config.pcmChannels = j->get<int>();
    }
    
    if (auto j = config_.get("multimodal.tts.speed"); j && j->is_number()) {
        config.speed = j->get<float>();
    }
    
    if (auto j = config_.get("multimodal.tts.gain"); j && j->is_number()) {
        config.gain = j->get<float>();
    }
    
    if (auto j = config_.get("multimodal.tts.pitch"); j && j->is_number()) {
        config.pitch = j->get<float>();
    }
    
    if (auto j = config_.get("multimodal.tts.volume"); j && j->is_number()) {
        config.volume = j->get<float>();
    }
    
    if (auto j = config_.get("multimodal.tts.stream"); j && j->is_boolean()) {
        config.stream = j->get<bool>();
    }
    
    if (auto j = config_.get("multimodal.tts.timeout_ms"); j && j->is_number_integer()) {
        config.timeoutMs = j->get<int>();
    }
    
    // Fallback到api.base_url和api.api_key
    if (config.baseUrl.empty()) {
        if (auto j = config_.get("api.base_url"); j && j->is_string()) {
            config.baseUrl = j->get<std::string>();
        }
    }
    
    if (config.apiKey.empty() || looksLikeEnvPlaceholder(config.apiKey)) {
        if (auto j = config_.get("api.api_key"); j && j->is_string()) {
            config.apiKey = j->get<std::string>();
        }
    }
    
    return config;
}

std::optional<SpeechService::STTResult> SpeechService::executeSTT(
    const std::string& audioPath,
    const STTConfig& config) {
    
    if (!std::filesystem::exists(audioPath)) {
        return std::nullopt;
    }
    
    // 使用AudioProcessor探测文件格式
    auto fileConfig = audioProcessor_.probeFile(audioPath);
    if (!fileConfig.has_value()) {
        return std::nullopt;
    }
    
    // 如果文件不是WAV格式，先转换为WAV
    std::string wavPath = audioPath;
    std::string tempWavPath;
    
    // 检查文件扩展名，如果不是.wav，需要转换
    std::string ext = std::filesystem::path(audioPath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    
    if (ext != ".wav") {
        // 解码为PCM，然后转换为WAV
        auto pcmBuffer = audioProcessor_.decodeFileToPCM(audioPath);
        if (!pcmBuffer.has_value()) {
            return std::nullopt;
        }
        
        // 创建临时WAV文件
        tempWavPath = (std::filesystem::temp_directory_path() / ("stt_convert_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav")).string();
        
        if (!audioProcessor_.writePcmToWav(tempWavPath, pcmBuffer->stream, pcmBuffer->data)) {
            return std::nullopt;
        }
        
        wavPath = tempWavPath;
    }
    
    // 读取WAV文件
    std::ifstream file(wavPath, std::ios::binary);
    if (!file) {
        if (!tempWavPath.empty()) {
            try {
                std::filesystem::remove(tempWavPath);
            } catch (...) {}
        }
        return std::nullopt;
    }
    
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0) {
        if (!tempWavPath.empty()) {
            try {
                std::filesystem::remove(tempWavPath);
            } catch (...) {}
        }
        return std::nullopt;
    }
    
    std::string audioData;
    audioData.resize(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(audioData.data(), static_cast<std::streamsize>(audioData.size()))) {
        if (!tempWavPath.empty()) {
            try {
                std::filesystem::remove(tempWavPath);
            } catch (...) {}
        }
        return std::nullopt;
    }
    
    // 使用multipart/form-data上传
    utils::HttpClient client(config.baseUrl);
    // 设置超时时间，确保不会无限等待
    int timeout = config.timeoutMs > 0 ? config.timeoutMs : 30000;
    client.setTimeout(timeout);
    
    // 对于流式STT，禁用重试以避免长时间阻塞
    // 通过检查超时时间来判断是否是流式调用（流式调用通常使用较短的超时时间，<= 2秒）
    if (timeout <= 2000) {
        utils::RetryConfig noRetryConfig;
        noRetryConfig.maxRetries = 0; // 禁用重试
        noRetryConfig.initialDelay = std::chrono::milliseconds(0); // 无延迟
        client.setRetryConfig(noRetryConfig);
    }
    
    std::map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + config.apiKey;
    
    std::map<std::string, std::string> fields;
    fields["model"] = config.modelId;
    if (config.language.has_value() && !config.language->empty()) {
        fields["language"] = *config.language;
    }
    
    utils::HttpClient::MultipartFile filePart;
    filePart.filename = std::filesystem::path(audioPath).filename().string();
    filePart.contentType = "audio/wav";
    filePart.data = std::move(audioData);
    
    std::map<std::string, utils::HttpClient::MultipartFile> files;
    files["file"] = std::move(filePart);
    
    auto resp = client.postMultipart("/audio/transcriptions", fields, files, headers);
    
    // 清理临时文件
    if (!tempWavPath.empty()) {
        try {
            std::filesystem::remove(tempWavPath);
        } catch (...) {
            // 忽略删除失败
        }
    }
    
    if (!resp.isSuccess()) {
        // 错误处理：根据HTTP状态码判断错误类型
        // 可以在这里添加重试逻辑或错误日志
        return std::nullopt;
    }
    
    auto result = parseSTTResponse(resp.body);
    
    // 应用置信度阈值过滤
    if (result.has_value() && config.confidenceThreshold > 0.0f) {
        if (result->confidence < config.confidenceThreshold) {
            return std::nullopt;
        }
    }
    
    return result;
}

std::optional<SpeechService::STTResult> SpeechService::executeSTTFromPCM(
    const std::vector<std::uint8_t>& pcmData,
    const utils::AudioStreamConfig& streamConfig,
    const STTConfig& config) {
    
    // 验证PCM数据
    auto validationError = utils::AudioProcessor::validatePcmBuffer(streamConfig, pcmData.size());
    if (validationError.has_value()) {
        return std::nullopt;
    }
    
    // 音频预处理：归一化（可选，根据配置）
    std::vector<std::uint8_t> processedPcm = pcmData;
    
    // 可选：归一化峰值到-1dBFS
    utils::AudioProcessor::normalizePeakInPlace(streamConfig, processedPcm, -1.0f);
    
    // 可选：裁剪静音（如果配置了）
    // utils::AudioProcessor::trimSilenceInPlace(streamConfig, processedPcm, -40.0f, 50);
    
    // 将PCM数据转换为WAV格式
    std::string tempWavPath = (std::filesystem::temp_directory_path() / "stt_temp.wav").string();
    
    if (!audioProcessor_.writePcmToWav(tempWavPath, streamConfig, processedPcm)) {
        return std::nullopt;
    }
    
    // 使用WAV文件进行STT
    auto result = executeSTT(tempWavPath, config);
    
    // 清理临时文件
    try {
        std::filesystem::remove(tempWavPath);
    } catch (...) {
        // 忽略删除失败
    }
    
    return result;
}

std::optional<SpeechService::TTSResult> SpeechService::executeTTS(
    const std::string& text,
    const TTSConfig& config) {
    
    utils::HttpClient client(config.baseUrl);
    std::map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + config.apiKey;
    
    nlohmann::json body;
    body["model"] = config.modelId;
    body["input"] = text;
    if (!config.responseFormat.empty() && config.responseFormat != "default") {
        body["response_format"] = config.responseFormat;
    }
    if (config.sampleRate.has_value()) {
        body["sample_rate"] = *config.sampleRate;
    }
    if (config.speed.has_value()) {
        body["speed"] = *config.speed;
    }
    if (config.gain.has_value()) {
        body["gain"] = *config.gain;
    }
    // 非流式模式
    body["stream"] = false;
    
    // 音色设置
    if (!config.voice.empty() && config.voice != "default") {
        body["voice"] = config.voice;
    } else if (!config.referenceUri.empty()) {
        body["voice"] = config.referenceUri;
    }
    
    client.setTimeout(config.timeoutMs);
    auto resp = client.post(joinUrl(config.baseUrl, "/audio/speech"), body.dump(), "application/json", headers);
    if (!resp.isSuccess()) {
        // 错误处理：根据HTTP状态码判断错误类型
        // 可以在这里添加重试逻辑或错误日志
        return std::nullopt;
    }
    
    TTSResult result;
    result.audioData.assign(reinterpret_cast<const std::uint8_t*>(resp.body.data()),
                           reinterpret_cast<const std::uint8_t*>(resp.body.data()) + resp.body.size());
    result.format = config.responseFormat;
    result.sampleRate = config.sampleRate.has_value() ? static_cast<std::uint32_t>(*config.sampleRate) : 44100;
    result.channels = config.pcmChannels.has_value() ? static_cast<std::uint32_t>(*config.pcmChannels) : 1;
    
    return result;
}


std::optional<SpeechService::STTResult> SpeechService::parseSTTResponse(const std::string& jsonResponse) const {
    try {
        auto j = nlohmann::json::parse(jsonResponse);
        
        STTResult result;
        
        // OpenAI兼容格式：{"text":"..."}
        if (j.contains("text") && j["text"].is_string()) {
            result.text = j["text"].get<std::string>();
        }
        
        // 嵌套格式：{"data":{"text":"..."}}
        if (result.text.empty() && j.contains("data") && j["data"].is_object()) {
            const auto& d = j["data"];
            if (d.contains("text") && d["text"].is_string()) {
                result.text = d["text"].get<std::string>();
            }
        }
        
        // 置信度（如果API返回）
        if (j.contains("confidence") && j["confidence"].is_number()) {
            result.confidence = j["confidence"].get<float>();
        }
        
        // 时长（如果API返回）
        if (j.contains("duration") && j["duration"].is_number()) {
            result.duration = j["duration"].get<double>();
        }
        
        // 语言（如果API返回）
        if (j.contains("language") && j["language"].is_string()) {
            result.language = j["language"].get<std::string>();
        }
        
        if (result.text.empty()) {
            return std::nullopt;
        }
        
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string SpeechService::joinUrl(const std::string& base, const std::string& path) const {
    if (base.empty()) return path;
    if (path.empty()) return base;
    if (base.back() == '/' && path.front() == '/') return base + path.substr(1);
    if (base.back() != '/' && path.front() != '/') return base + "/" + path;
    return base + path;
}

bool SpeechService::looksLikeEnvPlaceholder(const std::string& s) const {
    return s.find("${") != std::string::npos;
}

// ========== 流式STT内部实现 ==========

void SpeechService::sttStreamWorker() {
    // 配置录音选项
    utils::CaptureOptions captureOptions;
    captureOptions.useDeviceDefault = true;
    captureOptions.stream.format = utils::AudioFormat::S16;
    captureOptions.stream.sampleRate = 16000; // STT常用采样率
    captureOptions.stream.channels = 1; // 单声道
    captureOptions.storeInMemory = false;
    
    // 音频流缓冲区：1秒的音频数据
    const std::size_t chunkDurationMs = 1000; // 1秒
    const std::size_t chunkFrames = (captureOptions.stream.sampleRate * chunkDurationMs) / 1000;
    const std::size_t bytesPerFrame = 2; // S16 = 2 bytes per sample
    const std::size_t chunkBytes = chunkFrames * bytesPerFrame;
    
    std::vector<std::uint8_t> currentChunk;
    currentChunk.reserve(chunkBytes);
    
    captureOptions.onData = [this, &currentChunk, chunkBytes, bytesPerFrame, chunkFrames](
        const void* pcm, std::size_t bytes, std::uint32_t frames) {
        
        if (sttStreamStop_.load()) {
            return;
        }
        
        const std::uint8_t* data = static_cast<const std::uint8_t*>(pcm);
        currentChunk.insert(currentChunk.end(), data, data + bytes);
        
        // 当累积到1秒的数据时，处理一次
        if (currentChunk.size() >= chunkBytes) {
            std::vector<std::uint8_t> chunkToProcess;
            chunkToProcess.swap(currentChunk);
            currentChunk.clear();
            
            utils::AudioStreamConfig streamConfig;
            streamConfig.format = utils::AudioFormat::S16;
            streamConfig.sampleRate = 16000;
            streamConfig.channels = 1;
            
            processSTTChunk(chunkToProcess, streamConfig);
        }
    };
    
    // 启动录音
    if (!audioProcessor_.startCapture(captureOptions)) {
        sttStreaming_.store(false);
        if (sttStreamCallbacks_.onError) {
            ErrorInfo err;
            err.errorType = ErrorType::UnknownError; // InternalError不在ErrorType枚举中，使用UnknownError
            err.message = "Failed to start audio capture for streaming STT";
            sttStreamCallbacks_.onError(err);
        }
        return;
    }
    
    // 等待停止信号
    while (!sttStreamStop_.load() && sttStreaming_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 检查是否有未处理的音频数据（超时处理）
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - sttLastChunkTime_).count();
        
        // 如果超过1.5秒没有处理数据，且当前有累积的数据，处理它
        // 注意：这个时间应该小于API调用的超时时间（1秒），确保不会堆积太多数据
        if (elapsed > 1500 && !currentChunk.empty()) {
            // 超过1.5秒没有新数据，处理当前累积的数据
            std::vector<std::uint8_t> chunkToProcess;
            chunkToProcess.swap(currentChunk);
            currentChunk.clear();
            
            utils::AudioStreamConfig streamConfig;
            streamConfig.format = utils::AudioFormat::S16;
            streamConfig.sampleRate = 16000;
            streamConfig.channels = 1;
            
            processSTTChunk(chunkToProcess, streamConfig);
            // processSTTChunk 内部会更新 sttLastChunkTime_，这里不需要再次更新
        }
    }
    
    // 处理剩余的音频数据
    if (!currentChunk.empty()) {
        utils::AudioStreamConfig streamConfig;
        streamConfig.format = utils::AudioFormat::S16;
        streamConfig.sampleRate = 16000;
        streamConfig.channels = 1;
        
        processSTTChunk(currentChunk, streamConfig);
    }
    
    audioProcessor_.stopCapture();
    sttStreaming_.store(false);
}

void SpeechService::processSTTChunk(
    const std::vector<std::uint8_t>& chunk,
    const utils::AudioStreamConfig& streamConfig) {
    
    if (chunk.empty() || sttStreamStop_.load()) {
        return;
    }
    
    // 在锁保护下读取配置和回调（避免在停止时被修改）
    STTConfig config;
    STTStreamCallbacks callbacks;
    {
        std::lock_guard<std::mutex> lock(sttStreamMutex_);
        if (!sttStreaming_.load()) {
            return; // 已经停止
        }
        config = sttStreamConfig_;
        callbacks = sttStreamCallbacks_;
    }
    
    // 对于流式STT，使用非常短的超时时间（1秒），避免长时间阻塞
    // 这样可以快速失败，不阻塞流式处理循环
    STTConfig streamConfigWithTimeout = config;
    if (streamConfigWithTimeout.timeoutMs <= 0 || streamConfigWithTimeout.timeoutMs > 1000) {
        streamConfigWithTimeout.timeoutMs = 1000; // 1秒超时
    }
    
    // 执行STT识别（在锁外执行，避免长时间持有锁）
    // 注意：即使设置了1秒超时，对于无效的API地址，连接尝试仍可能需要一些时间
    // 但至少不会超过1秒
    auto result = executeSTTFromPCM(chunk, streamConfig, streamConfigWithTimeout);
    
    // 检查是否在API调用期间被停止
    if (sttStreamStop_.load() || !sttStreaming_.load()) {
        return;
    }
    
    // 无论成功与否，都更新最后处理时间（避免超时检查逻辑出现问题）
    sttLastChunkTime_ = std::chrono::steady_clock::now();
    
    if (!result.has_value()) {
        // API调用失败，但不影响流式处理继续
        // 可以选择发送错误回调，或者静默忽略（因为可能是网络问题）
        return;
    }
    
    // 应用置信度阈值
    if (config.confidenceThreshold > 0.0f && 
        result->confidence < config.confidenceThreshold) {
        return;
    }
    
    // 简单的句子边界检测：检查文本是否以句号、问号、感叹号结尾
    bool isCompleteSentence = false;
    if (!result->text.empty()) {
        char lastChar = result->text.back();
        isCompleteSentence = (lastChar == '.' || lastChar == '?' || lastChar == '!' || 
                             lastChar == '。' || lastChar == '？' || lastChar == '！');
    }
    
    // 在锁保护下更新累积文本并发送回调
    std::string accumulatedText;
    {
        std::lock_guard<std::mutex> lock(sttStreamMutex_);
        if (!sttStreaming_.load()) {
            return; // 已经停止
        }
        
        // 累积文本
        if (!sttAccumulatedText_.empty() && !result->text.empty()) {
            // 检查是否需要添加空格
            if (sttAccumulatedText_.back() != ' ' && result->text.front() != ' ') {
                sttAccumulatedText_ += " ";
            }
        }
        sttAccumulatedText_ += result->text;
        accumulatedText = sttAccumulatedText_;
    }
    
    // 在锁外发送回调（避免回调中再次获取锁导致死锁）
    if (callbacks.onPartialText) {
        callbacks.onPartialText(accumulatedText);
    }
    
    // 如果是完整句子，发送最终结果并重置累积文本
    if (isCompleteSentence) {
        if (callbacks.onFinalResult) {
            STTResult finalResult = *result;
            finalResult.text = accumulatedText;
            callbacks.onFinalResult(finalResult);
        }
        
        // 在锁保护下清除累积文本
        std::lock_guard<std::mutex> lock(sttStreamMutex_);
        sttAccumulatedText_.clear();
    }
}

} // namespace naw::desktop_pet::service
