#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace naw::desktop_pet::service::types {

/**
 * @brief 图像格式枚举
 */
enum class ImageFormat {
    RGB,    // RGB24 (每像素3字节)
    BGR,    // BGR24 (每像素3字节，OpenCV默认)
    RGBA,   // RGBA32 (每像素4字节)
    BGRA,   // BGRA32 (每像素4字节)
    Grayscale // 灰度图 (每像素1字节)
};

/**
 * @brief 图像数据结构
 * 
 * 存储屏幕截图的图像数据，支持多种格式和颜色空间
 */
struct ImageData {
    std::vector<uint8_t> data;  // 图像数据（行优先存储）
    uint32_t width{0};           // 图像宽度（像素）
    uint32_t height{0};          // 图像高度（像素）
    ImageFormat format{ImageFormat::BGR}; // 图像格式
    uint32_t stride{0};         // 每行字节数（可能包含padding，0表示连续存储）
    
    /**
     * @brief 计算每像素字节数
     */
    uint32_t bytesPerPixel() const {
        switch (format) {
            case ImageFormat::RGB:
            case ImageFormat::BGR:
                return 3;
            case ImageFormat::RGBA:
            case ImageFormat::BGRA:
                return 4;
            case ImageFormat::Grayscale:
                return 1;
            default:
                return 3;
        }
    }
    
    /**
     * @brief 计算图像数据总大小（字节）
     */
    size_t totalSize() const {
        if (stride > 0) {
            return stride * height;
        }
        return width * height * bytesPerPixel();
    }
    
    /**
     * @brief 检查数据是否有效
     */
    bool isValid() const {
        return width > 0 && height > 0 && !data.empty() && 
               data.size() >= totalSize();
    }
    
    /**
     * @brief 清空数据
     */
    void clear() {
        data.clear();
        width = 0;
        height = 0;
        stride = 0;
    }
    
    /**
     * @brief 分配内存
     */
    void allocate(uint32_t w, uint32_t h, ImageFormat f, uint32_t s = 0) {
        width = w;
        height = h;
        format = f;
        stride = s;
        
        if (stride > 0) {
            data.resize(stride * height);
        } else {
            data.resize(width * height * bytesPerPixel());
        }
    }
};

/**
 * @brief 矩形区域定义
 */
struct Rect {
    int32_t x{0};      // 左上角X坐标
    int32_t y{0};      // 左上角Y坐标
    uint32_t width{0}; // 宽度
    uint32_t height{0}; // 高度
    
    bool isValid() const {
        return width > 0 && height > 0;
    }
    
    bool contains(int32_t px, int32_t py) const {
        return px >= x && px < static_cast<int32_t>(x + width) &&
               py >= y && py < static_cast<int32_t>(y + height);
    }
};

/**
 * @brief 显示器信息
 */
struct DisplayInfo {
    uint32_t id{0};                    // 显示器ID
    std::string name;                  // 显示器名称
    Rect bounds;                       // 显示器边界（相对于主显示器）
    bool isPrimary{false};            // 是否为主显示器
    uint32_t refreshRate{60};          // 刷新率（Hz）
    
    // 物理尺寸（可选，单位：毫米）
    std::optional<uint32_t> physicalWidth;
    std::optional<uint32_t> physicalHeight;
};

/**
 * @brief 窗口句柄（平台无关的抽象）
 */
using WindowHandle = void*;

} // namespace naw::desktop_pet::service::types
