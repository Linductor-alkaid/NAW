#include "naw/desktop_pet/service/platform/ScreenCaptureWindows.h"

#ifdef _WIN32

#include <algorithm>
#include <array>
#include <sstream>
#include <tlhelp32.h>  // 用于进程枚举
#include <psapi.h>     // 用于获取进程信息
#include <VersionHelpers.h>  // 替代 GetVersionEx

namespace naw::desktop_pet::service::platform {

// ========== 构造函数和析构函数 ==========

ScreenCaptureWindows::ScreenCaptureWindows() {
    enumerateDisplays();
}

ScreenCaptureWindows::~ScreenCaptureWindows() {
    cleanupGraphicsCapture();
    cleanupDXGI();
}

// ========== 公共接口实现 ==========

std::optional<types::ImageData> ScreenCaptureWindows::captureFullScreen(int32_t displayId) {
    // 优先级顺序:Windows.Graphics.Capture > DXGI > BitBlt
    // Windows.Graphics.Capture 支持多程序同时捕获,避免独占问题
    
    std::string graphicsCaptureError;
    std::string dxgiError;
    
    // 1. 优先尝试 Windows.Graphics.Capture (Windows 10 1803+)
    if (graphicsCaptureAvailable_ || (!graphicsCaptureInitialized_ && initializeGraphicsCapture())) {
        auto result = captureFullScreenGraphicsCapture(displayId);
        if (result.has_value()) {
            graphicsCaptureAvailable_ = true;
            return result;
        }
        // 保存错误信息
        graphicsCaptureError = getLastError();
        // 如果初始化成功但捕获失败，标记为不可用
        if (graphicsCaptureInitialized_) {
            graphicsCaptureAvailable_ = false;
        }
    } else {
        // 初始化失败，保存错误信息
        graphicsCaptureError = getLastError();
    }
    
    // 2. 回退到 DXGI(如果可用)
    if (dxgiAvailable_ || (!dxgiInitialized_ && initializeDXGI())) {
        auto result = captureDisplayDXGI(displayId);
        if (result.has_value()) {
            dxgiAvailable_ = true;
            return result;
        }
        // 保存错误信息
        dxgiError = getLastError();
        if (!dxgiAvailable_) {
            dxgiAvailable_ = false;
        }
    } else {
        // 初始化失败，保存错误信息
        dxgiError = getLastError();
    }
    
    // 3. 最后回退到 BitBlt
    // 设置综合错误信息
    if (!graphicsCaptureError.empty() && !dxgiError.empty()) {
        setLastError("GraphicsCapture: " + graphicsCaptureError + "; DXGI: " + dxgiError);
    } else if (!graphicsCaptureError.empty()) {
        setLastError("GraphicsCapture: " + graphicsCaptureError);
    } else if (!dxgiError.empty()) {
        setLastError("DXGI: " + dxgiError);
    }
    
    return captureFullScreenBitBlt(displayId);
}

std::optional<types::ImageData> ScreenCaptureWindows::captureWindow(types::WindowHandle handle) {
    if (!handle) {
        setLastError("Invalid window handle");
        return std::nullopt;
    }
    
    HWND hwnd = reinterpret_cast<HWND>(handle);
    if (!IsWindow(hwnd)) {
        setLastError("Invalid window handle");
        return std::nullopt;
    }
    
    return captureWindowBitBlt(hwnd);
}

std::optional<types::ImageData> ScreenCaptureWindows::captureRegion(
    const types::Rect& region, 
    int32_t displayId) {
    
    (void)displayId; // 标记参数已使用,避免警告
    
    if (!region.isValid()) {
        setLastError("Invalid region");
        return std::nullopt;
    }
    
    return captureRegionBitBlt(region);
}

std::vector<types::DisplayInfo> ScreenCaptureWindows::getDisplays() {
    return displays_;
}

// ========== DXGI实现 ==========

bool ScreenCaptureWindows::initializeDXGI() {
    if (dxgiInitialized_) {
        return true;
    }
    
    // 创建D3D11设备
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        featureLevels,
        static_cast<UINT>(std::size(featureLevels)),
        D3D11_SDK_VERSION,
        &d3dDevice_,
        nullptr,
        &d3dContext_
    );
    
