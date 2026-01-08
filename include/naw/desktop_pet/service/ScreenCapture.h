#pragma once

#include "naw/desktop_pet/service/types/ImageData.h"

#include <memory>
#include <optional>
#include <vector>

namespace naw::desktop_pet::service {

/**
 * @brief 截图选项配置
 */
struct CaptureOptions {
    // 分辨率控制
    std::optional<uint32_t> maxWidth;          // 最大宽度限制
    std::optional<uint32_t> maxHeight;         // 最大高度限制
    std::optional<uint32_t> targetWidth;       // 目标宽度
    std::optional<uint32_t> targetHeight;      // 目标高度
    bool keepAspectRatio{true};                // 是否保持宽高比
    bool adaptiveResolution{false};             // 是否启用自适应分辨率
    
    // 图像压缩
    std::optional<int> jpegQuality;            // JPEG 压缩质量（0-100），如果设置则压缩为 JPEG
    std::optional<int> pngCompression;         // PNG 压缩级别（0-9），如果设置则压缩为 PNG
    
    // 处理层类型（用于自适应分辨率计算）
    int layerType{0};                          // 处理层类型（0=CV实时, 1=YOLO, 2=复杂CV, 3=VLM）
};

/**
 * @brief 屏幕采集接口
 * 
 * 提供跨平台的屏幕截图功能，支持全屏、窗口和区域截图
 */
class ScreenCapture {
public:
    virtual ~ScreenCapture() = default;
    
    /**
     * @brief 全屏截图
     * @param displayId 显示器ID（0表示主显示器，-1表示所有显示器合并）
     * @param options 截图选项（可选，默认不进行后处理）
     * @return 图像数据，失败返回std::nullopt
     */
    virtual std::optional<types::ImageData> captureFullScreen(
        int32_t displayId = 0,
        const CaptureOptions& options = {}
    ) = 0;
    
    /**
     * @brief 指定窗口截图
     * @param handle 窗口句柄（平台特定）
     * @param options 截图选项（可选，默认不进行后处理）
     * @return 图像数据，失败返回std::nullopt
     */
    virtual std::optional<types::ImageData> captureWindow(
        types::WindowHandle handle,
        const CaptureOptions& options = {}
    ) = 0;
    
    /**
     * @brief 指定区域截图（ROI）
     * @param region 截图区域（相对于主显示器的坐标）
     * @param displayId 显示器ID（0表示主显示器）
     * @param options 截图选项（可选，默认不进行后处理）
     * @return 图像数据，失败返回std::nullopt
     */
    virtual std::optional<types::ImageData> captureRegion(
        const types::Rect& region, 
        int32_t displayId = 0,
        const CaptureOptions& options = {}
    ) = 0;
    
    /**
     * @brief 获取显示器列表
     * @return 显示器信息列表
     */
    virtual std::vector<types::DisplayInfo> getDisplays() = 0;
    
    /**
     * @brief 检查是否支持窗口截图
     * @return true表示支持
     */
    virtual bool supportsWindowCapture() const = 0;
    
    /**
     * @brief 检查是否支持区域截图
     * @return true表示支持
     */
    virtual bool supportsRegionCapture() const = 0;
    
    /**
     * @brief 获取最后错误信息
     * @return 错误信息字符串
     */
    virtual std::string getLastError() const = 0;
    
    /**
     * @brief 工厂方法：创建平台特定的ScreenCapture实例
     * @return ScreenCapture实例，失败返回nullptr
     */
    static std::unique_ptr<ScreenCapture> create();
    
    /**
     * @brief 检查当前平台是否支持屏幕采集
     * @return true表示支持
     */
    static bool isSupported();
};

} // namespace naw::desktop_pet::service
