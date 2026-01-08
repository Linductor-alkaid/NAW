#pragma once

#include "naw/desktop_pet/service/types/ImageData.h"

#include <optional>
#include <string>
#include <vector>

namespace naw::desktop_pet::service {

/**
 * @brief 图像处理工具类
 * 
 * 提供图像压缩、缩放、分辨率控制等功能，使用 OpenCV 实现
 */
class ImageProcessor {
public:
    /**
     * @brief 插值算法枚举
     */
    enum class InterpolationMethod {
        Linear,      // 线性插值（cv::INTER_LINEAR）
        Cubic,       // 双三次插值（cv::INTER_CUBIC）
        Lanczos,     // Lanczos 插值（cv::INTER_LANCZOS4）
        Nearest      // 最近邻插值（cv::INTER_NEAREST）
    };

    /**
     * @brief 分辨率配置结构
     */
    struct ResolutionConfig {
        std::optional<uint32_t> maxWidth;      // 最大宽度（0表示无限制）
        std::optional<uint32_t> maxHeight;     // 最大高度（0表示无限制）
        std::optional<uint32_t> targetWidth;  // 目标宽度（0表示不缩放）
        std::optional<uint32_t> targetHeight; // 目标高度（0表示不缩放）
        bool keepAspectRatio{true};            // 是否保持宽高比
        bool adaptive{false};                  // 是否启用自适应分辨率
    };

    /**
     * @brief 压缩为 JPEG 格式
     * @param image 输入图像数据
     * @param quality JPEG 质量（0-100，默认85）
     * @return 压缩后的字节流，失败返回 std::nullopt
     */
    static std::optional<std::vector<uint8_t>> compressToJPEG(
        const types::ImageData& image,
        int quality = 85
    );

    /**
     * @brief 压缩为 PNG 格式
     * @param image 输入图像数据
     * @param compressionLevel PNG 压缩级别（0-9，默认3）
     * @return 压缩后的字节流，失败返回 std::nullopt
     */
    static std::optional<std::vector<uint8_t>> compressToPNG(
        const types::ImageData& image,
        int compressionLevel = 3
    );

    /**
     * @brief 缩放图像到指定分辨率
     * @param image 输入图像数据
     * @param targetWidth 目标宽度
     * @param targetHeight 目标高度
     * @param method 插值方法（默认线性插值）
     * @return 缩放后的图像数据，失败返回 std::nullopt
     */
    static std::optional<types::ImageData> resize(
        const types::ImageData& image,
        uint32_t targetWidth,
        uint32_t targetHeight,
        InterpolationMethod method = InterpolationMethod::Linear
    );

    /**
     * @brief 保持宽高比缩放图像
     * @param image 输入图像数据
     * @param targetWidth 目标宽度（0表示根据高度计算）
     * @param targetHeight 目标高度（0表示根据宽度计算）
     * @param method 插值方法（默认线性插值）
     * @return 缩放后的图像数据，失败返回 std::nullopt
     */
    static std::optional<types::ImageData> resizeKeepAspectRatio(
        const types::ImageData& image,
        uint32_t targetWidth,
        uint32_t targetHeight,
        InterpolationMethod method = InterpolationMethod::Linear
    );

    /**
     * @brief 缩放并裁剪到指定尺寸
     * @param image 输入图像数据
     * @param targetWidth 目标宽度
     * @param targetHeight 目标高度
     * @param method 插值方法（默认线性插值）
     * @return 缩放并裁剪后的图像数据，失败返回 std::nullopt
     */
    static std::optional<types::ImageData> resizeAndCrop(
        const types::ImageData& image,
        uint32_t targetWidth,
        uint32_t targetHeight,
        InterpolationMethod method = InterpolationMethod::Linear
    );

    /**
     * @brief 根据配置计算最优分辨率
     * @param currentWidth 当前宽度
     * @param currentHeight 当前高度
     * @param config 分辨率配置
     * @return 计算后的宽度和高度（pair<width, height>）
     */
    static std::pair<uint32_t, uint32_t> getOptimalResolution(
        uint32_t currentWidth,
        uint32_t currentHeight,
        const ResolutionConfig& config
    );

    /**
     * @brief 根据处理层需求计算自适应分辨率
     * @param currentWidth 当前宽度
     * @param currentHeight 当前高度
     * @param layerType 处理层类型（0=CV实时, 1=YOLO, 2=复杂CV, 3=VLM）
     * @return 计算后的宽度和高度（pair<width, height>）
     */
    static std::pair<uint32_t, uint32_t> calculateAdaptiveResolution(
        uint32_t currentWidth,
        uint32_t currentHeight,
        int layerType
    );

    /**
     * @brief 应用分辨率控制（根据配置进行缩放）
     * @param image 输入图像数据
     * @param config 分辨率配置
     * @param method 插值方法（默认线性插值）
     * @return 处理后的图像数据，失败返回 std::nullopt
     */
    static std::optional<types::ImageData> applyResolutionControl(
        const types::ImageData& image,
        const ResolutionConfig& config,
        InterpolationMethod method = InterpolationMethod::Linear
    );

private:
    /**
     * @brief 将 ImageData 转换为 OpenCV Mat
     * @param image 输入图像数据
     * @return OpenCV Mat 对象，失败返回空 Mat
     */
    static void imageDataToMat(const types::ImageData& image, void* outMat);

    /**
     * @brief 将 OpenCV Mat 转换为 ImageData
     * @param mat OpenCV Mat 对象（通过指针传递）
     * @param format 目标图像格式
     * @return ImageData 对象，失败返回 std::nullopt
     */
    static std::optional<types::ImageData> matToImageData(
        const void* mat,
        types::ImageFormat format
    );

    /**
     * @brief 获取 OpenCV 插值常量
     * @param method 插值方法枚举
     * @return OpenCV 插值常量
     */
    static int getOpenCVInterpolation(InterpolationMethod method);
};

} // namespace naw::desktop_pet::service
