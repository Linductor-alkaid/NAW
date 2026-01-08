#include "naw/desktop_pet/service/ScreenCapture.h"
#include "naw/desktop_pet/service/ImageProcessor.h"

#ifdef _WIN32
#include <windows.h>
#include "naw/desktop_pet/service/platform/ScreenCaptureWindows.h"
#endif

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using naw::desktop_pet::service::ScreenCapture;
using naw::desktop_pet::service::CaptureOptions;
using naw::desktop_pet::service::ImageProcessor;
using naw::desktop_pet::service::types::ImageData;
using naw::desktop_pet::service::types::ImageFormat;
using naw::desktop_pet::service::types::DisplayInfo;
using naw::desktop_pet::service::types::Rect;

// 辅助函数声明
void writeUint32LE(std::ofstream& file, uint32_t value);
void writeUint16LE(std::ofstream& file, uint16_t value);
void writeInt32LE(std::ofstream& file, int32_t value);

// 简单的BMP文件保存函数（仅支持24位BGR格式）
bool saveBMP(const std::string& filepath, const ImageData& image) {
    if (image.format != ImageFormat::BGR) {
        std::cerr << "Error: Only BGR format is supported for BMP saving" << std::endl;
        return false;
    }
    
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Failed to open file for writing: " << filepath << std::endl;
        return false;
    }
    
    // BMP文件头
    const uint32_t width = image.width;
    const uint32_t height = image.height;
    const uint32_t rowSize = ((width * 3 + 3) / 4) * 4; // 4字节对齐
    const uint32_t imageSize = rowSize * height;
    const uint32_t fileSize = 54 + imageSize; // 54字节文件头 + 图像数据
    
    // BMP文件头（14字节）
    file.write("BM", 2); // 签名
    writeUint32LE(file, fileSize); // 文件大小
    writeUint32LE(file, 0); // 保留字段
    writeUint32LE(file, 54); // 数据偏移量
    
    // BMP信息头（40字节）
    writeUint32LE(file, 40); // 信息头大小
    writeUint32LE(file, width); // 宽度
    writeInt32LE(file, static_cast<int32_t>(height)); // 高度（正数表示从上到下）
    writeUint16LE(file, 1); // 颜色平面数
    writeUint16LE(file, 24); // 每像素位数
    writeUint32LE(file, 0); // 压缩方式（无压缩）
    writeUint32LE(file, imageSize); // 图像大小
    writeUint32LE(file, 0); // 水平分辨率
    writeUint32LE(file, 0); // 垂直分辨率
    writeUint32LE(file, 0); // 调色板颜色数
    writeUint32LE(file, 0); // 重要颜色数
    
    // 写入图像数据（BMP是从下到上存储的）
    const uint8_t* data = image.data.data();
    for (int32_t y = static_cast<int32_t>(height) - 1; y >= 0; --y) {
        const uint8_t* row = data + y * width * 3;
        file.write(reinterpret_cast<const char*>(row), width * 3);
        
        // 写入填充字节（对齐到4字节）
        uint32_t padding = rowSize - width * 3;
        if (padding > 0) {
            std::vector<uint8_t> pad(padding, 0);
            file.write(reinterpret_cast<const char*>(pad.data()), padding);
        }
    }
    
    return file.good();
}

// 辅助函数：写入小端序整数
void writeUint32LE(std::ofstream& file, uint32_t value) {
    file.put(static_cast<char>(value & 0xFF));
    file.put(static_cast<char>((value >> 8) & 0xFF));
    file.put(static_cast<char>((value >> 16) & 0xFF));
    file.put(static_cast<char>((value >> 24) & 0xFF));
}

void writeUint16LE(std::ofstream& file, uint16_t value) {
    file.put(static_cast<char>(value & 0xFF));
    file.put(static_cast<char>((value >> 8) & 0xFF));
}

void writeInt32LE(std::ofstream& file, int32_t value) {
    writeUint32LE(file, static_cast<uint32_t>(value));
}

// 测试显示器枚举
void testDisplayEnumeration(ScreenCapture* capture) {
    std::cout << "\n=== Test Display Enumeration ===" << std::endl;
    
    auto displays = capture->getDisplays();
    std::cout << "Found " << displays.size() << " display(s):" << std::endl;
    
    for (size_t i = 0; i < displays.size(); ++i) {
        const auto& display = displays[i];
        std::cout << "  Display " << display.id << ":" << std::endl;
        std::cout << "    Name: " << display.name << std::endl;
        std::cout << "    Resolution: " << display.bounds.width << "x" << display.bounds.height << std::endl;
        std::cout << "    Position: (" << display.bounds.x << ", " << display.bounds.y << ")" << std::endl;
        std::cout << "    Primary: " << (display.isPrimary ? "Yes" : "No") << std::endl;
        std::cout << "    Refresh Rate: " << display.refreshRate << " Hz" << std::endl;
    }
}

