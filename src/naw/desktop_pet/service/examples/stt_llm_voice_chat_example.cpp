#include "naw/desktop_pet/service/APIClient.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/types/ChatMessage.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"
#include "naw/desktop_pet/service/utils/AudioProcessor.h"
#include "naw/desktop_pet/service/utils/HttpClient.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
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
    if (sc.apiKey.empty() || sc.apiKey.find("${") != std::string::npos) {
        if (whyNot) *whyNot = "missing multimodal.stt.api_key (and api.api_key fallback); consider env override";
        return std::nullopt;
    }
    if (sc.modelId.empty()) {
        if (whyNot) *whyNot = "missing multimodal.stt.model_id";
        return std::nullopt;
    }

    return sc;
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

    AudioProcessor audio;
    if (!audio.initialize()) {
        std::cerr << "AudioProcessor initialize failed\n";
        return 1;
    }

    SegmentQueue jobs;
    std::atomic<bool> running{true};

    std::mutex historyMu;
    std::vector<ChatMessage> history;
    history.emplace_back(MessageRole::System, "You are a helpful assistant.");

    std::thread worker([&] {
        SegmentJob job;
        while (jobs.popWait(job)) {
            if (!running.load()) break;

            std::string sttErr;
            auto text = transcribeWavViaOpenAICompatible(*sttCfg, job.wavPath, &sttErr);
            if (!text || text->empty()) {
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
            stdoutWriter().write(*text);
            stdoutWriter().write("\nAssistant> ");
            stdoutWriter().flush();
#else
            std::cout << "\nYou(speech)> " << *text << "\nAssistant> " << std::flush;
#endif

            ChatRequest req;
            if (auto m = cfg.get("routing.fallback_model"); m.has_value() && m->is_string()) {
                req.model = m->get<std::string>();
            } else {
                req.model = "deepseek-ai/DeepSeek-R1-0528-Qwen3-8B";
            }
            req.temperature = 0.7f;

            {
                std::lock_guard<std::mutex> lk(historyMu);
                history.emplace_back(MessageRole::User, *text);
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
    cbs.onComplete = [&](const std::string& path) {
        // 音频线程：只投递任务
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
            history.emplace_back(MessageRole::System, "You are a helpful assistant.");
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
