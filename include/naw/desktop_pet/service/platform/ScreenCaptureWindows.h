#pragma once

#include "naw/desktop_pet/service/ScreenCapture.h"

#ifdef _WIN32

#include <windows.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <wrl/client.h>

// Windows.Graphics.Capture API (Windows 10 1803+)
// 使用 COM 接口，避免依赖 C++/WinRT
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <windows.graphics.capture.h>
#include <windows.foundation.h>
#include <winstring.h>  // WindowsCreateStringReference
#include <roapi.h>
#include <dcomp.h>

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace naw::desktop_pet::service::platform {

/**
 * @brief Windows平台屏幕采集实现（使用DXGI Desktop Duplication API）
 * 
 * 支持：
 * - 全屏截图（支持多显示器）
 * - 窗口截图（使用BitBlt作为回退）
 * - 区域截图
 * 
 * 性能优化：
 * - 使用DXGI硬件加速
 * - 支持增量更新（仅捕获变化区域）
 */
class ScreenCaptureWindows : public ScreenCapture {
public:
    ScreenCaptureWindows();
    ~ScreenCaptureWindows() override;
    
    // 禁止拷贝
    ScreenCaptureWindows(const ScreenCaptureWindows&) = delete;
    ScreenCaptureWindows& operator=(const ScreenCaptureWindows&) = delete;
    
    // 移动构造
    ScreenCaptureWindows(ScreenCaptureWindows&&) noexcept = default;
    ScreenCaptureWindows& operator=(ScreenCaptureWindows&&) noexcept = default;
    
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
     * @brief 检查DXGI是否可用
     * @return true表示DXGI可用，false表示不可用（可能被其他程序占用）
     */
    bool isDXGIAvailable() const;
    
    /**
     * @brief 获取当前使用的截图方法
     * @return "GraphicsCapture", "DXGI" 或 "BitBlt"
     */
    std::string getCaptureMethod() const;
    
    /**
     * @brief 检测可能占用DXGI的程序
     * @return 占用程序的名称列表（可能不完整，只是常见程序的检测）
     */
    std::vector<std::string> detectDXGIOccupyingProcesses() const;
    
private:
    /**
     * @brief 初始化DXGI资源
     * @param displayId 显示器ID
     */
    bool initializeDXGI(int32_t displayId = 0);
    
    /**
     * @brief 清理DXGI资源
     */
    void cleanupDXGI();
    
    /**
     * @brief 使用DXGI捕获显示器
     */
    std::optional<types::ImageData> captureDisplayDXGI(int32_t displayId);
    
    /**
     * @brief 使用BitBlt捕获全屏（回退方案）
     */
    std::optional<types::ImageData> captureFullScreenBitBlt(int32_t displayId);
    
    /**
     * @brief 使用BitBlt捕获窗口
     */
    std::optional<types::ImageData> captureWindowBitBlt(HWND hwnd);
    
    /**
     * @brief 使用BitBlt捕获区域
     */
    std::optional<types::ImageData> captureRegionBitBlt(const types::Rect& region);
    
    /**
     * @brief 将DXGI纹理转换为ImageData
     */
    std::optional<types::ImageData> textureToImageData(
        ID3D11Texture2D* texture,
        uint32_t width,
        uint32_t height
    );
    
    /**
     * @brief 统一的纹理复制到staging（支持增量更新）
     * @param srcTexture 源纹理
     * @param dstTexture 目标staging纹理
     * @param dirtyRects 脏矩形列表（可选，nullptr表示全帧复制）
     * @return 成功返回true
     */
    bool copyGPUTextureToStaging(
        ID3D11Texture2D* srcTexture,
        ID3D11Texture2D* dstTexture,
        const std::vector<types::Rect>* dirtyRects = nullptr
    );
    
    /**
     * @brief 获取显示器句柄
     */
    HMONITOR getMonitorHandle(int32_t displayId);
    
    /**
     * @brief 枚举显示器
     */
    void enumerateDisplays();
    
    /**
     * @brief 初始化 Windows.Graphics.Capture
     */
    bool initializeGraphicsCapture();
    
    /**
     * @brief 清理 Windows.Graphics.Capture 资源
     */
    void cleanupGraphicsCapture();
    
    /**
     * @brief 使用 Windows.Graphics.Capture 捕获全屏
     */
    std::optional<types::ImageData> captureFullScreenGraphicsCapture(int32_t displayId);
    
    // DXGI相关资源
    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> outputDuplication_;
    Microsoft::WRL::ComPtr<IDXGIOutput1> output1_;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 2> stagingTextures_;  // 双缓冲staging texture
    int currentStagingIndex_{0};  // 当前使用的缓冲区索引
    Microsoft::WRL::ComPtr<ID3D11Texture2D> previousFrameTexture_;  // 存储上一帧（用于增量更新）
    Microsoft::WRL::ComPtr<ID3D11Query> query_;  // 用于同步 GPU 操作
    std::mutex stagingMutex_;  // 保护缓冲区切换
    
    // 显示器信息
    std::vector<types::DisplayInfo> displays_;
    std::vector<HMONITOR> monitorHandles_;
    
    // Windows.Graphics.Capture相关资源（使用COM接口）
    bool graphicsCaptureInitialized_{false};
    bool graphicsCaptureAvailable_{false};
    bool graphicsCaptureFirstFrameReceived_{false};  // 标记是否已收到第一帧
    Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem> captureItem_;
    Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePool> framePool_;
    Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureSession> captureSession_;
    Microsoft::WRL::ComPtr<ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice> graphicsDevice_;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 2> graphicsCaptureStagingTextures_;  // 双缓冲staging texture
    int currentGraphicsCaptureStagingIndex_{0};  // 当前使用的缓冲区索引
    
    // 状态
    bool dxgiInitialized_{false};
    bool dxgiAvailable_{false};  // DXGI是否可用（未被占用）
    bool dxgiFirstCapture_{true};  // DXGI首次截图标志
    int32_t currentDisplayId_{-1};
    uint32_t outputWidth_{0};
    uint32_t outputHeight_{0};
    
    // 错误信息
    mutable std::mutex errorMutex_;
    mutable std::string lastError_;
    
    void setLastError(const std::string& error) const {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_ = error;
    }
};

} // namespace naw::desktop_pet::service::platform

#endif // _WIN32