    if (FAILED(hr)) {
        setLastError("Failed to create D3D11 device");
        return false;
    }
    
    // 获取DXGI输出
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> outputDuplication;
    Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    
    // 从 ID3D11Device 获取 IDXGIDevice，然后获取 IDXGIAdapter
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    hr = d3dDevice_.As(&dxgiDevice);
    if (FAILED(hr)) {
        setLastError("Failed to get IDXGIDevice from D3D11 device");
        return false;
    }
    
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) {
        setLastError("Failed to get DXGI adapter");
        return false;
    }
    
    // 获取主显示器(displayId = 0)
    hr = adapter->EnumOutputs(0, &output);
    if (FAILED(hr)) {
        setLastError("Failed to enumerate outputs");
        return false;
    }
    
    hr = output.As(&output1);
    if (FAILED(hr)) {
        setLastError("Failed to get IDXGIOutput1");
        return false;
    }
    
    hr = output1->DuplicateOutput(d3dDevice_.Get(), &outputDuplication);
    if (FAILED(hr)) {
        // 常见错误:
        // E_ACCESSDENIED: 输出已被其他程序占用(如远程桌面、某些屏幕录制软件)
        // DXGI_ERROR_UNSUPPORTED: 不支持桌面复制
        dxgiAvailable_ = false;
        if (hr == E_ACCESSDENIED) {
            setLastError("DXGI output already in use (may be captured by another application like Remote Desktop, OBS, etc.)");
        } else if (hr == DXGI_ERROR_UNSUPPORTED) {
            setLastError("DXGI desktop duplication not supported");
        } else {
            setLastError("Failed to duplicate output (HRESULT: 0x" + 
                        std::to_string(static_cast<unsigned int>(hr)) + ")");
        }
        return false;
    }
    
    dxgiAvailable_ = true;
    
    outputDuplication_ = outputDuplication;
    output1_ = output1;
    
    // 获取输出描述
    DXGI_OUTPUT_DESC outputDesc;
    output->GetDesc(&outputDesc);
    outputWidth_ = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    outputHeight_ = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
    
    // 创建staging texture用于CPU读取
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = outputWidth_;
    texDesc.Height = outputHeight_;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_STAGING;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    
    hr = d3dDevice_->CreateTexture2D(&texDesc, nullptr, &stagingTexture_);
    if (FAILED(hr)) {
        setLastError("Failed to create staging texture");
        return false;
    }
    
    dxgiInitialized_ = true;
    dxgiAvailable_ = true;
    currentDisplayId_ = 0;
    return true;
}

void ScreenCaptureWindows::cleanupDXGI() {
    stagingTexture_.Reset();
    outputDuplication_.Reset();
    output1_.Reset();
    d3dContext_.Reset();
    d3dDevice_.Reset();
    dxgiInitialized_ = false;
    // 注意:不重置dxgiAvailable_,因为如果被占用,重试也会失败
}

// ========== Windows.Graphics.Capture 实现 ==========

