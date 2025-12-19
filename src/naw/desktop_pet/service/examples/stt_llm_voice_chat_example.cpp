#include "naw/desktop_pet/service/APIClient.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/types/ChatMessage.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"
#include "naw/desktop_pet/service/utils/AudioProcessor.h"
#include "naw/desktop_pet/service/utils/HttpClient.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <cstring>
#include <algorithm>

#include <nlohmann/json.hpp>

using naw::desktop_pet::service::APIClient;
using naw::desktop_pet::service::ConfigManager;
using naw::desktop_pet::service::ErrorInfo;
using naw::desktop_pet::service::types::ChatMessage;
using naw::desktop_pet::service::types::ChatRequest;
using naw::desktop_pet::service::types::MessageRole;
using naw::desktop_pet::service::utils::AudioFormat;
using naw::desktop_pet::service::utils::AudioProcessor;
using naw::desktop_pet::service::utils::CaptureOptions;
using naw::desktop_pet::service::utils::HttpClient;
using naw::desktop_pet::service::utils::VADCallbacks;
using naw::desktop_pet::service::utils::VADConfig;

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <windows.h>

static void setupConsoleUtf8() {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    _setmode(_fileno(stdin), _O_U16TEXT);
}

static std::string utf8FromWide(const std::wstring& ws) {
    if (ws.empty()) return {};
    int u8len = WideCharToMultiByte(CP_UTF8,
                                    0,
                                    ws.data(),
                                    static_cast<int>(ws.size()),
                                    nullptr,
                                    0,
                                    nullptr,
                                    nullptr);
    if (u8len <= 0) return {};
    std::string out;
    out.resize(static_cast<size_t>(u8len));
    WideCharToMultiByte(CP_UTF8,
                        0,
                        ws.data(),
                        static_cast<int>(ws.size()),
                        out.data(),
                        u8len,
                        nullptr,
                        nullptr);
    return out;
}

static size_t utf8CompletePrefixLen(std::string_view s) {
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            i += 1;
            continue;
        }

        size_t need = 0;
        if ((c & 0xE0) == 0xC0) need = 2;
        else if ((c & 0xF0) == 0xE0) need = 3;
        else if ((c & 0xF8) == 0xF0) need = 4;
        else {
            i += 1;
            continue;
        }

        if (i + need > s.size()) break;

        bool ok = true;
        for (size_t k = 1; k < need; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            i += 1;
            continue;
        }

        i += need;
    }
    return i;
}

class Utf8ConsoleWriter {
public:
    explicit Utf8ConsoleWriter(DWORD stdHandleId)
        : m_h(GetStdHandle(stdHandleId))
    {}

    void write(std::string_view chunk) {
        if (chunk.empty()) return;
        DWORD mode = 0;
        if (m_h == INVALID_HANDLE_VALUE || m_h == nullptr || GetConsoleMode(m_h, &mode) == 0) {
            if (m_h == GetStdHandle(STD_ERROR_HANDLE)) {
                std::cerr << chunk;
            } else {
                std::cout << chunk;
            }
            return;
        }

        m_pending.append(chunk.data(), chunk.size());
        flushComplete();
    }

    void flush() { flushComplete(true); }

private:
    HANDLE m_h{nullptr};
    std::string m_pending;

    void flushComplete(bool flushAll = false) {
        if (m_pending.empty()) return;
        const size_t prefix = flushAll ? m_pending.size() : utf8CompletePrefixLen(m_pending);
        if (prefix == 0) return;

        const std::string_view ready(m_pending.data(), prefix);
        int wlen = MultiByteToWideChar(CP_UTF8, 0, ready.data(), static_cast<int>(ready.size()), nullptr, 0);
        if (wlen <= 0) {
            DWORD written = 0;
            WriteConsoleA(m_h, ready.data(), static_cast<DWORD>(ready.size()), &written, nullptr);
            m_pending.erase(0, prefix);
            return;
        }
        std::wstring ws;
        ws.resize(static_cast<size_t>(wlen));
        MultiByteToWideChar(CP_UTF8, 0, ready.data(), static_cast<int>(ready.size()), ws.data(), wlen);
        DWORD written = 0;
        WriteConsoleW(m_h, ws.data(), static_cast<DWORD>(ws.size()), &written, nullptr);
        m_pending.erase(0, prefix);
    }
};

static Utf8ConsoleWriter& stdoutWriter() {
    static Utf8ConsoleWriter w(STD_OUTPUT_HANDLE);
    return w;
}
static Utf8ConsoleWriter& stderrWriter() {
    static Utf8ConsoleWriter w(STD_ERROR_HANDLE);
    return w;
}

static bool readLineUtf8(std::string& out) {
    out.clear();
    if (_isatty(_fileno(stdin))) {
        std::wstring wline;
        if (!std::getline(std::wcin, wline)) return false;
        out = utf8FromWide(wline);
        return true;
    }
    return static_cast<bool>(std::getline(std::cin, out));
}

#else
static void setupConsoleUtf8() {}
static bool readLineUtf8(std::string& out) { return static_cast<bool>(std::getline(std::cin, out)); }
#endif

struct SegmentJob {
    std::string wavPath;
};

class SegmentQueue {
public:
    void push(SegmentJob j) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            q_.push(std::move(j));
        }
        cv_.notify_one();
    }

    bool popWait(SegmentJob& out) {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [&] { return stop_ || !q_.empty(); });
        if (stop_ && q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop();
        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
    }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::queue<SegmentJob> q_;
    bool stop_{false};
};

static bool looksLikeEnvPlaceholder(const std::string& s) {
    return s.find("${") != std::string::npos;
}

static std::optional<std::string> readFileToString(const std::string& path, std::string* err) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        if (err) *err = "failed to open file: " + path;
        return std::nullopt;
    }
    ifs.seekg(0, std::ios::end);
    const std::streamoff size = ifs.tellg();
    if (size < 0) {
        if (err) *err = "failed to get file size: " + path;
        return std::nullopt;
    }
    std::string data;
    data.resize(static_cast<size_t>(size));
    ifs.seekg(0, std::ios::beg);
    if (!data.empty()) {
        if (!ifs.read(data.data(), static_cast<std::streamsize>(data.size()))) {
            if (err) *err = "failed to read file: " + path;
            return std::nullopt;
        }
    }
    return data;
}

