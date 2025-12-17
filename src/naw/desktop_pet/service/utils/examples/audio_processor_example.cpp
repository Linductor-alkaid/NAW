#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>

#include "naw/desktop_pet/service/utils/AudioProcessor.h"

using namespace std::chrono_literals;
using naw::desktop_pet::service::utils::AudioFormat;
using naw::desktop_pet::service::utils::AudioProcessor;
using naw::desktop_pet::service::utils::CaptureOptions;
using naw::desktop_pet::service::utils::VADCallbacks;
using naw::desktop_pet::service::utils::VADConfig;

int main() {
    AudioProcessor audio;
    if (!audio.initialize()) {
        std::cerr << "AudioProcessor initialize failed\n";
        return 1;
    }

    CaptureOptions cap{};
    cap.useDeviceDefault = true;
    cap.stream.format = AudioFormat::S16;
    cap.storeInMemory = false; // 被动监听模式不需要主线程存储

    VADConfig vad{};
    vad.startThresholdDb = -35.0f;
    vad.stopThresholdDb = -40.0f;
    vad.startHoldMs = 200;
    vad.stopHoldMs = 600;
    vad.maxBufferSeconds = 10.0f;
    vad.outputWavPath = "vad_capture.wav";

    std::atomic<bool> captured{false};
    std::mutex pathMutex;
    std::string savedPath;
    std::optional<std::uint32_t> playbackId;

    VADCallbacks cbs{};
    cbs.onTrigger = []() { std::cout << "[VAD] trigger\n"; };
    cbs.onComplete = [&](const std::string& path) {
        std::cout << "[VAD] saved: " << path << "\n";
        {
            std::lock_guard<std::mutex> lock(pathMutex);
            savedPath = path;
        }
        auto id = audio.playFile(path);
        if (id.has_value()) {
            playbackId = id;
            std::cout << "[Playback] started id=" << *id << "\n";
        } else {
            std::cout << "[Playback] failed to start\n";
        }
        captured.store(true);
    };

    if (!audio.startPassiveListening(vad, cap, cbs)) {
        std::cerr << "startPassiveListening failed\n";
        return 1;
    }

    std::cout << "Passive listening... speak to trigger (auto saves to vad_capture.wav). Waiting 15s.\n";
    std::this_thread::sleep_for(15s);
    audio.stopPassiveListening();
    std::cout << "Stopped. Waiting a bit for playback...\n";

    // 如果已经捕获并触发了播放，给播放器一点时间输出
    if (captured.load()) {
        std::this_thread::sleep_for(3s);
    }

    // 示例：可选停止所有播放，防止进程退出时仍在播放
    audio.stopAll();
    audio.shutdown();
    std::cout << "Done.\n";
    return 0;
}