bool ScreenCaptureWindows::initializeGraphicsCapture() {
    if (graphicsCaptureInitialized_) {
        return true;
    }
    
    // 注意: 不进行版本检查，直接尝试初始化
    // IsWindows10OrGreater() 需要应用程序 manifest 才能正确工作
    // 如果 Windows.Graphics.Capture API 不可用，初始化会失败并返回错误
    
    // 初始化 Windows Runtime
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        setLastError("Failed to initialize Windows Runtime");
        graphicsCaptureAvailable_ = false;
        return false;
    }
    
    // 创建 D3D11 设备(如果还没有)
    if (!d3dDevice_) {
        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };
        
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,  // Graphics Capture 需要 BGRA 支持
            featureLevels,
            static_cast<UINT>(std::size(featureLevels)),
            D3D11_SDK_VERSION,
            &d3dDevice_,
            nullptr,
            &d3dContext_
        );
        
        if (FAILED(hr)) {
            setLastError("Failed to create D3D11 device for Graphics Capture");
            graphicsCaptureAvailable_ = false;
            return false;
        }
    }
    
    // 获取显示器句柄
    HMONITOR monitor = getMonitorHandle(0); // 主显示器
    if (!monitor) {
        setLastError("Failed to get monitor handle");
        graphicsCaptureAvailable_ = false;
        return false;
    }
    
    // 使用 interop 接口创建 GraphicsCaptureItem
    // 注意:需要 Windows SDK 10.0.17763.0 或更高版本
    Microsoft::WRL::ComPtr<IGraphicsCaptureItemInterop> interop;
    HSTRING className = nullptr;
    HSTRING_HEADER classNameHeader = {};
    hr = WindowsCreateStringReference(
        RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem,
        static_cast<UINT32>(wcslen(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem)),
        &classNameHeader,
        &className
    );
    if (FAILED(hr)) {
        setLastError("Failed to create HSTRING for GraphicsCaptureItem");
        graphicsCaptureAvailable_ = false;
        return false;
    }
    hr = RoGetActivationFactory(className, __uuidof(IGraphicsCaptureItemInterop), &interop);
    
    if (FAILED(hr) || !interop) {
        setLastError("Failed to get GraphicsCaptureItem interop (may require Windows 10 1803+ and Windows SDK 10.0.17763+)");
        graphicsCaptureAvailable_ = false;
        return false;
    }
    
    // 从 HMONITOR 创建 GraphicsCaptureItem
    void* captureItemPtr = nullptr;
    hr = interop->CreateForMonitor(monitor, __uuidof(ABI::Windows::Graphics::Capture::IGraphicsCaptureItem), &captureItemPtr);
    if (FAILED(hr) || !captureItemPtr) {
        setLastError("Failed to create GraphicsCaptureItem for monitor (HRESULT: 0x" + 
                    std::to_string(static_cast<unsigned int>(hr)) + ")");
        graphicsCaptureAvailable_ = false;
        return false;
    }
    
    captureItem_ = Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(
        reinterpret_cast<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem*>(captureItemPtr)
    );
    
    // 获取捕获项的大小
    ABI::Windows::Graphics::SizeInt32 size;
    hr = captureItem_->get_Size(&size);
    if (FAILED(hr)) {
        setLastError("Failed to get capture item size");
        graphicsCaptureAvailable_ = false;
        return false;
    }
    
    outputWidth_ = size.Width;
    outputHeight_ = size.Height;
    
    // 获取 Direct3DDevice(从 D3D11 设备创建)
    // 首先从 ID3D11Device 获取 IDXGIDevice
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    hr = d3dDevice_.As(&dxgiDevice);
    if (FAILED(hr)) {
        setLastError("Failed to get IDXGIDevice from D3D11 device");
        graphicsCaptureAvailable_ = false;
        return false;
    }
    
    // 创建 Direct3DDevice
    Microsoft::WRL::ComPtr<IInspectable> inspectableDevice;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), &inspectableDevice);
    if (FAILED(hr)) {
        setLastError("Failed to create Direct3DDevice from DXGI device");
        graphicsCaptureAvailable_ = false;
        return false;
    }
    
    // 转换为 IDirect3DDevice
    hr = inspectableDevice.As(&graphicsDevice_);
    if (FAILED(hr)) {
        setLastError("Failed to convert IInspectable to IDirect3DDevice");
        graphicsCaptureAvailable_ = false;
        return false;
    }
    
    // 创建 FramePool
    Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePoolStatics> framePoolStatics;
    HSTRING framePoolClassName = nullptr;
    HSTRING_HEADER framePoolClassNameHeader = {};
    hr = WindowsCreateStringReference(
        RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool,
        static_cast<UINT32>(wcslen(RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool)),
        &framePoolClassNameHeader,
        &framePoolClassName
    );
    if (FAILED(hr)) {
        setLastError("Failed to create HSTRING for Direct3D11CaptureFramePool");
        graphicsCaptureAvailable_ = false;
        return false;
    }
    hr = RoGetActivationFactory(framePoolClassName, 
                                __uuidof(ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePoolStatics),
                                &framePoolStatics);
    
    if (FAILED(hr)) {
        setLastError("Failed to get Direct3D11CaptureFramePool statics");
        graphicsCaptureAvailable_ = false;
        return false;
    }
    
    // 创建 FramePool(使用 BGRA8 格式,双缓冲)
    hr = framePoolStatics->Create(
        graphicsDevice_.Get(),
        ABI::Windows::Graphics::DirectX::DirectXPixelFormat_B8G8R8A8UIntNormalized,
        2,  // 双缓冲
        size,
        &framePool_
    );
    
    if (FAILED(hr)) {
        setLastError("Failed to create Direct3D11CaptureFramePool");
        graphicsCaptureAvailable_ = false;
        return false;
    }
    
    // 从 FramePool 创建 CaptureSession
    Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePool> framePoolInterface;
    hr = framePool_.As(&framePoolInterface);
    if (FAILED(hr)) {
        setLastError("Failed to get IDirect3D11CaptureFramePool interface");
        graphicsCaptureAvailable_ = false;
        return false;
    }
    
    hr = framePoolInterface->CreateCaptureSession(captureItem_.Get(), &captureSession_);
    if (FAILED(hr)) {
        setLastError("Failed to create capture session");
        graphicsCaptureAvailable_ = false;
        return false;
    }
    
    // 启动捕获
    hr = captureSession_->StartCapture();
    if (FAILED(hr)) {
        setLastError("Failed to start capture session");
        graphicsCaptureAvailable_ = false;
        return false;
    }
    
    graphicsCaptureInitialized_ = true;
    graphicsCaptureAvailable_ = true;
    return true;
}