// 测试全屏截图
void testFullScreenCapture(ScreenCapture* capture) {
    std::cout << "\n=== Test Full Screen Capture ===" << std::endl;
    
#ifdef _WIN32
    auto* windowsCapture = dynamic_cast<naw::desktop_pet::service::platform::ScreenCaptureWindows*>(capture);
    if (windowsCapture) {
        std::cout << "Before capture - Method: " << windowsCapture->getCaptureMethod() << std::endl;
        std::cout << "  GraphicsCapture available: " << (windowsCapture->getCaptureMethod() == "GraphicsCapture" ? "Yes" : "No") << std::endl;
        std::cout << "  DXGI available: " << (windowsCapture->isDXGIAvailable() ? "Yes" : "No") << std::endl;
    }
#endif
    
    auto image = capture->captureFullScreen(0);
    if (!image.has_value()) {
        std::cerr << "Error: Full screen capture failed - " << capture->getLastError() << std::endl;
        return;
    }
    
    const auto& img = image.value();
    std::cout << "Capture successful!" << std::endl;
#ifdef _WIN32
    if (windowsCapture) {
        std::string method = windowsCapture->getCaptureMethod();
        std::cout << "  Method: " << method;
        
        if (method == "DXGI") {
            std::cout << " (DXGI Desktop Duplication - Hardware accelerated, optimal performance)";
        } else if (method == "GraphicsCapture") {
            std::cout << " (Windows.Graphics.Capture API - Fallback, supports multiple concurrent captures)";
            // 如果使用了 GraphicsCapture，说明 DXGI 可能被占用
            std::string error = capture->getLastError();
            if (!error.empty() && error.find("DXGI failed") != std::string::npos) {
                std::cout << std::endl << "  Note: DXGI was unavailable - " << error;
            }
        } else {
            std::cout << " (BitBlt - Software fallback)";
            // 如果使用了 BitBlt，显示可能的错误信息
            std::string error = capture->getLastError();
            if (!error.empty() && error != "Success") {
                std::cout << std::endl << "  Note: DXGI/GraphicsCapture failed - " << error;
            }
        }
        std::cout << std::endl;
    }
#endif
    std::cout << "  Size: " << img.width << "x" << img.height << std::endl;
    std::cout << "  Format: BGR" << std::endl;
    std::cout << "  Data size: " << img.data.size() << " bytes" << std::endl;
    
    // 保存到文件
    std::string filename = "test_fullscreen.bmp";
    if (saveBMP(filename, img)) {
        std::cout << "  Saved to: " << filename << std::endl;
    } else {
        std::cerr << "  Error: Failed to save file" << std::endl;
    }
}

// 测试区域截图
void testRegionCapture(ScreenCapture* capture) {
    std::cout << "\n=== Test Region Capture ===" << std::endl;
    
    // 获取主显示器信息
    auto displays = capture->getDisplays();
    if (displays.empty()) {
        std::cerr << "Error: Failed to get display information" << std::endl;
        return;
    }
    
    const auto& primaryDisplay = displays[0];
    
    // 截取屏幕中心区域（800x600）
    Rect region;
    region.x = primaryDisplay.bounds.x + (primaryDisplay.bounds.width - 800) / 2;
    region.y = primaryDisplay.bounds.y + (primaryDisplay.bounds.height - 600) / 2;
    region.width = 800;
    region.height = 600;
    
    std::cout << "Capture region: (" << region.x << ", " << region.y << ") "
              << region.width << "x" << region.height << std::endl;
    
    auto image = capture->captureRegion(region);
    if (!image.has_value()) {
        std::cerr << "Error: Region capture failed - " << capture->getLastError() << std::endl;
        return;
    }
    
    const auto& img = image.value();
    std::cout << "Capture successful!" << std::endl;
    std::cout << "  Size: " << img.width << "x" << img.height << std::endl;
    std::cout << "  Format: BGR" << std::endl;
    std::cout << "  Data size: " << img.data.size() << " bytes" << std::endl;
    
    // 保存到文件
    std::string filename = "test_region.bmp";
    if (saveBMP(filename, img)) {
        std::cout << "  Saved to: " << filename << std::endl;
    } else {
        std::cerr << "  Error: Failed to save file" << std::endl;
    }
}

