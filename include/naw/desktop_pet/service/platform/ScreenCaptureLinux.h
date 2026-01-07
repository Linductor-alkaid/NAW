#pragma once

#include "naw/desktop_pet/service/ScreenCapture.h"

#ifdef __linux__

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace naw::desktop_pet::service::platform {

/**
 * @brief Linux平台屏幕采集实现
 * 
 * 支持X11和Wayland两种显示服务器：
 * - X11: 使用XGetImage或XShmGetImage（共享内存，性能更好）
 * - Wayland: 使用D-Bus Portal API（org.freedesktop.portal.Screenshot）
 * 
 * 自动检测当前显示服务器类型并选择相应实现
 */
class ScreenCaptureLinux : public ScreenCapture {
public:
    ScreenCaptureLinux();
    ~ScreenCaptureLinux() override;
    
    // 禁止拷贝
    ScreenCaptureLinux(const ScreenCaptureLinux&) = delete;
    ScreenCaptureLinux& operator=(const ScreenCaptureLinux&) = delete;
    
    // 移动构造
    ScreenCaptureLinux(ScreenCaptureLinux&&) noexcept = default;
    ScreenCaptureLinux& operator=(ScreenCaptureLinux&&) noexcept = default;
    
    std::optional<types::ImageData> captureFullScreen(int32_t displayId = 0) override;
    std::optional<types::ImageData> captureWindow(types::WindowHandle handle) override;
    std::optional<types::ImageData> captureRegion(
        const types::Rect& region, 
        int32_t displayId = 0
    ) override;
    std::vector<types::DisplayInfo> getDisplays() override;
    bool supportsWindowCapture() const override;
    bool supportsRegionCapture() const override;
    std::string getLastError() const override { return lastError_; }
    
private:
    /**
     * @brief 显示服务器类型
     */
    enum class DisplayServer {
        Unknown,
        X11,
        Wayland
    };
    
    /**
     * @brief 检测显示服务器类型
     */
    DisplayServer detectDisplayServer();
    
    /**
     * @brief 初始化X11
     */
    bool initializeX11();
    
    /**
     * @brief 清理X11资源
     */
    void cleanupX11();
    
    /**
     * @brief 使用X11捕获全屏
     */
    std::optional<types::ImageData> captureFullScreenX11(int32_t displayId);
    
    /**
     * @brief 使用X11捕获窗口
     */
    std::optional<types::ImageData> captureWindowX11(types::WindowHandle handle);
    
    /**
     * @brief 使用X11捕获区域
     */
    std::optional<types::ImageData> captureRegionX11(const types::Rect& region);
    
    /**
     * @brief 初始化Wayland（D-Bus Portal）
     */
    bool initializeWayland();
    
    /**
     * @brief 使用Wayland Portal捕获全屏
     */
    std::optional<types::ImageData> captureFullScreenWayland(int32_t displayId);
    
    /**
     * @brief 枚举X11显示器
     */
    void enumerateDisplaysX11();
    
    /**
     * @brief 枚举Wayland显示器
     */
    void enumerateDisplaysWayland();
    
    // 显示服务器类型
    DisplayServer displayServer_{DisplayServer::Unknown};
    
    // X11相关（前向声明，避免包含X11头文件）
    struct X11Context;
    std::unique_ptr<X11Context> x11Context_;
    
    // Wayland相关（D-Bus连接）
    void* dbusConnection_{nullptr}; // DBusConnection*
    
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

#endif // __linux__