struct SttConfig {
    bool enabled{false};
    std::string baseUrl;
    std::string apiKey;
    std::string modelId;
    std::optional<std::string> language;
};

static std::optional<SttConfig> readSttConfig(ConfigManager& cfg, std::string* whyNot) {
    SttConfig sc;
    auto enabledJ = cfg.get("multimodal.stt.enabled");
    if (enabledJ && enabledJ->is_boolean()) sc.enabled = enabledJ->get<bool>();

    auto baseUrlJ = cfg.get("multimodal.stt.base_url");
    if (baseUrlJ && baseUrlJ->is_string()) sc.baseUrl = baseUrlJ->get<std::string>();
    if (looksLikeEnvPlaceholder(sc.baseUrl)) sc.baseUrl.clear();

    auto apiKeyJ = cfg.get("multimodal.stt.api_key");
    if (apiKeyJ && apiKeyJ->is_string()) sc.apiKey = apiKeyJ->get<std::string>();

    auto modelJ = cfg.get("multimodal.stt.model_id");
    if (modelJ && modelJ->is_string()) sc.modelId = modelJ->get<std::string>();

    auto langJ = cfg.get("multimodal.stt.language");
    if (langJ && langJ->is_string()) sc.language = langJ->get<std::string>();

    if (!sc.enabled) {
        if (whyNot) *whyNot = "multimodal.stt.enabled is false";
        return std::nullopt;
    }
    if (sc.baseUrl.empty()) {
        // fallback: api.base_url
        auto apiBase = cfg.get("api.base_url");
        if (apiBase && apiBase->is_string()) sc.baseUrl = apiBase->get<std::string>();
    }
    if (sc.apiKey.empty()) {
        // fallback: api.api_key
        auto apiKey = cfg.get("api.api_key");
        if (apiKey && apiKey->is_string()) sc.apiKey = apiKey->get<std::string>();
    }

    if (sc.baseUrl.empty()) {
        if (whyNot) *whyNot = "missing multimodal.stt.base_url (and api.base_url fallback)";
        return std::nullopt;
    }
    if (sc.apiKey.empty() || looksLikeEnvPlaceholder(sc.apiKey)) {
        if (whyNot) *whyNot = "missing multimodal.stt.api_key (and api.api_key fallback); consider env override";
        return std::nullopt;
    }
    if (sc.modelId.empty()) {
        if (whyNot) *whyNot = "missing multimodal.stt.model_id";
        return std::nullopt;
    }

    return sc;
}

struct LlmFilterConfig {
    bool enabled{false};
    std::string modelId;
    std::string promptPath{"src/naw/desktop_pet/service/examples/prompt.txt"};
};

static LlmFilterConfig readLlmFilterConfig(ConfigManager& cfg) {
    LlmFilterConfig lc;
    if (auto j = cfg.get("multimodal.llm_filter.enabled"); j && j->is_boolean()) lc.enabled = j->get<bool>();
    if (auto j = cfg.get("multimodal.llm_filter.model_id"); j && j->is_string()) lc.modelId = j->get<std::string>();
    if (auto j = cfg.get("multimodal.llm_filter.prompt_path"); j && j->is_string()) lc.promptPath = j->get<std::string>();
    return lc;
}

struct LlmFilterResult {
    bool respond{false};
    std::string correctedText;
    std::string confidence;
    std::string reason;
};

static std::optional<LlmFilterResult> parseLlmFilterJson(const std::string& text, std::string* err) {
    // 允许模型包一层 ```json ... ```；尽量提取首个 { ... } 片段
    auto firstBrace = text.find('{');
    auto lastBrace = text.rfind('}');
    if (firstBrace == std::string::npos || lastBrace == std::string::npos || lastBrace <= firstBrace) {
        if (err) *err = "llm1 output has no JSON object: " + text;
        return std::nullopt;
    }
    const std::string jsonPart = text.substr(firstBrace, lastBrace - firstBrace + 1);
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonPart);
    } catch (const std::exception& e) {
        if (err) *err = std::string("llm1 JSON parse failed: ") + e.what() + " raw=" + jsonPart;
        return std::nullopt;
    }

    if (!j.is_object() || !j.contains("respond") || !j["respond"].is_boolean()) {
        if (err) *err = "llm1 JSON missing boolean 'respond': " + j.dump();
        return std::nullopt;
    }

    LlmFilterResult r;
    r.respond = j["respond"].get<bool>();
    if (j.contains("reason") && j["reason"].is_string()) r.reason = j["reason"].get<std::string>();
    if (j.contains("confidence") && j["confidence"].is_string()) r.confidence = j["confidence"].get<std::string>();
    if (r.respond) {
        if (j.contains("corrected_text") && j["corrected_text"].is_string()) {
            r.correctedText = j["corrected_text"].get<std::string>();
        } else {
            // 容错：如果 respond=true 但没有 corrected_text，就用空，让上层回退到原始文本
            r.correctedText.clear();
        }
    }
    return r;
}

static std::optional<LlmFilterResult> runLlm1Filter(APIClient& api,
                                                    const LlmFilterConfig& cfg1,
                                                    const std::string& promptText,
                                                    const std::vector<ChatMessage>& llm2History,
                                                    const std::string& currentInput,
                                                    double timeSinceLastSeconds,
                                                    const std::string& petName,
                                                    std::string* errOut) {
    if (!cfg1.enabled) {
        LlmFilterResult r;
        r.respond = true;
        r.correctedText = currentInput;
        r.confidence = "high";
        r.reason = "llm_filter.disabled";
        return r;
    }
    if (cfg1.modelId.empty()) {
        if (errOut) *errOut = "multimodal.llm_filter.model_id is empty";
        return std::nullopt;
    }

    // 取 llm2 最近 10 轮上下文（避免太长）
    nlohmann::json hist = nlohmann::json::array();
    const size_t maxMsgs = 20; // 10轮*2
    const size_t start = llm2History.size() > maxMsgs ? (llm2History.size() - maxMsgs) : 0;
    for (size_t i = start; i < llm2History.size(); ++i) {
        // ChatMessage::toJson() 是 openai 兼容结构（含 role/content）
        hist.push_back(llm2History[i].toJson());
    }

    nlohmann::json payload;
    payload["conversation_history"] = hist;
    payload["current_input"] = currentInput;
    payload["time_since_last"] = timeSinceLastSeconds;
    payload["pet_name"] = petName;

    ChatRequest req;
    req.model = cfg1.modelId;
    req.temperature = 0.0f; // 过滤器尽量确定性
    req.messages = {
        ChatMessage(MessageRole::System, promptText),
        ChatMessage(MessageRole::User, payload.dump()),
    };

    try {
        auto resp = api.chat(req);
        std::string parseErr;
        auto r = parseLlmFilterJson(resp.content, &parseErr);
        if (!r) {
            if (errOut) *errOut = parseErr;
            return std::nullopt;
        }
        return r;
    } catch (const std::exception& e) {
        if (errOut) *errOut = std::string("llm1 chat failed: ") + e.what();
        return std::nullopt;
    }
}

