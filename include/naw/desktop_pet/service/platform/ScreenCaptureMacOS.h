#pragma once

#include "naw/desktop_pet/service/ScreenCapture.h"

#ifdef __APPLE__

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace naw::desktop_pet::service::platform {

/**
 * @brief macOS平台屏幕采集实现（使用CoreGraphics）
 * 
 * 使用CoreGraphics框架进行屏幕截图：
 * - CGWindowListCreateImage: 窗口截图
 * - CGDisplayCreateImage: 全屏截图
 * 
 * 注意：macOS 10.15+需要屏幕录制权限
 */
class ScreenCaptureMacOS : public ScreenCapture {
public:
    ScreenCaptureMacOS();
    ~ScreenCaptureMacOS() override;
    
    // 禁止拷贝
    ScreenCaptureMacOS(const ScreenCaptureMacOS&) = delete;
    ScreenCaptureMacOS& operator=(const ScreenCaptureMacOS&) = delete;
    
    // 移动构造
    ScreenCaptureMacOS(ScreenCaptureMacOS&&) noexcept = default;
    ScreenCaptureMacOS& operator=(ScreenCaptureMacOS&&) noexcept = default;
    
    std::optional<types::ImageData> captureFullScreen(int32_t displayId = 0) override;
    std::optional<types::ImageData> captureWindow(types::WindowHandle handle) override;
    std::optional<types::ImageData> captureRegion(
        const types::Rect& region, 
        int32_t displayId = 0
    ) override;
    std::vector<types::DisplayInfo> getDisplays() override;
    bool supportsWindowCapture() const override { return true; }
    bool supportsRegionCapture() const override { return true; }
    std::string getLastError() const override { return lastError_; }
    
    /**
     * @brief 检查屏幕录制权限
     * @return true表示有权限
     */
    bool checkScreenRecordingPermission() const;
    
    /**
     * @brief 请求屏幕录制权限（会弹出系统对话框）
     */
    void requestScreenRecordingPermission() const;
    
private:
    /**
     * @brief 将CGImage转换为ImageData
     */
    std::optional<types::ImageData> cgImageToImageData(void* cgImage); // CGImageRef
    
    /**
     * @brief 枚举显示器
     */
    void enumerateDisplays();
    
    // 显示器信息
    std::vector<types::DisplayInfo> displays_;
    
    // 错误信息
    mutable std::mutex errorMutex_;
    std::string lastError_;
    
    void setLastError(const std::string& error) const {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_ = error;
    }
};

} // namespace naw::desktop_pet::service::platform

#endif // __APPLE__