void ScreenCaptureWindows::cleanupGraphicsCapture() {
    if (captureSession_) {
        // IGraphicsCaptureSession 继承自 IClosable,需要通过 IClosable 接口调用 Close
        Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IClosable> closable;
        if (SUCCEEDED(captureSession_.As(&closable)) && closable) {
            closable->Close();
        }
        captureSession_.Reset();
    }
    framePool_.Reset();
    captureItem_.Reset();
    graphicsDevice_.Reset();
    graphicsCaptureInitialized_ = false;
}

std::optional<types::ImageData> ScreenCaptureWindows::captureFullScreenGraphicsCapture(int32_t displayId) {
    if (!graphicsCaptureInitialized_ || displayId != 0) {
        // 需要重新初始化或切换显示器
        cleanupGraphicsCapture();
        if (!initializeGraphicsCapture()) {
            return std::nullopt;
        }
    }
    
    if (!framePool_ || !captureSession_) {
        return std::nullopt;
    }
    
    // 尝试获取帧(非阻塞)
    // 注意: 捕获会话启动后可能需要一些时间才能产生第一帧
    Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Capture::IDirect3D11CaptureFrame> frame;
    HRESULT hr = framePool_->TryGetNextFrame(&frame);
    
    // 如果第一次尝试失败，可能是还没有帧可用，尝试多次等待
    // Windows.Graphics.Capture 在启动后通常需要 100-200ms 才能产生第一帧
    const int maxRetries = 5;
    const int retryDelayMs = 50;
    
    for (int retry = 0; (FAILED(hr) || !frame) && retry < maxRetries; ++retry) {
        Sleep(retryDelayMs);
        hr = framePool_->TryGetNextFrame(&frame);
    }
    
    if (FAILED(hr) || !frame) {
        // 检查是否是特定的错误码
        if (hr == 0x8000000A) {  // E_PENDING - 表示还没有帧可用
            setLastError("GraphicsCapture: Frame not available yet (may need more time or frame pool not receiving frames)");
        } else {
            setLastError("GraphicsCapture: Failed to get capture frame (HRESULT: 0x" + 
                        std::to_string(static_cast<unsigned int>(hr)) + ")");
        }
        return std::nullopt;
    }
    
    // 获取帧的 Surface
    Microsoft::WRL::ComPtr<ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface> surface;
    hr = frame->get_Surface(surface.GetAddressOf());
    if (FAILED(hr) || !surface) {
        setLastError("Failed to get frame surface");
        return std::nullopt;
    }
    
    // 从 Surface 获取 D3D11 Texture2D
    // 使用 Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess
    Microsoft::WRL::ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> dxgiInterfaceAccess;
    hr = surface.As(&dxgiInterfaceAccess);
    if (FAILED(hr)) {
        setLastError("Failed to get DxgiInterfaceAccess");
        return std::nullopt;
    }
    
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    hr = dxgiInterfaceAccess->GetInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(texture.GetAddressOf()));
    if (FAILED(hr)) {
        setLastError("Failed to get D3D11 texture from surface");
        return std::nullopt;
    }
    
    // 转换为 ImageData
    return textureToImageData(texture.Get(), outputWidth_, outputHeight_);
}