struct TtsConfig {
    bool enabled{false};
    std::string baseUrl;
    std::string apiKey;
    std::string modelId;
    // SiliconFlow /audio/speech 要求 voice 或 references 至少给一个
    // - voice: 预置音色（例如 moss 模型：fnlp/MOSS-TTSD-v0.5:alex）
    // - references: 用上传后的 uri（speech:xxx:...）作为参考音频（更适合 CosyVoice2）
    std::string voice{};
    std::string referenceUri{}; // 对应 upload-voice 返回的 uri（speech:...）
    std::string referenceText{}; // references[].text（某些服务端要求提供参考音频对应文本）

    // SiliconFlow 使用 response_format（mp3/opus/wav/pcm），不是 format
    std::string responseFormat{"wav"};
    std::optional<int> sampleRate; // 例如 44100
    std::optional<int> pcmChannels; // pcm 输出声道数（1/2），默认 1
    std::optional<float> speed;    // 0.25..4
    std::optional<float> gain;     // -10..10
    std::optional<bool> stream;    // 默认 true（服务端可能会忽略）
};

static std::optional<TtsConfig> readTtsConfig(ConfigManager& cfg, std::string* whyNot) {
    TtsConfig tc;
    if (auto j = cfg.get("multimodal.tts.enabled"); j && j->is_boolean()) tc.enabled = j->get<bool>();
    if (auto j = cfg.get("multimodal.tts.base_url"); j && j->is_string()) tc.baseUrl = j->get<std::string>();
    if (looksLikeEnvPlaceholder(tc.baseUrl)) tc.baseUrl.clear();
    if (auto j = cfg.get("multimodal.tts.api_key"); j && j->is_string()) tc.apiKey = j->get<std::string>();
    if (auto j = cfg.get("multimodal.tts.model_id"); j && j->is_string()) tc.modelId = j->get<std::string>();
    if (auto j = cfg.get("multimodal.tts.voice"); j && j->is_string()) tc.voice = j->get<std::string>();
    if (auto j = cfg.get("multimodal.tts.reference_uri"); j && j->is_string()) tc.referenceUri = j->get<std::string>();
    if (auto j = cfg.get("multimodal.tts.reference_text"); j && j->is_string()) tc.referenceText = j->get<std::string>();

    if (auto j = cfg.get("multimodal.tts.response_format"); j && j->is_string()) tc.responseFormat = j->get<std::string>();
    if (auto j = cfg.get("multimodal.tts.sample_rate"); j && j->is_number_integer()) tc.sampleRate = j->get<int>();
    if (auto j = cfg.get("multimodal.tts.pcm_channels"); j && j->is_number_integer()) tc.pcmChannels = j->get<int>();
    if (auto j = cfg.get("multimodal.tts.speed"); j && j->is_number()) tc.speed = j->get<float>();
    if (auto j = cfg.get("multimodal.tts.gain"); j && j->is_number()) tc.gain = j->get<float>();
    if (auto j = cfg.get("multimodal.tts.stream"); j && j->is_boolean()) tc.stream = j->get<bool>();

    if (!tc.enabled) {
        if (whyNot) *whyNot = "multimodal.tts.enabled is false";
        return std::nullopt;
    }
    if (tc.baseUrl.empty()) {
        if (auto j = cfg.get("api.base_url"); j && j->is_string()) tc.baseUrl = j->get<std::string>();
    }
    if (tc.apiKey.empty()) {
        if (auto j = cfg.get("api.api_key"); j && j->is_string()) tc.apiKey = j->get<std::string>();
    }
    if (tc.baseUrl.empty()) {
        if (whyNot) *whyNot = "missing multimodal.tts.base_url (and api.base_url fallback)";
        return std::nullopt;
    }
    if (tc.apiKey.empty() || looksLikeEnvPlaceholder(tc.apiKey)) {
        if (whyNot) *whyNot = "missing multimodal.tts.api_key (and api.api_key fallback); consider env override";
        return std::nullopt;
    }
    if (tc.modelId.empty()) {
        if (whyNot) *whyNot = "missing multimodal.tts.model_id";
        return std::nullopt;
    }
    // SiliconFlow 要求 voice 或 references 至少一个
    const bool hasVoice = !tc.voice.empty() && tc.voice != "default";
    const bool hasRef = !tc.referenceUri.empty();
    if (!hasVoice && !hasRef) {
        if (whyNot) {
            *whyNot =
                "SiliconFlow TTS requires multimodal.tts.voice OR multimodal.tts.reference_uri. "
                "For CosyVoice2, use upload-voice to get a uri, then set multimodal.tts.reference_uri.";
        }
        return std::nullopt;
    }
    return tc;
}