// 测试窗口截图（需要窗口句柄）
void testWindowCapture(ScreenCapture* capture) {
    std::cout << "\n=== Test Window Capture ===" << std::endl;
    
    if (!capture->supportsWindowCapture()) {
        std::cout << "Window capture not supported" << std::endl;
        return;
    }
    
#ifdef _WIN32
    // 尝试获取当前控制台窗口
    HWND consoleWindow = GetConsoleWindow();
    if (consoleWindow == nullptr) {
        std::cout << "Failed to get console window handle" << std::endl;
        std::cout << "Trying to find a visible window instead..." << std::endl;
        
        // 尝试查找桌面窗口或其他可见窗口
        HWND desktopWindow = GetDesktopWindow();
        if (desktopWindow != nullptr) {
            consoleWindow = desktopWindow;
        } else {
            std::cout << "No suitable window found for testing" << std::endl;
            return;
        }
    }
    
    // 检查窗口状态
    if (!IsWindowVisible(consoleWindow)) {
        std::cout << "Window is not visible, skipping test" << std::endl;
        return;
    }
    
    RECT windowRect;
    if (GetWindowRect(consoleWindow, &windowRect)) {
        std::cout << "Window size: " << (windowRect.right - windowRect.left) 
                  << "x" << (windowRect.bottom - windowRect.top) << std::endl;
    }
    
    std::cout << "Attempting to capture window..." << std::endl;
    auto image = capture->captureWindow(reinterpret_cast<void*>(consoleWindow));
    if (!image.has_value()) {
        std::cerr << "Error: Window capture failed - " << capture->getLastError() << std::endl;
        return;
    }
    
    const auto& img = image.value();
    
    // 检查图像是否有效
    if (img.width == 0 || img.height == 0 || img.data.empty()) {
        std::cerr << "Error: Captured image is empty" << std::endl;
        return;
    }
    
    std::cout << "Capture successful!" << std::endl;
    std::cout << "  Size: " << img.width << "x" << img.height << std::endl;
    std::cout << "  Format: BGR" << std::endl;
    std::cout << "  Data size: " << img.data.size() << " bytes" << std::endl;
    
    // 保存到文件
    std::string filename = "test_window.bmp";
    if (saveBMP(filename, img)) {
        std::cout << "  Saved to: " << filename << std::endl;
    } else {
        std::cerr << "  Error: Failed to save file" << std::endl;
    }
#else
    std::cout << "Windows platform specific feature" << std::endl;
#endif
}

// 测试分辨率控制
void testResolutionControl(ScreenCapture* capture) {
    std::cout << "\n=== Test Resolution Control ===" << std::endl;
    
    // 测试最大分辨率限制
    {
        CaptureOptions options;
        options.maxWidth = 1280;
        options.maxHeight = 720;
        options.keepAspectRatio = true;
        
        std::cout << "Testing max resolution limit (1280x720)..." << std::endl;
        auto image = capture->captureFullScreen(0, options);
        if (image.has_value()) {
            const auto& img = image.value();
            std::cout << "  Original capture size: " << img.width << "x" << img.height << std::endl;
            assert(img.width <= options.maxWidth.value());
            assert(img.height <= options.maxHeight.value());
            std::cout << "  Resolution control applied successfully!" << std::endl;
        } else {
            std::cerr << "  Error: Capture failed - " << capture->getLastError() << std::endl;
        }
    }
    
    // 测试目标分辨率
    {
        CaptureOptions options;
        options.targetWidth = 640;
        options.targetHeight = 480;
        options.keepAspectRatio = true;
        
        std::cout << "Testing target resolution (640x480, keep aspect ratio)..." << std::endl;
        auto image = capture->captureFullScreen(0, options);
        if (image.has_value()) {
            const auto& img = image.value();
            std::cout << "  Result size: " << img.width << "x" << img.height << std::endl;
            // 由于保持宽高比，实际尺寸可能不完全匹配
            std::cout << "  Aspect ratio preserved: " << (img.width * 480 == img.height * 640 ? "Yes" : "No") << std::endl;
        } else {
            std::cerr << "  Error: Capture failed - " << capture->getLastError() << std::endl;
        }
    }
    
    // 测试自适应分辨率
    {
        CaptureOptions options;
        options.adaptiveResolution = true;
        options.layerType = 0; // CV实时处理层
        
        std::cout << "Testing adaptive resolution (Layer 0 - CV real-time)..." << std::endl;
        auto image = capture->captureFullScreen(0, options);
        if (image.has_value()) {
            const auto& img = image.value();
            std::cout << "  Adaptive size: " << img.width << "x" << img.height << std::endl;
            // Layer 0 应该降低到 640x480 或更小
            assert(img.width <= 640);
            assert(img.height <= 480);
            std::cout << "  Adaptive resolution applied successfully!" << std::endl;
        } else {
            std::cerr << "  Error: Capture failed - " << capture->getLastError() << std::endl;
        }
    }
}