bool ScreenCaptureWindows::isDXGIAvailable() const {
    return dxgiAvailable_;
}

std::string ScreenCaptureWindows::getCaptureMethod() const {
    if (graphicsCaptureAvailable_) {
        return "GraphicsCapture";
    } else if (dxgiAvailable_) {
        return "DXGI";
    } else {
        return "BitBlt";
    }
}

std::optional<types::ImageData> ScreenCaptureWindows::captureDisplayDXGI(int32_t displayId) {
    if (!dxgiInitialized_ || displayId != currentDisplayId_) {
        // 需要重新初始化
        cleanupDXGI();
        if (!initializeDXGI()) {
            return std::nullopt;
        }
    }
    
    if (!outputDuplication_) {
        return std::nullopt;
    }
    
    // 获取桌面帧
    Microsoft::WRL::ComPtr<IDXGIResource> desktopResource;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    
    HRESULT hr = outputDuplication_->AcquireNextFrame(0, &frameInfo, &desktopResource);
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            setLastError("Frame acquisition timeout");
        } else if (hr == DXGI_ERROR_ACCESS_LOST) {
            // 访问丢失,需要重新初始化
            cleanupDXGI();
            setLastError("DXGI access lost, reinitialization required");
        } else {
            setLastError("Failed to acquire next frame (HRESULT: 0x" + 
                        std::to_string(static_cast<unsigned int>(hr)) + ")");
        }
        return std::nullopt;
    }
    
    // 获取纹理
    Microsoft::WRL::ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);
    if (FAILED(hr)) {
        outputDuplication_->ReleaseFrame();
        setLastError("Failed to get desktop texture");
        return std::nullopt;
    }
    
    // 复制到staging texture
    d3dContext_->CopyResource(stagingTexture_.Get(), desktopTexture.Get());
    
    // 释放帧
    outputDuplication_->ReleaseFrame();
    
    // 转换为ImageData
    return textureToImageData(stagingTexture_.Get(), outputWidth_, outputHeight_);
}

std::optional<types::ImageData> ScreenCaptureWindows::textureToImageData(
    ID3D11Texture2D* texture,
    uint32_t width,
    uint32_t height) {
    
    if (!texture) {
        return std::nullopt;
    }
    
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = d3dContext_->Map(texture, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        setLastError("Failed to map texture");
        return std::nullopt;
    }
    
    // 创建ImageData(BGR格式,OpenCV兼容)
    types::ImageData imageData;
    imageData.allocate(width, height, types::ImageFormat::BGR);
    
    // 从BGRA转换为BGR(跳过Alpha通道)
    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
    uint8_t* dst = imageData.data.data();
    
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* srcRow = src + y * mapped.RowPitch;
        uint8_t* dstRow = dst + y * width * 3;
        
        for (uint32_t x = 0; x < width; ++x) {
            // BGRA -> BGR
            dstRow[x * 3 + 0] = srcRow[x * 4 + 0]; // B
            dstRow[x * 3 + 1] = srcRow[x * 4 + 1]; // G
            dstRow[x * 3 + 2] = srcRow[x * 4 + 2]; // R
        }
    }
    
    d3dContext_->Unmap(texture, 0);
    
    return imageData;
}

// ========== BitBlt实现(回退方案) ==========