static std::optional<std::string> synthesizeSpeechViaOpenAICompatible(const TtsConfig& tts,
                                                                      const std::string& text,
                                                                      std::string* errOut) {
    HttpClient client(tts.baseUrl);
    std::map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + tts.apiKey;

    auto buildCommon = [&](nlohmann::json& body) {
        body["model"] = tts.modelId;
        body["input"] = text;
        if (!tts.responseFormat.empty() && tts.responseFormat != "default") {
            body["response_format"] = tts.responseFormat;
        }
        if (tts.sampleRate.has_value()) body["sample_rate"] = *tts.sampleRate;
        if (tts.speed.has_value()) body["speed"] = *tts.speed;
        if (tts.gain.has_value()) body["gain"] = *tts.gain;
        // 目前示例未实现 TTS 流式拼接，因此强制走非流式返回，避免拿到截断音频导致噪声/崩溃。
        body["stream"] = false;
    };

    // 兼容策略：
    // 1) 若配置了 voice：直接走 voice
    // 2) 否则若有 referenceUri：
    //    2.1) 优先把 referenceUri 当作 voice（部分服务端把 speech:... 当 voice 使用）
    //    2.2) 若失败，再回退到 references 形态（audio=uri, text=referenceText）
    std::vector<nlohmann::json> attempts;

    if (!tts.voice.empty() && tts.voice != "default") {
        nlohmann::json b;
        buildCommon(b);
        b["voice"] = tts.voice;
        attempts.push_back(std::move(b));
    } else if (!tts.referenceUri.empty()) {
        {
            nlohmann::json b;
            buildCommon(b);
            b["voice"] = tts.referenceUri;
            attempts.push_back(std::move(b));
        }
        {
            nlohmann::json b;
            buildCommon(b);
            b["references"] = nlohmann::json::array(
                {{{"audio", tts.referenceUri}, {"text", tts.referenceText}}});
            attempts.push_back(std::move(b));
        }
    }

    for (size_t i = 0; i < attempts.size(); ++i) {
        auto resp = client.post("/audio/speech", attempts[i].dump(), "application/json", headers);
        if (resp.isSuccess()) {
            return resp.body; // 二进制音频
        }
        // 如果是 5xx，直接尝试下一个形态（提高成功率）
        // 如果是 4xx，也尝试下一个（常见是 voice 形态不支持）
        if (i == attempts.size() - 1) {
            if (errOut) {
                *errOut = "TTS HTTP failed: status=" + std::to_string(resp.statusCode) +
                          " error=" + resp.error +
                          " body=" + resp.body;
            }
        }
    }
    return std::nullopt;
}

static std::string joinUrl(const std::string& base, const std::string& path) {
    if (base.empty()) return path;
    if (path.empty()) return base;
    if (base.back() == '/' && path.front() == '/') return base + path.substr(1);
    if (base.back() != '/' && path.front() != '/') return base + "/" + path;
    return base + path;
}

static bool isProbablyJson(std::string_view chunk) {
    // 仅靠 “首个非空白字符是否是 {/[” 对二进制 PCM 很不安全（随机字节也可能碰巧是 '{'）。
    // 这里做更严格的启发式：必须以 '{'/'[' 开头且后续一小段主要是可打印字符。
    size_t i = 0;
    while (i < chunk.size() && (chunk[i] == ' ' || chunk[i] == '\r' || chunk[i] == '\n' || chunk[i] == '\t')) ++i;
    if (i >= chunk.size()) return false;
    const char first = chunk[i];
    if (first != '{' && first != '[') return false;
    // 4. 检查前128字节的可打印字符比例（更长的采样窗口）
    const size_t scan = std::min<size_t>(chunk.size() - i, 128);
    if (scan < 32) return false; // 至少需要32字节来确认
    
    size_t printable = 0;
    size_t nullBytes = 0; // 统计null字节（PCM数据中常见）
    
    for (size_t k = 0; k < scan; ++k) {
        unsigned char c = static_cast<unsigned char>(chunk[i + k]);
        if (c == 0) {
            nullBytes++;
            continue; // null字节在JSON中极少见，但在PCM中常见
        }
        if (c == '\r' || c == '\n' || c == '\t') {
            printable++;
            continue;
        }
        if (c >= 32 && c <= 126) {
            printable++;
            continue;
        }
    }
    
    // 5. 如果null字节过多，很可能是二进制数据
    if (nullBytes > scan / 4) return false; // 超过25%的null字节，很可能是PCM
    
    // 6. 可打印字符必须超过85%（更严格）
    if (printable * 100 < scan * 85) return false;
    
    // 7. 检查是否包含JSON常见关键字（额外验证）
    std::string_view sample(chunk.data() + i, std::min<size_t>(scan, 256));
    const bool hasJsonKeywords = 
        sample.find("error") != std::string::npos ||
        sample.find("message") != std::string::npos ||
        sample.find("code") != std::string::npos ||
        sample.find("\"") != std::string::npos; // 至少有一个引号
    
    // 8. 如果可打印字符比例高但没有JSON特征，可能是其他文本格式，保守处理
    if (!hasJsonKeywords && printable * 100 < scan * 95) return false;
    
    return true;
}

static bool containsCaseInsensitive(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return false;
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool ok = true;
        for (size_t k = 0; k < needle.size(); ++k) {
            if (lower(static_cast<unsigned char>(haystack[i + k])) !=
                lower(static_cast<unsigned char>(needle[k]))) {
                ok = false;
                break;
            }
        }
        if (ok) return true;
    }
    return false;
}

static bool shouldSpeakTts(const std::string& text, int maxChars) {
    if (text.empty()) return false;
    if (maxChars > 0 && static_cast<int>(text.size()) > maxChars) return false;
    // 代码/公式：直接拒绝播报
    if (text.find("```") != std::string::npos) return false;
    if (text.find("\\frac") != std::string::npos || text.find("\\sum") != std::string::npos ||
        text.find("\\int") != std::string::npos || text.find("$$") != std::string::npos) {
        return false;
    }
    // 符号密度过高通常读起来很糟
    const std::string symbols = "{}[]()<>`=/*_^\\\\|";
    int symCount = 0;
    int nonSpace = 0;
    for (unsigned char c : text) {
        if (c <= 32) continue;
        nonSpace++;
        if (symbols.find(static_cast<char>(c)) != std::string::npos) symCount++;
    }
    if (nonSpace > 0) {
        const double ratio = static_cast<double>(symCount) / static_cast<double>(nonSpace);
        if (ratio >= 0.18) return false;
    }
    return true;
}

