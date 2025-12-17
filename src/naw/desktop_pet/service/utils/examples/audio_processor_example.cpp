#include <chrono>
#include <iostream>
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

    VADCallbacks cbs{};
    cbs.onTrigger = []() { std::cout << "[VAD] trigger\n"; };
    cbs.onComplete = [](const std::string& path) { std::cout << "[VAD] saved: " << path << "\n"; };

    if (!audio.startPassiveListening(vad, cap, cbs)) {
        std::cerr << "startPassiveListening failed\n";
        return 1;
    }

    std::cout << "Passive listening... speak to trigger (auto saves to vad_capture.wav). Waiting 15s.\n";
    std::this_thread::sleep_for(15s);
    audio.stopPassiveListening();
    std::cout << "Stopped.\n";
    return 0;
}
