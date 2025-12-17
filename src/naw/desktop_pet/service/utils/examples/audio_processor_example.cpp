#include <chrono>
#include <iostream>
#include <thread>

#include "naw/desktop_pet/service/utils/AudioProcessor.h"

using namespace std::chrono_literals;
using naw::desktop_pet::service::utils::AudioFormat;
using naw::desktop_pet::service::utils::AudioProcessor;
using naw::desktop_pet::service::utils::CaptureOptions;

int main() {
    AudioProcessor audio;
    if (!audio.initialize()) {
        std::cerr << "AudioProcessor initialize failed\n";
        return 1;
    }

    CaptureOptions cap{};
    cap.useDeviceDefault = true;            // 使用设备默认采样率/声道/格式，减少重采样
    cap.stream.format = AudioFormat::S16;   // 若设备默认不可获取，将回落到 S16
    cap.storeInMemory = true;
    cap.maxFramesInBuffer = 48000 * 6;      // 6秒上限，足够缓存5秒

    std::cout << "Start capture for 5 seconds...\n";
    if (!audio.startCapture(cap)) {
        std::cerr << "startCapture failed\n";
        return 1;
    }

    std::this_thread::sleep_for(5s);
    audio.stopCapture();

    const std::string out = "capture.wav";
    if (!audio.saveCapturedWav(out)) {
        std::cerr << "saveCapturedWav failed\n";
        return 1;
    }

    std::cout << "Saved wav: " << out << "\n";
    return 0;
}