static std::optional<std::uint32_t> ttsPcmStreamToPlayback(const TtsConfig& tts,
                                                           const std::string& text,
                                                           AudioProcessor& audio,
                                                           std::optional<std::uint32_t> previousId,
                                                           std::atomic<bool>& playbackActive,
                                                           std::atomic<long long>& ignoreUntilMs,
                                                           int tailIgnoreMs,
                                                           std::string* errOut) {
    // 停止上一次播放（避免多路同时播导致资源/线程压力）
    if (previousId.has_value()) {
        audio.stop(*previousId);
    }

    // 音频流参数：默认按 S16LE 输出；声道数做成可配置（否则声道不匹配容易炸麦/噪声）
    naw::desktop_pet::service::utils::AudioStreamConfig stream;
    stream.format = AudioFormat::S16;
    stream.channels = static_cast<std::uint32_t>(tts.pcmChannels.value_or(1));
    stream.sampleRate = tts.sampleRate.has_value() ? static_cast<std::uint32_t>(*tts.sampleRate) : 44100;

    // 播放期间标记 active，用于 VAD gate
    playbackActive.store(true, std::memory_order_release);

    // 增大缓冲到 ~3s，降低欠载导致的“频繁小噪声”
    auto soundId = audio.startStream(stream, static_cast<std::size_t>(stream.sampleRate) * 3);
    if (!soundId.has_value()) {
        playbackActive.store(false, std::memory_order_release);
        if (errOut) *errOut = "AudioProcessor::startStream failed";
        return std::nullopt;
    }

    HttpClient client(tts.baseUrl);

    // 构造请求体：强制 pcm + stream=true
    auto buildCommon = [&](nlohmann::json& body) {
        body["model"] = tts.modelId;
        body["input"] = text;
        body["response_format"] = "pcm";
        body["stream"] = true;
        if (tts.sampleRate.has_value()) body["sample_rate"] = *tts.sampleRate;
        if (tts.speed.has_value()) body["speed"] = *tts.speed;
        if (tts.gain.has_value()) body["gain"] = *tts.gain;
    };

    nlohmann::json body;
    buildCommon(body);
    if (!tts.voice.empty() && tts.voice != "default") {
        body["voice"] = tts.voice;
    } else if (!tts.referenceUri.empty()) {
        // 先用 uri 作为 voice（更贴合 CosyVoice2 习惯）
        body["voice"] = tts.referenceUri;
    }

    naw::desktop_pet::service::utils::HttpRequest req;
    req.method = naw::desktop_pet::service::utils::HttpMethod::POST;
    req.url = joinUrl(tts.baseUrl, "/audio/speech");
    req.timeoutMs = 60000;
    req.followRedirects = true;
    req.body = body.dump();
    req.headers["Authorization"] = "Bearer " + tts.apiKey;
    req.headers["Content-Type"] = "application/json";

    // 处理二进制分块：保证按帧对齐（S16 * channels）
    std::string errorBody;
    std::vector<std::uint8_t> pending;
    pending.reserve(4096);
    bool sawPossibleJson = false;
    std::size_t audioBytesWritten = 0; // 已写入的音频字节数
    const std::size_t kMinAudioBytes = 1024; // 至少写入1KB音频后才允许判断为错误

    req.streamHandler = [&](std::string_view chunk) {
        if (chunk.empty()) return;
        // 改进的错误检测策略：
        // 1. 只有在还没有写入足够音频数据时，才检测JSON错误
        // 2. 即使检测到可能的JSON，也继续处理，直到流结束并检查状态码
        // 3. 如果已经写入了音频数据，说明之前的数据是有效的，不应该因为后续数据而停止
        
        if (!sawPossibleJson && audioBytesWritten < kMinAudioBytes) {
            // 只在初始阶段检测JSON，避免误判已写入的音频数据
            if (isProbablyJson(chunk)) {
                sawPossibleJson = true;
                // 收集可能的错误信息，但不立即停止处理
                if (errorBody.size() < 64 * 1024) {
                    errorBody.append(chunk.data(), chunk.size());
                }
                // 关键改进：即使检测到可能的JSON，也继续处理后续数据
                // 因为可能是服务器先返回错误信息，然后返回音频（虽然不常见）
                // 或者可能是误判，实际是音频数据
                // 真正的错误判断应该在流结束后根据HTTP状态码决定
                return; // 跳过这个chunk，但不设置全局停止标志
            }
        } else if (sawPossibleJson && audioBytesWritten < kMinAudioBytes) {
            // 如果已经检测到可能的JSON，继续收集错误信息
            if (errorBody.size() < 64 * 1024) {
                errorBody.append(chunk.data(), chunk.size());
            }
            return;
        }
        
        // 如果已经写入了足够的音频数据，即使后续看到JSON也不应该停止
        // 因为可能是流式响应中混有元数据，或者检测误判

        // append PCM bytes
        pending.insert(pending.end(),
                       reinterpret_cast<const std::uint8_t*>(chunk.data()),
                       reinterpret_cast<const std::uint8_t*>(chunk.data()) + chunk.size());

        const size_t frameBytes = 2u * static_cast<size_t>(stream.channels); // S16 * channels
        const size_t usable = (pending.size() / frameBytes) * frameBytes;
        if (usable == 0) return;

        // 可能 buffer 满：不要在网络回调里长时间 sleep（会导致后续 chunk 堵塞，出现“只说半句”）。
        // 策略：尽力写，写不进去就保留 pending 等下一次 chunk 再继续。
        size_t offset = 0;
        const size_t kChunk = 4096u * frameBytes; // 4KB * frameBytes 的倍数
        while (offset < usable) {
            const size_t remain = usable - offset;
            const size_t toWrite = (std::min)(remain, kChunk);
            if (audio.appendStreamData(*soundId, pending.data() + offset, toWrite)) {
                offset += toWrite;
                audioBytesWritten += toWrite; // 记录已写入的音频数据量
                continue;
            }
            break;
        }

        // 移除已消费部分
        if (offset == 0) return;
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(offset));
    };

    auto resp = client.executeStream(req);
    
    // 改进的错误处理：只有在HTTP状态码明确表示错误时，才停止播放
    // 如果已经写入了音频数据，即使检测到可能的JSON，也不应该停止
    if (!resp.isSuccess()) {
        audio.stop(*soundId);
        audio.finishStream(*soundId); // 确保流被正确结束
        playbackActive.store(false, std::memory_order_release);
        if (errOut) {
            if (!errorBody.empty()) {
                *errOut = "TTS stream failed: status=" + std::to_string(resp.statusCode) + " body=" + errorBody;
            } else {
                *errOut = "TTS stream failed: status=" + std::to_string(resp.statusCode) + " error=" + resp.error;
            }
        }
        return std::nullopt;
    }
    
    // 即使HTTP状态码成功，如果检测到JSON且没有写入音频数据，也可能是错误
    // 但这种情况应该很少见，因为服务器通常会在错误时返回非2xx状态码
    if (sawPossibleJson && audioBytesWritten < kMinAudioBytes && !errorBody.empty()) {
        // 警告：检测到可能的错误响应，但HTTP状态码是成功的
        // 这种情况可能是服务器实现问题，或者检测误判
        // 为了安全，如果确实没有写入音频数据，则认为是错误
        audio.stop(*soundId);
        audio.finishStream(*soundId);
        playbackActive.store(false, std::memory_order_release);
        if (errOut) {
            *errOut = "TTS stream suspicious: status=" + std::to_string(resp.statusCode) + 
                     " but received JSON-like data with no audio. body=" + errorBody.substr(0, 512);
        }
        return std::nullopt;
    }
    
    // 正常完成：标记流结束，让音频播放完缓冲中的数据
    audio.finishStream(*soundId);
    playbackActive.store(false, std::memory_order_release);
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    ignoreUntilMs.store(nowMs + static_cast<long long>(tailIgnoreMs), std::memory_order_release);

    return soundId;
}

