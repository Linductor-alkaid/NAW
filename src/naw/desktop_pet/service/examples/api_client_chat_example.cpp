#include "naw/desktop_pet/service/APIClient.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/types/ChatMessage.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

using naw::desktop_pet::service::APIClient;
using naw::desktop_pet::service::ConfigManager;
using naw::desktop_pet::service::types::ChatMessage;
using naw::desktop_pet::service::types::ChatRequest;
using naw::desktop_pet::service::types::MessageRole;

// Windows 控制台：默认可能是 CP936(GBK)，而 nlohmann::json 严格要求 UTF-8。
// 这里把输入/输出切到 UTF-8，并提供一层本地编码->UTF-8 转换兜底，避免中文输入触发 invalid UTF-8。
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#include <fcntl.h>

static void setupConsoleUtf8() {
    // 让 std::getline/std::cout 走 UTF-8（仅影响控制台 code page，不改变 std::string 的本质）
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
}

static std::string toUtf8FromConsole(const std::string& s) {
    // 将当前 ANSI code page (CP_ACP) 的字节转成 UTF-8
    if (s.empty()) return s;
    int wlen = MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (wlen <= 0) return s;
    std::wstring ws;
    ws.resize(static_cast<size_t>(wlen));
    MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), ws.data(), wlen);

    int u8len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) return s;
    std::string out;
    out.resize(static_cast<size_t>(u8len));
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), wlen, out.data(), u8len, nullptr, nullptr);
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
            // 非法起始字节：跳过 1 字节（尽量不中断输出）
            i += 1;
            continue;
        }

        if (i + need > s.size()) break; // 不完整，留到下一段

        bool ok = true;
        for (size_t k = 1; k < need; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            // 非法 continuation：跳过 1 字节继续
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
        // 如果不是 console（例如被重定向），直接输出字节
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
            // 理论上不会发生（我们做了边界），兜底：直接输出并清空
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
#else
static void setupConsoleUtf8() {}
static std::string toUtf8FromConsole(const std::string& s) { return s; }
static void writeUtf8ToConsole(std::string_view s) { std::cout << s; }
#endif

static void printHelp() {
    std::cout << "Commands:\n"
              << "  /exit   - quit\n"
              << "  /reset  - clear conversation history\n"
              << "  /help   - show this help\n\n";
}

int main() {
    setupConsoleUtf8();

    ConfigManager cfg;
    naw::desktop_pet::service::ErrorInfo err;

    // 建议使用项目自带配置模板：config/ai_service_config.json
    // 并通过环境变量注入 SILICONFLOW_API_KEY（避免落盘明文）
    if (!cfg.loadFromFile("config/ai_service_config.json", &err)) {
        std::cerr << "Failed to load config: " << err.toString() << "\n";
        return 1;
    }
    cfg.applyEnvironmentOverrides();

    // 如果 api_key 仍是占位符，validate() 会报错（但这里不强制终止，你也可以自行判断）
    const auto issues = cfg.validate();
    for (const auto& s : issues) {
        if (s.rfind("WARN:", 0) == 0) {
            std::cerr << "[WARN] " << s << "\n";
        } else {
            std::cerr << "[ERR ] " << s << "\n";
        }
    }

    APIClient api(cfg);
    std::cout << "Base URL: " << api.getBaseUrl() << "\n";
    std::cout << "API Key : " << api.getApiKeyRedacted() << "\n\n";
    printHelp();

    // 维护上下文
    std::vector<ChatMessage> history;
    // 可选 system prompt
    history.emplace_back(MessageRole::System, "You are a helpful assistant.");

    std::string line;
    while (true) {
        std::cout << "\nYou> ";
        if (!std::getline(std::cin, line)) break;
        // 兜底：将控制台输入转换为 UTF-8（避免 JSON dump/parse 报 invalid UTF-8）
        line = toUtf8FromConsole(line);
        if (line == "/exit") break;
        if (line == "/help") {
            printHelp();
            continue;
        }
        if (line == "/reset") {
            history.clear();
            history.emplace_back(MessageRole::System, "You are a helpful assistant.");
            std::cout << "History cleared.\n";
            continue;
        }
        if (line.empty()) continue;

        history.emplace_back(MessageRole::User, line);

        ChatRequest req;
        // 这里默认使用 config 里的模型（routing 会在更上层做；示例直接读一个常用路径）
        // 若你想指定：把下面 model 改成你需要的 model_id 即可。
        if (auto m = cfg.get("routing.fallback_model"); m.has_value() && m->is_string()) {
            req.model = m->get<std::string>();
        } else if (auto m2 = cfg.get("models"); m2.has_value() && m2->is_array() && !m2->empty() &&
                   (*m2)[0].is_object() && (*m2)[0].contains("model_id") && (*m2)[0]["model_id"].is_string()) {
            req.model = (*m2)[0]["model_id"].get<std::string>();
        } else {
            // 最后兜底：让服务端报错提示
            req.model = "deepseek-ai/DeepSeek-V3";
        }

        req.messages = history;
        req.temperature = 0.7f;

        std::string assistantText;
        std::cout << "Assistant> " << std::flush;

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
        cb.onToolCallDelta = [&](const APIClient::ToolCallDelta& d) {
            // 示例仅打印（上层可按需实时消费 function calling 增量）
            if (!d.nameDelta.empty() || !d.argumentsDelta.empty()) {
#if defined(_WIN32)
                stderrWriter().write("\n[tool_call_delta] index=");
                stderrWriter().write(std::to_string(d.index));
                stderrWriter().write(" id=");
                stderrWriter().write(d.id);
                stderrWriter().write(" name+=");
                stderrWriter().write(d.nameDelta);
                stderrWriter().write(" args+=");
                stderrWriter().write(d.argumentsDelta);
                stderrWriter().write("\n");
#else
                std::cerr << "\n[tool_call_delta] index=" << d.index
                          << " id=" << d.id
                          << " name+=" << d.nameDelta
                          << " args+=" << d.argumentsDelta << "\n";
#endif
                std::cout << "Assistant> " << std::flush;
            }
        };
        cb.onComplete = [&](const naw::desktop_pet::service::types::ChatResponse& r) {
            // 这里 r.content == assistantText（聚合结果）
            (void)r;
        };
        cb.onError = [&](const naw::desktop_pet::service::ErrorInfo& e) {
            std::cerr << "\n[ERROR] " << e.toString() << "\n";
        };

        try {
            api.chatStream(req, cb);
        } catch (const std::exception& e) {
            std::cerr << "\n[EXC] " << e.what() << "\n";
        }

#if defined(_WIN32)
        // 确保最后一个 UTF-8 字符不会卡在 pending 缓冲里
        stdoutWriter().flush();
        stderrWriter().flush();
#endif

        // 把 assistant 回复写回上下文（即使为空也写入，便于对齐轮次）
        history.emplace_back(MessageRole::Assistant, assistantText);
    }

    std::cout << "\nBye.\n";
    return 0;
}