std::optional<types::ImageData> ScreenCaptureWindows::captureFullScreenBitBlt(int32_t displayId) {
    HMONITOR monitor = getMonitorHandle(displayId);
    if (!monitor) {
        setLastError("Invalid display ID");
        return std::nullopt;
    }
    
    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(MONITORINFO);
    if (!GetMonitorInfo(monitor, &monitorInfo)) {
        setLastError("Failed to get monitor info");
        return std::nullopt;
    }
    
    types::Rect region;
    region.x = monitorInfo.rcMonitor.left;
    region.y = monitorInfo.rcMonitor.top;
    region.width = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
    region.height = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
    
    return captureRegionBitBlt(region);
}

std::optional<types::ImageData> ScreenCaptureWindows::captureWindowBitBlt(HWND hwnd) {
    // 检查窗口是否有效
    if (!IsWindow(hwnd)) {
        setLastError("Invalid window handle");
        return std::nullopt;
    }
    
    // 检查窗口是否可见
    if (!IsWindowVisible(hwnd)) {
        setLastError("Window is not visible");
        return std::nullopt;
    }
    
    // 检查窗口是否最小化
    if (IsIconic(hwnd)) {
        setLastError("Window is minimized");
        return std::nullopt;
    }
    
    // 获取窗口矩形
    RECT rect;
    if (!GetWindowRect(hwnd, &rect)) {
        setLastError("Failed to get window rect");
        return std::nullopt;
    }
    
    uint32_t width = rect.right - rect.left;
    uint32_t height = rect.bottom - rect.top;
    
    // 检查窗口大小是否有效
    if (width == 0 || height == 0 || width > 10000 || height > 10000) {
        setLastError("Invalid window size");
        return std::nullopt;
    }
    
    // 获取窗口DC
    HDC windowDC = GetWindowDC(hwnd);
    if (!windowDC) {
        setLastError("Failed to get window DC");
        return std::nullopt;
    }
    
    // 创建兼容DC和位图
    HDC memDC = CreateCompatibleDC(windowDC);
    if (!memDC) {
        ReleaseDC(hwnd, windowDC);
        setLastError("Failed to create compatible DC");
        return std::nullopt;
    }
    
    HBITMAP bitmap = CreateCompatibleBitmap(windowDC, width, height);
    if (!bitmap) {
        DeleteDC(memDC);
        ReleaseDC(hwnd, windowDC);
        setLastError("Failed to create bitmap");
        return std::nullopt;
    }
    
    // 选择位图到DC
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDC, bitmap));
    
    // 使用PrintWindow复制窗口内容(支持最小化窗口和子窗口)
    BOOL result = PrintWindow(hwnd, memDC, PW_CLIENTONLY);
    if (!result) {
        // PrintWindow失败,尝试使用BitBlt
        result = BitBlt(
            memDC, 0, 0, width, height,
            windowDC, 0, 0,
            SRCCOPY
        );
    }
    
    if (!result) {
        SelectObject(memDC, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memDC);
        ReleaseDC(hwnd, windowDC);
        setLastError("Failed to copy window content");
        return std::nullopt;
    }
    
    // 获取位图信息
    BITMAPINFO bmpInfo = {};
    bmpInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmpInfo.bmiHeader.biWidth = width;
    bmpInfo.bmiHeader.biHeight = -static_cast<LONG>(height); // 负值表示从上到下
    bmpInfo.bmiHeader.biPlanes = 1;
    bmpInfo.bmiHeader.biBitCount = 24; // RGB
    bmpInfo.bmiHeader.biCompression = BI_RGB;
    
    // 创建ImageData
    types::ImageData imageData;
    imageData.allocate(width, height, types::ImageFormat::BGR);
    
    // 从位图读取数据
    HDC screenDC = GetDC(nullptr);
    int scanLines = GetDIBits(
        screenDC, bitmap, 0, height,
        imageData.data.data(),
        &bmpInfo,
        DIB_RGB_COLORS
    );
    ReleaseDC(nullptr, screenDC);
    
    // 清理资源
    SelectObject(memDC, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDC);
    ReleaseDC(hwnd, windowDC);
    
    if (scanLines != static_cast<int>(height)) {
        setLastError("GetDIBits failed");
        return std::nullopt;
    }
    
    return imageData;
}