// 测试图像压缩
void testImageCompression(ScreenCapture* capture) {
    std::cout << "\n=== Test Image Compression ===" << std::endl;
    
    // 先捕获一张图像
    auto originalImage = capture->captureFullScreen(0);
    if (!originalImage.has_value()) {
        std::cerr << "Error: Failed to capture image for compression test - " << capture->getLastError() << std::endl;
        return;
    }
    
    const auto& img = originalImage.value();
    std::cout << "Original image size: " << img.width << "x" << img.height << std::endl;
    std::cout << "Original data size: " << img.data.size() << " bytes" << std::endl;
    
    // 测试 JPEG 压缩
    {
        std::cout << "Testing JPEG compression (quality: 85)..." << std::endl;
        auto compressed = ImageProcessor::compressToJPEG(img, 85);
        if (compressed.has_value()) {
            std::cout << "  Compressed size: " << compressed->size() << " bytes" << std::endl;
            double ratio = (1.0 - static_cast<double>(compressed->size()) / img.data.size()) * 100.0;
            std::cout << "  Compression ratio: " << ratio << "%" << std::endl;
            
            // 保存压缩后的 JPEG
            std::string filename = "test_compressed.jpg";
            std::ofstream file(filename, std::ios::binary);
            if (file.is_open()) {
                file.write(reinterpret_cast<const char*>(compressed->data()), compressed->size());
                file.close();
                std::cout << "  Saved to: " << filename << std::endl;
            }
        } else {
            std::cerr << "  Error: JPEG compression failed" << std::endl;
        }
    }
    
    // 测试 PNG 压缩
    {
        std::cout << "Testing PNG compression (level: 3)..." << std::endl;
        auto compressed = ImageProcessor::compressToPNG(img, 3);
        if (compressed.has_value()) {
            std::cout << "  Compressed size: " << compressed->size() << " bytes" << std::endl;
            double ratio = (1.0 - static_cast<double>(compressed->size()) / img.data.size()) * 100.0;
            std::cout << "  Compression ratio: " << ratio << "%" << std::endl;
            
            // 保存压缩后的 PNG
            std::string filename = "test_compressed.png";
            std::ofstream file(filename, std::ios::binary);
            if (file.is_open()) {
                file.write(reinterpret_cast<const char*>(compressed->data()), compressed->size());
                file.close();
                std::cout << "  Saved to: " << filename << std::endl;
            }
        } else {
            std::cerr << "  Error: PNG compression failed" << std::endl;
        }
    }
    
    // 测试不同质量级别的 JPEG
    {
        std::cout << "Testing different JPEG quality levels..." << std::endl;
        for (int quality : {95, 85, 75, 50, 25}) {
            auto compressed = ImageProcessor::compressToJPEG(img, quality);
            if (compressed.has_value()) {
                std::cout << "  Quality " << quality << ": " << compressed->size() << " bytes" << std::endl;
            }
        }
    }
}