static std::optional<std::string> transcribeWavViaOpenAICompatible(const SttConfig& stt,
                                                                  const std::string& wavPath,
                                                                  std::string* errOut) {
    std::string readErr;
    auto wavData = readFileToString(wavPath, &readErr);
    if (!wavData) {
        if (errOut) *errOut = readErr;
        return std::nullopt;
    }

    HttpClient client(stt.baseUrl);
    std::map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + stt.apiKey;

    std::map<std::string, std::string> fields;
    fields["model"] = stt.modelId;
    if (stt.language.has_value() && !stt.language->empty()) {
        fields["language"] = *stt.language;
    }

    HttpClient::MultipartFile file;
    file.filename = std::filesystem::path(wavPath).filename().string();
    file.contentType = "audio/wav";
    file.data = std::move(*wavData);

    std::map<std::string, HttpClient::MultipartFile> files;
    files["file"] = std::move(file);

    auto resp = client.postMultipart("/audio/transcriptions", fields, files, headers);
    if (!resp.isSuccess()) {
        if (errOut) {
            *errOut = "STT HTTP failed: status=" + std::to_string(resp.statusCode) +
                      " error=" + resp.error +
                      " body=" + resp.body;
        }
        return std::nullopt;
    }

    std::string jsonErr;
    auto j = resp.asJson(&jsonErr);
    if (!j) {
        if (errOut) *errOut = "STT response is not JSON: " + jsonErr + " body=" + resp.body;
        return std::nullopt;
    }

    // OpenAI-compatible: {"text":"..."}
    if (j->contains("text") && (*j)["text"].is_string()) {
        return (*j)["text"].get<std::string>();
    }

    // Some providers may nest results.
    if (j->contains("data") && (*j)["data"].is_object()) {
        const auto& d = (*j)["data"];
        if (d.contains("text") && d["text"].is_string()) return d["text"].get<std::string>();
    }

    if (errOut) *errOut = "STT JSON has no 'text' field: " + j->dump();
    return std::nullopt;
}

static void printHelp() {
    std::cout << "Commands:\n"
              << "  /exit   - quit\n"
              << "  /reset  - clear conversation history\n"
              << "  /help   - show this help\n\n";
}