std::optional<types::ImageData> ScreenCaptureWindows::captureRegionBitBlt(const types::Rect& region) {
    // 获取屏幕DC
    HDC screenDC = GetDC(nullptr);
    if (!screenDC) {
        setLastError("Failed to get screen DC");
        return std::nullopt;
    }
    
    // 创建兼容DC和位图
    HDC memDC = CreateCompatibleDC(screenDC);
    if (!memDC) {
        ReleaseDC(nullptr, screenDC);
        setLastError("Failed to create compatible DC");
        return std::nullopt;
    }
    
    HBITMAP bitmap = CreateCompatibleBitmap(screenDC, region.width, region.height);
    if (!bitmap) {
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        setLastError("Failed to create bitmap");
        return std::nullopt;
    }
    
    // 选择位图到DC
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDC, bitmap));
    
    // 复制屏幕区域到位图
    BOOL result = BitBlt(
        memDC, 0, 0, region.width, region.height,
        screenDC, region.x, region.y,
        SRCCOPY
    );
    
    if (!result) {
        SelectObject(memDC, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        setLastError("BitBlt failed");
        return std::nullopt;
    }
    
    // 获取位图信息
    BITMAPINFO bmpInfo = {};
    bmpInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmpInfo.bmiHeader.biWidth = region.width;
    bmpInfo.bmiHeader.biHeight = -static_cast<LONG>(region.height); // 负值表示从上到下
    bmpInfo.bmiHeader.biPlanes = 1;
    bmpInfo.bmiHeader.biBitCount = 24; // RGB
    bmpInfo.bmiHeader.biCompression = BI_RGB;
    
    // 创建ImageData
    types::ImageData imageData;
    imageData.allocate(region.width, region.height, types::ImageFormat::BGR);
    
    // 从位图读取数据
    int scanLines = GetDIBits(
        screenDC, bitmap, 0, region.height,
        imageData.data.data(),
        &bmpInfo,
        DIB_RGB_COLORS
    );
    
    // 清理资源
    SelectObject(memDC, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
    
    if (scanLines != static_cast<int>(region.height)) {
        setLastError("GetDIBits failed");
        return std::nullopt;
    }
    
    return imageData;
}

// ========== 显示器枚举 ==========

HMONITOR ScreenCaptureWindows::getMonitorHandle(int32_t displayId) {
    if (displayId < 0 || static_cast<size_t>(displayId) >= monitorHandles_.size()) {
        return nullptr;
    }
    return monitorHandles_[displayId];
}

void ScreenCaptureWindows::enumerateDisplays() {
    displays_.clear();
    monitorHandles_.clear();
    
    struct EnumData {
        std::vector<types::DisplayInfo>* displays;
        std::vector<HMONITOR>* monitors;
    } data = {&displays_, &monitorHandles_};
    
    auto enumProc = [](HMONITOR hMonitor, HDC /*hdc*/, LPRECT /*lprcMonitor*/, LPARAM dwData) -> BOOL {
        EnumData* data = reinterpret_cast<EnumData*>(dwData);
        
        MONITORINFOEX monitorInfo = {};
        monitorInfo.cbSize = sizeof(MONITORINFOEX);
        if (!GetMonitorInfo(hMonitor, &monitorInfo)) {
            return TRUE; // 继续枚举
        }
        
        types::DisplayInfo display;
        display.id = static_cast<uint32_t>(data->monitors->size());
        display.name = monitorInfo.szDevice;
        display.bounds.x = monitorInfo.rcMonitor.left;
        display.bounds.y = monitorInfo.rcMonitor.top;
        display.bounds.width = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
        display.bounds.height = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
        display.isPrimary = (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) != 0;
        display.refreshRate = 60; // 默认值,可以通过其他API获取
        
        data->displays->push_back(display);
        data->monitors->push_back(hMonitor);
        
        return TRUE; // 继续枚举
    };
    
    EnumDisplayMonitors(nullptr, nullptr, enumProc, reinterpret_cast<LPARAM>(&data));
    
    // 如果没有找到显示器,至少添加主显示器
    if (displays_.empty()) {
        types::DisplayInfo primary;
        primary.id = 0;
        primary.name = "Primary Display";
        primary.bounds.width = GetSystemMetrics(SM_CXSCREEN);
        primary.bounds.height = GetSystemMetrics(SM_CYSCREEN);
        primary.isPrimary = true;
        primary.refreshRate = 60;
        displays_.push_back(primary);
        monitorHandles_.push_back(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY));
    }
}

