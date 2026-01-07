#pragma once

#include "naw/desktop_pet/service/types/ImageData.h"

#include <memory>
#include <optional>
#include <vector>

namespace naw::desktop_pet::service {

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
     * @return 图像数据，失败返回std::nullopt
     */
    virtual std::optional<types::ImageData> captureFullScreen(int32_t displayId = 0) = 0;
    
    /**
     * @brief 指定窗口截图
     * @param handle 窗口句柄（平台特定）
     * @return 图像数据，失败返回std::nullopt
     */
    virtual std::optional<types::ImageData> captureWindow(types::WindowHandle handle) = 0;
    
    /**
     * @brief 指定区域截图（ROI）
     * @param region 截图区域（相对于主显示器的坐标）
     * @param displayId 显示器ID（0表示主显示器）
     * @return 图像数据，失败返回std::nullopt
     */
    virtual std::optional<types::ImageData> captureRegion(
        const types::Rect& region, 
        int32_t displayId = 0
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