int main() {
    setupConsoleUtf8();

    ConfigManager cfg;
    ErrorInfo err;
    if (!cfg.loadFromFile("config/ai_service_config.json", &err)) {
        std::cerr << "Failed to load config: " << err.toString() << "\n";
        return 1;
    }
    cfg.applyEnvironmentOverrides();

    std::string sttWhy;
    auto sttCfg = readSttConfig(cfg, &sttWhy);
    if (!sttCfg) {
        std::cerr << "[STT disabled/unavailable] " << sttWhy << "\n";
        std::cerr << "Hint: set config.multimodal.stt.enabled=true and provide base_url/api_key/model_id (env override supported).\n";
        return 1;
    }

    APIClient api(cfg);

    // pet.name
    std::string petName = "NAW";
    if (auto j = cfg.get("pet.name"); j && j->is_string() && !j->get<std::string>().empty()) {
        petName = j->get<std::string>();
    }

    // llm1 filter config + prompt
    const auto llm1Cfg = readLlmFilterConfig(cfg);
    std::string llm1PromptText;
    {
        std::string perr;
        auto p = readFileToString(llm1Cfg.promptPath, &perr);
        if (p) {
            llm1PromptText = *p;
        } else {
            // 允许没有文件时继续（但会导致 llm1 输出不可控），因此只在启用时强提示
            if (llm1Cfg.enabled) {
                std::cerr << "[WARN] failed to read llm_filter prompt_path=" << llm1Cfg.promptPath
                          << " err=" << perr << "\n";
            }
        }
    }

    // tts config（可选）
    std::string ttsWhy;
    auto ttsCfg = readTtsConfig(cfg, &ttsWhy);
    int ttsTailIgnoreMs = 600;
    if (auto j = cfg.get("multimodal.tts.tail_ignore_ms"); j && j->is_number_integer()) {
        ttsTailIgnoreMs = j->get<int>();
    }

    // 回声门控：共享状态（播放期间/尾音窗口）
    std::atomic<bool> playbackActive{false};
    std::atomic<long long> ignoreUntilMs{0};

    AudioProcessor audio;
    if (!audio.initialize()) {
        std::cerr << "AudioProcessor initialize failed\n";
        return 1;
    }

    SegmentQueue jobs;
    std::atomic<bool> running{true};

    std::mutex historyMu;
    std::vector<ChatMessage> history;
    history.emplace_back(
        MessageRole::System,
        "You are a small desktop pet with your own personality. "
        "You are not a generic assistant. "
        "Be brief, warm, and natural. Avoid being overly formal. "
        "If the user is busy, do not interrupt; respond only when appropriate.");

    std::mutex timingMu;
    std::chrono::steady_clock::time_point lastPetResponse = std::chrono::steady_clock::now() - std::chrono::hours(24);

    std::thread worker([&] {
        SegmentJob job;
        std::optional<std::uint32_t> ttsStreamId;
        // 播放门控标志在主线程 cbs.onComplete 中也会读取，所以使用外层共享变量（在 main 中定义）
        while (jobs.popWait(job)) {
            if (!running.load()) break;

            std::string sttErr;
            auto sttText = transcribeWavViaOpenAICompatible(*sttCfg, job.wavPath, &sttErr);
            if (!sttText || sttText->empty()) {
#if defined(_WIN32)
                stderrWriter().write("\n[STT ERROR] ");
                stderrWriter().write(sttErr);
                stderrWriter().write("\n");
                stderrWriter().flush();
#else
                std::cerr << "\n[STT ERROR] " << sttErr << "\n";
#endif
                // 清理录音文件（避免堆积）
                audio.removeVadFile(job.wavPath);
                continue;
            }

#if defined(_WIN32)
            stdoutWriter().write("\nYou(speech)> ");
            stdoutWriter().write(*sttText);
            stdoutWriter().write("\nAssistant> ");
            stdoutWriter().flush();
#else
            std::cout << "\nYou(speech)> " << *sttText << "\nAssistant> " << std::flush;
#endif

            // llm1 gate: decide respond + correct text
            double sinceLast = 0.0;
            {
                std::lock_guard<std::mutex> lk(timingMu);
                const auto now = std::chrono::steady_clock::now();
                sinceLast = std::chrono::duration<double>(now - lastPetResponse).count();
            }

            std::vector<ChatMessage> historySnapshot;
            {
                std::lock_guard<std::mutex> lk(historyMu);
                historySnapshot = history;
            }

            std::string llm1Err;
            auto filterRes = runLlm1Filter(api,
                                           llm1Cfg,
                                           llm1PromptText,
                                           historySnapshot,
                                           *sttText,
                                           sinceLast,
                                           petName,
                                           &llm1Err);
            if (!filterRes) {
#if defined(_WIN32)
                stderrWriter().write("\n[LLM1 ERROR] ");
                stderrWriter().write(llm1Err);
                stderrWriter().write("\n");
                stderrWriter().flush();
#else
                std::cerr << "\n[LLM1 ERROR] " << llm1Err << "\n";
#endif
                audio.removeVadFile(job.wavPath);
                continue;
            }

            if (!filterRes->respond) {
#if defined(_WIN32)
                stderrWriter().write("\n[LLM1] respond=false reason=");
                stderrWriter().write(filterRes->reason);
                stderrWriter().write(" confidence=");
                stderrWriter().write(filterRes->confidence);
                stderrWriter().write("\n");
                stderrWriter().flush();
                stdoutWriter().write("\n(ignored)\n");
                stdoutWriter().flush();
#else
                std::cerr << "\n[LLM1] respond=false reason=" << filterRes->reason
                          << " confidence=" << filterRes->confidence << "\n";
                std::cout << "\n(ignored)\n" << std::flush;
#endif
                audio.removeVadFile(job.wavPath);
                continue;
            }

            // ---- 硬规则降噪/降频（避免桌宠过于打扰）----
            // 1) 低置信度：直接忽略
            if (filterRes->confidence == "low") {
#if defined(_WIN32)
                stderrWriter().write("\n[LLM1] ignored due to low confidence\n");
                stderrWriter().flush();
                stdoutWriter().write("\n(ignored: low confidence)\n");
                stdoutWriter().flush();
#else
                std::cerr << "\n[LLM1] ignored due to low confidence\n";
                std::cout << "\n(ignored: low confidence)\n" << std::flush;
#endif
                audio.removeVadFile(job.wavPath);
                continue;
            }

            const std::string corrected = !filterRes->correctedText.empty() ? filterRes->correctedText : *sttText;
            // 2) 明显无意义极短输入：忽略（例如单个音/符号）
            if (corrected.size() < 2) {
#if defined(_WIN32)
                stderrWriter().write("\n[LLM1] ignored due to too-short input\n");
                stderrWriter().flush();
                stdoutWriter().write("\n(ignored: too short)\n");
                stdoutWriter().flush();
#else
                std::cerr << "\n[LLM1] ignored due to too-short input\n";
                std::cout << "\n(ignored: too short)\n" << std::flush;
#endif
                audio.removeVadFile(job.wavPath);
                continue;
            }

            // 3) 冷却时间：上次说完后的 N 秒内，除非明确叫了名字，否则不回应
            constexpr double kCooldownSeconds = 8.0;
            const bool calledPet = containsCaseInsensitive(corrected, petName) ||
                                   containsCaseInsensitive(*sttText, petName);
            if (sinceLast < kCooldownSeconds && !calledPet) {
#if defined(_WIN32)
                stderrWriter().write("\n[LLM1] ignored due to cooldown\n");
                stderrWriter().flush();
                stdoutWriter().write("\n(ignored: cooldown)\n");
                stdoutWriter().flush();
#else
                std::cerr << "\n[LLM1] ignored due to cooldown\n";
                std::cout << "\n(ignored: cooldown)\n" << std::flush;
#endif
                audio.removeVadFile(job.wavPath);
                continue;
            }

#if defined(_WIN32)
            stderrWriter().write("\n[LLM1] respond=true confidence=");
            stderrWriter().write(filterRes->confidence);
            stderrWriter().write(" reason=");
            stderrWriter().write(filterRes->reason);
            stderrWriter().write("\n");
            stderrWriter().flush();
#else
            std::cerr << "\n[LLM1] respond=true confidence=" << filterRes->confidence
                      << " reason=" << filterRes->reason << "\n";
#endif

            ChatRequest req;
            // llm2 使用与 llm1 相同的模型（若配置了 llm_filter.model_id）
            if (!llm1Cfg.modelId.empty()) {
                req.model = llm1Cfg.modelId;
            } else if (auto m = cfg.get("routing.fallback_model"); m.has_value() && m->is_string()) {
                req.model = m->get<std::string>();
            } else {
                req.model = "deepseek-ai/DeepSeek-R1-0528-Qwen3-8B";
            }
            req.temperature = 0.7f;

            {
                std::lock_guard<std::mutex> lk(historyMu);
                history.emplace_back(MessageRole::User, corrected);
                req.messages = history;
            }

            std::string assistantText;
            APIClient::Callbacks cb;
            cb.onTextDelta = [&](std::string_view d) {
                assistantText.append(d.data(), d.size());
#if defined(_WIN32)
                stdoutWriter().write(d);
                std::cout << std::flush;
#else
                std::cout << d << std::flush;
#endif
            };
            cb.onToolCallDelta = [&](const APIClient::ToolCallDelta&) {
                // 语音聊天示例暂不消费 tool call
            };
            cb.onComplete = [&](const naw::desktop_pet::service::types::ChatResponse&) {
                // no-op
            };
            cb.onError = [&](const naw::desktop_pet::service::ErrorInfo& e) {
#if defined(_WIN32)
                stderrWriter().write("\n[LLM ERROR] ");
                stderrWriter().write(e.toString());
                stderrWriter().write("\n");
                stderrWriter().flush();
#else
                std::cerr << "\n[LLM ERROR] " << e.toString() << "\n";
#endif
            };

            try {
                api.chatStream(req, cb);
            } catch (const std::exception& e) {
#if defined(_WIN32)
                stderrWriter().write("\n[LLM EXC] ");
                stderrWriter().write(e.what());
                stderrWriter().write("\n");
                stderrWriter().flush();
#else
                std::cerr << "\n[LLM EXC] " << e.what() << "\n";
#endif
            }

#if defined(_WIN32)
            stdoutWriter().flush();
            stderrWriter().flush();
#endif

            {
                std::lock_guard<std::mutex> lk(historyMu);
                history.emplace_back(MessageRole::Assistant, assistantText);
            }

            // tts + playback (optional)
            if (ttsCfg.has_value()) {
                std::string ttsErr;
                auto id = ttsPcmStreamToPlayback(*ttsCfg,
                                                 assistantText,
                                                 audio,
                                                 ttsStreamId,
                                                 playbackActive,
                                                 ignoreUntilMs,
                                                 ttsTailIgnoreMs,
                                                 &ttsErr);
                if (!id) {
#if defined(_WIN32)
                    stderrWriter().write("\n[TTS ERROR] ");
                    stderrWriter().write(ttsErr);
                    stderrWriter().write("\n");
                    stderrWriter().flush();
#else
                    std::cerr << "\n[TTS ERROR] " << ttsErr << "\n";
#endif
                } else {
                    ttsStreamId = id;
                }
            } else {
#if defined(_WIN32)
                stderrWriter().write("\n[TTS disabled] ");
                stderrWriter().write(ttsWhy);
                stderrWriter().write("\n");
                stderrWriter().flush();
#else
                std::cerr << "\n[TTS disabled] " << ttsWhy << "\n";
#endif
            }

            {
                std::lock_guard<std::mutex> lk(timingMu);
                lastPetResponse = std::chrono::steady_clock::now();
            }

            // 清理本段录音文件
            audio.removeVadFile(job.wavPath);

#if defined(_WIN32)
            stdoutWriter().write("\n\n(continue speaking...)\n");
            stdoutWriter().flush();
#else
            std::cout << "\n\n(continue speaking...)\n" << std::flush;
#endif
        }
    });

    CaptureOptions cap{};
    cap.useDeviceDefault = true;
    cap.stream.format = AudioFormat::S16;
    cap.storeInMemory = false;

    VADConfig vad{};
    vad.startThresholdDb = -35.0f;
    vad.stopThresholdDb = -40.0f;
    vad.startHoldMs = 200;
    vad.stopHoldMs = 600;
    vad.maxBufferSeconds = 10.0f;
    vad.outputWavPath = "vad_voice_chat.wav";

    VADCallbacks cbs{};
    cbs.onTrigger = [] {
#if defined(_WIN32)
        stdoutWriter().write("\n[VAD] trigger\n");
        stdoutWriter().flush();
#else
        std::cout << "\n[VAD] trigger\n" << std::flush;
#endif
    };
    // 回声门控：不暂停录音/VAD，只在“播放期间+尾音窗口”丢弃片段，避免桌宠自己说话触发 STT→LLM 自激。
    // 注意：由于 onComplete 在 AudioProcessor 内部线程触发，这里仅做轻量判断与删除。
    cbs.onComplete = [&](const std::string& path) {
        const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
        const auto until = ignoreUntilMs.load(std::memory_order_acquire);
        const bool active = playbackActive.load(std::memory_order_acquire);
        if (active || nowMs < until) {
            audio.removeVadFile(path);
            return;
        }
        jobs.push(SegmentJob{path});
    };

    if (!audio.startPassiveListening(vad, cap, cbs)) {
        std::cerr << "startPassiveListening failed\n";
        running.store(false);
        jobs.stop();
        worker.join();
        audio.shutdown();
        return 1;
    }

    std::cout << "STT+LLM voice chat started. Speak to the mic; each segment will be transcribed and sent to LLM.\n";
    printHelp();

    std::string line;
    while (true) {
        std::cout << "\nCmd> ";
        if (!readLineUtf8(line)) break;
        if (line == "/exit") break;
        if (line == "/help") {
            printHelp();
            continue;
        }
        if (line == "/reset") {
            std::lock_guard<std::mutex> lk(historyMu);
            history.clear();
            history.emplace_back(
                MessageRole::System,
                "You are a small desktop pet with your own personality. "
                "You are not a generic assistant. "
                "Be brief, warm, and natural. Avoid being overly formal. "
                "If the user is busy, do not interrupt; respond only when appropriate.");
            std::cout << "History cleared.\n";
            continue;
        }
    }

    std::cout << "\nStopping...\n";
    running.store(false);
    audio.stopPassiveListening();
    jobs.stop();
    if (worker.joinable()) worker.join();
    audio.stopAll();
    audio.shutdown();
    std::cout << "Bye.\n";
    return 0;
}
