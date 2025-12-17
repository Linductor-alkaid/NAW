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
    cap.storeInMemory = false;

    VADConfig vad{};
    vad.startThresholdDb = -35.0f;
    vad.stopThresholdDb = -40.0f;
    vad.startHoldMs = 200;
    vad.stopHoldMs = 600;
    vad.maxBufferSeconds = 10.0f;
    vad.outputWavPath = "vad_capture.wav";  // 基础文件名，实际会生成带时间戳的文件

    std::atomic<bool> captured{false};
    std::mutex pathMutex;
    std::string savedPath;
    std::optional<std::uint32_t> playbackId;
    auto deleteSaved = [&]() {
        std::string toDelete;
        {
            std::lock_guard<std::mutex> lock(pathMutex);
            toDelete = savedPath;
            savedPath.clear();
        }
        if (!toDelete.empty()) {
            audio.removeVadFile(toDelete);
        }
    };

    VADCallbacks cbs{};
    cbs.onTrigger = []() { 
        std::cout << "[VAD] trigger\n"; 
    };
    
    cbs.onComplete = [&](const std::string& path) {
        std::cout << "[VAD] saved: " << path << "\n";
        
        // *** 关键修复：停止之前的播放 ***
        std::string previousPath;
        {
            std::lock_guard<std::mutex> lock(pathMutex);
            previousPath = savedPath;
        }
        if (playbackId.has_value()) {
            std::cout << "[Playback] stopping previous playback id=" << *playbackId << "\n";
            audio.stop(*playbackId);
            playbackId.reset();
        }
        if (!previousPath.empty()) {
            audio.removeVadFile(previousPath);
        }
        
        {
            std::lock_guard<std::mutex> lock(pathMutex);
            savedPath = path;
        }
        
        // 播放新录制的音频
        auto id = audio.playFile(path);
        if (id.has_value()) {
            playbackId = id;
            std::cout << "[Playback] started new playback id=" << *id << "\n";
        } else {
            std::cout << "[Playback] failed to start\n";
            deleteSaved();
        }
        captured.store(true);
    };

    if (!audio.startPassiveListening(vad, cap, cbs)) {
        std::cerr << "startPassiveListening failed\n";
        return 1;
    }

    std::cout << "Passive listening started...\n";
    std::cout << "Speak to trigger recording (auto saves with timestamp).\n";
    std::cout << "You can trigger multiple times - each will create a new file.\n";
    std::cout << "Previous recordings will be automatically cleaned up.\n";
    std::cout << "Waiting 30s for multiple recordings...\n\n";
    
    // 延长等待时间，允许多次录音测试
    std::this_thread::sleep_for(30s);
    
    std::cout << "\nStopping passive listening...\n";
    audio.stopPassiveListening();
    
    std::cout << "Stopped. Waiting for playback to finish...\n";

    // 给最后一次播放一些时间
    if (captured.load() && playbackId.has_value()) {
        std::this_thread::sleep_for(3s);
    }

    // 停止所有播放
    audio.stopAll();
    deleteSaved();
    
    // shutdown 会自动清理所有 VAD 文件
    audio.shutdown();
    
    std::cout << "Done. All temporary recordings have been cleaned up.\n";
    return 0;
}