std::vector<std::string> ScreenCaptureWindows::detectDXGIOccupyingProcesses() const {
    std::vector<std::string> occupyingProcesses;
    
    // 常见的可能占用DXGI的程序列表
    const std::vector<std::string> knownProcesses = {
        "obs64.exe", "obs32.exe", "obs.exe",           // OBS Studio
        "xsplit.core.exe", "xsplit.broadcaster.exe",   // XSplit
        "teamviewer.exe",                              // TeamViewer
        "anydesk.exe",                                 // AnyDesk
        "mstsc.exe",                                   // Windows远程桌面客户端
        "rdpclip.exe",                                 // 远程桌面剪贴板
        "msrdc.exe",                                   // 远程桌面连接
        "nvidia_shadowplay_helper.exe",                // NVIDIA ShadowPlay
        "nvspcaps64.exe", "nvspcaps32.exe",            // NVIDIA ShadowPlay
        "fraps.exe",                                   // FRAPS
        "bandicam.exe",                                // Bandicam
        "dxtory.exe",                                  // Dxtory
        "mirillisaction.exe",                          // Action!
        "plays.tv.exe",                                // Plays.tv
        "overwolf.exe",                                // Overwolf
        "discord.exe",                                 // Discord (可能使用屏幕共享)
        "zoom.exe",                                    // Zoom
        "teams.exe",                                   // Microsoft Teams
        "skype.exe",                                   // Skype
        "screenpresso.exe",                            // Screenpresso
        "greenshot.exe",                               // Greenshot (某些版本)
        "sharex.exe",                                  // ShareX
        "snippingtool.exe",                            // Windows截图工具
        "snip.exe"                                     // Windows截图工具
    };
    
    // 创建进程快照
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return occupyingProcesses;
    }
    
    PROCESSENTRY32 processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32);
    
    // 枚举所有进程
    if (Process32First(snapshot, &processEntry)) {
        do {
            std::string processName = processEntry.szExeFile;
            
            // 转换为小写进行比较
            std::transform(processName.begin(), processName.end(), processName.begin(),
                          [](unsigned char c) { return std::tolower(c); });
            
            // 检查是否是已知的可能占用DXGI的程序
            for (const auto& knownProcess : knownProcesses) {
                std::string lowerKnown = knownProcess;
                std::transform(lowerKnown.begin(), lowerKnown.end(), lowerKnown.begin(),
                              [](unsigned char c) { return std::tolower(c); });
                
                if (processName == lowerKnown) {
                    // 获取进程完整路径(可选,需要额外权限)
                    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processEntry.th32ProcessID);
                    if (hProcess != nullptr) {
                        char processPath[MAX_PATH];
                        if (GetModuleFileNameExA(hProcess, nullptr, processPath, MAX_PATH) > 0) {
                            occupyingProcesses.push_back(std::string(processPath) + " (PID: " + 
                                                         std::to_string(processEntry.th32ProcessID) + ")");
                        } else {
                            occupyingProcesses.push_back(processName + " (PID: " + 
                                                         std::to_string(processEntry.th32ProcessID) + ")");
                        }
                        CloseHandle(hProcess);
                    } else {
                        // 没有权限获取完整路径,只显示进程名
                        occupyingProcesses.push_back(processName + " (PID: " + 
                                                     std::to_string(processEntry.th32ProcessID) + ")");
                    }
                    break;
                }
            }
        } while (Process32Next(snapshot, &processEntry));
    }
    
    CloseHandle(snapshot);
    return occupyingProcesses;
}

} // namespace naw::desktop_pet::service::platform

#endif // _WIN32