#include "naw/desktop_pet/service/ScreenCapture.h"

#ifdef _WIN32
#include "naw/desktop_pet/service/platform/ScreenCaptureWindows.h"
using PlatformImpl = naw::desktop_pet::service::platform::ScreenCaptureWindows;
#else
// 其他平台暂未实现
class PlatformImplStub : public naw::desktop_pet::service::ScreenCapture {
public:
    std::optional<types::ImageData> captureFullScreen(int32_t) override { return std::nullopt; }
    std::optional<types::ImageData> captureWindow(types::WindowHandle) override { return std::nullopt; }
    std::optional<types::ImageData> captureRegion(const types::Rect&, int32_t) override { return std::nullopt; }
    std::vector<types::DisplayInfo> getDisplays() override { return {}; }
    bool supportsWindowCapture() const override { return false; }
    bool supportsRegionCapture() const override { return false; }
    std::string getLastError() const override { return "Platform not supported"; }
};
using PlatformImpl = PlatformImplStub;
#endif

namespace naw::desktop_pet::service {

std::unique_ptr<ScreenCapture> ScreenCapture::create() {
#ifdef _WIN32
    try {
        return std::make_unique<PlatformImpl>();
    } catch (const std::exception& e) {
        // 创建失败，返回nullptr
        return nullptr;
    }
#else
    // 其他平台暂未实现
    return nullptr;
#endif
}

bool ScreenCapture::isSupported() {
#ifdef _WIN32
    // Windows 8+ 支持DXGI Desktop Duplication
    // 简化检查：只要编译通过就认为支持
    return true;
#else
    // 其他平台暂未实现
    return false;
#endif
}

} // namespace naw::desktop_pet::service