// 性能测试
void testPerformance(ScreenCapture* capture) {
    std::cout << "\n=== Performance Test ===" << std::endl;
    
    const int testCount = 10;
    std::cout << "Executing " << testCount << " full screen captures..." << std::endl;
    
#ifdef _WIN32
    auto* windowsCapture = dynamic_cast<naw::desktop_pet::service::platform::ScreenCaptureWindows*>(capture);
    if (windowsCapture) {
        std::cout << "Initial capture method: " << windowsCapture->getCaptureMethod() << std::endl;
    }
#endif
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < testCount; ++i) {
        auto image = capture->captureFullScreen(0);
        if (!image.has_value()) {
            std::cerr << "Error: Capture " << (i + 1) << " failed - " << capture->getLastError() << std::endl;
            break;
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    double avgTime = duration.count() / static_cast<double>(testCount);
    double fps = 1000.0 / avgTime;
    
    std::cout << "Total time: " << duration.count() << " ms" << std::endl;
    std::cout << "Average time: " << avgTime << " ms/capture" << std::endl;
    std::cout << "Average FPS: " << fps << std::endl;
    
#ifdef _WIN32
    if (windowsCapture) {
        std::string finalMethod = windowsCapture->getCaptureMethod();
        std::cout << "Final capture method: " << finalMethod << std::endl;
        if (finalMethod == "DXGI") {
            std::cout << "  Using DXGI Desktop Duplication - optimal performance achieved!" << std::endl;
        } else if (finalMethod == "GraphicsCapture") {
            std::cout << "  Using Windows.Graphics.Capture API - good performance (DXGI unavailable)" << std::endl;
        }
    }
#endif
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "ScreenCapture Test Program (Windows)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 检查平台支持
    if (!ScreenCapture::isSupported()) {
        std::cerr << "Error: Screen capture not supported on this platform" << std::endl;
        return 1;
    }
    
    // 创建ScreenCapture实例
    auto capture = ScreenCapture::create();
    if (!capture) {
        std::cerr << "Error: Failed to create ScreenCapture instance" << std::endl;
        return 1;
    }
    
    std::cout << "ScreenCapture instance created successfully" << std::endl;
    std::cout << "  Window capture support: " << (capture->supportsWindowCapture() ? "Yes" : "No") << std::endl;
    std::cout << "  Region capture support: " << (capture->supportsRegionCapture() ? "Yes" : "No") << std::endl;
    
#ifdef _WIN32
    // 检查捕获方法和状态
    auto* windowsCapture = dynamic_cast<naw::desktop_pet::service::platform::ScreenCaptureWindows*>(capture.get());
    if (windowsCapture) {
        std::string method = windowsCapture->getCaptureMethod();
        std::cout << "  Capture method: " << method << std::endl;
        
        if (method == "DXGI") {
            std::cout << "  Status: Using DXGI Desktop Duplication API (optimal performance)" << std::endl;
            std::cout << "  Benefits:" << std::endl;
            std::cout << "    - Hardware accelerated" << std::endl;
            std::cout << "    - Best performance" << std::endl;
            std::cout << "  Note: DXGI requires exclusive access" << std::endl;
        } else if (method == "GraphicsCapture") {
            std::cout << "  Status: Using Windows.Graphics.Capture API (fallback)" << std::endl;
            std::cout << "  Benefits:" << std::endl;
            std::cout << "    - Good performance and quality" << std::endl;
            std::cout << "    - Supports multiple concurrent captures" << std::endl;
            std::cout << "    - No exclusive access required" << std::endl;
            std::cout << "  Note: DXGI was unavailable or in use by another application" << std::endl;
        } else {
            std::cout << "  Status: Using BitBlt (software fallback)" << std::endl;
            std::cout << "  Note: This method is slower but always available" << std::endl;
        }
        
        std::cout << "  DXGI available: " << (windowsCapture->isDXGIAvailable() ? "Yes" : "No") << std::endl;
        
        if (method != "DXGI" && !windowsCapture->isDXGIAvailable()) {
            std::cout << "  Note: DXGI may be in use by another application" << std::endl;
            std::cout << "        Falling back to Windows.Graphics.Capture or BitBlt" << std::endl;
            
            // 检测可能占用DXGI的程序
            auto occupyingProcesses = windowsCapture->detectDXGIOccupyingProcesses();
            if (!occupyingProcesses.empty()) {
                std::cout << "  Detected potentially occupying processes:" << std::endl;
                for (const auto& process : occupyingProcesses) {
                    std::cout << "    - " << process << std::endl;
                }
                std::cout << "  Note: These processes may be using DXGI Desktop Duplication API" << std::endl;
                std::cout << "        Windows.Graphics.Capture can work alongside them" << std::endl;
            } else {
                std::cout << "  No known screen capture/remote desktop processes detected" << std::endl;
                std::cout << "  DXGI may be unavailable for other reasons:" << std::endl;
                std::cout << "    - Running in a virtual machine" << std::endl;
                std::cout << "    - Using Remote Desktop connection" << std::endl;
                std::cout << "    - Graphics driver issues" << std::endl;
                std::cout << "    - System policy restrictions" << std::endl;
            }
        }
    }
#endif
    
    try {
        // 运行测试
        testDisplayEnumeration(capture.get());
        testFullScreenCapture(capture.get());
        testRegionCapture(capture.get());
        testWindowCapture(capture.get());
        testResolutionControl(capture.get());
        testImageCompression(capture.get());
        testPerformance(capture.get());
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "All tests completed!" << std::endl;
        std::cout << "========================================" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
