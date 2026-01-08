#include "naw/desktop_pet/service/ImageProcessor.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace naw::desktop_pet::service {

namespace {
    // 将 ImageData 格式转换为 OpenCV Mat 类型
    int imageFormatToCVType(types::ImageFormat format) {
        switch (format) {
            case types::ImageFormat::Grayscale:
                return CV_8UC1;
            case types::ImageFormat::RGB:
            case types::ImageFormat::BGR:
                return CV_8UC3;
            case types::ImageFormat::RGBA:
            case types::ImageFormat::BGRA:
                return CV_8UC4;
            default:
                return CV_8UC3;
        }
    }

    // 检查是否需要颜色通道转换
    bool needsColorConversion(types::ImageFormat srcFormat, types::ImageFormat dstFormat) {
        if (srcFormat == dstFormat) {
            return false;
        }
        // BGR <-> RGB 需要转换
        if ((srcFormat == types::ImageFormat::BGR && dstFormat == types::ImageFormat::RGB) ||
            (srcFormat == types::ImageFormat::RGB && dstFormat == types::ImageFormat::BGR) ||
            (srcFormat == types::ImageFormat::BGRA && dstFormat == types::ImageFormat::RGBA) ||
            (srcFormat == types::ImageFormat::RGBA && dstFormat == types::ImageFormat::BGRA)) {
            return true;
        }
        return false;
    }
}

std::optional<std::vector<uint8_t>> ImageProcessor::compressToJPEG(
    const types::ImageData& image,
    int quality
) {
    if (!image.isValid()) {
        return std::nullopt;
    }

    if (quality < 0 || quality > 100) {
        quality = 85; // 默认质量
    }

    try {
        // 转换为 OpenCV Mat
        cv::Mat mat;
        imageDataToMat(image, &mat);
        
        if (mat.empty()) {
            return std::nullopt;
        }

        // 压缩参数
        std::vector<int> compressionParams;
        compressionParams.push_back(cv::IMWRITE_JPEG_QUALITY);
        compressionParams.push_back(quality);

        // 压缩为 JPEG
        std::vector<uint8_t> buffer;
        if (!cv::imencode(".jpg", mat, buffer, compressionParams)) {
            return std::nullopt;
        }

        return buffer;
    } catch (const cv::Exception& e) {
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::vector<uint8_t>> ImageProcessor::compressToPNG(
    const types::ImageData& image,
    int compressionLevel
) {
    if (!image.isValid()) {
        return std::nullopt;
    }

    if (compressionLevel < 0 || compressionLevel > 9) {
        compressionLevel = 3; // 默认压缩级别
    }

    try {
        // 转换为 OpenCV Mat
        cv::Mat mat;
        imageDataToMat(image, &mat);
        
        if (mat.empty()) {
            return std::nullopt;
        }

        // 压缩参数
        std::vector<int> compressionParams;
        compressionParams.push_back(cv::IMWRITE_PNG_COMPRESSION);
        compressionParams.push_back(compressionLevel);

        // 压缩为 PNG
        std::vector<uint8_t> buffer;
        if (!cv::imencode(".png", mat, buffer, compressionParams)) {
            return std::nullopt;
        }

        return buffer;
    } catch (const cv::Exception& e) {
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<types::ImageData> ImageProcessor::resize(
    const types::ImageData& image,
    uint32_t targetWidth,
    uint32_t targetHeight,
    InterpolationMethod method
) {
    if (!image.isValid() || targetWidth == 0 || targetHeight == 0) {
        return std::nullopt;
    }

    try {
        // 转换为 OpenCV Mat
        cv::Mat srcMat;
        imageDataToMat(image, &srcMat);
        
        if (srcMat.empty()) {
            return std::nullopt;
        }

        // 缩放
        cv::Mat dstMat;
        cv::resize(srcMat, dstMat, cv::Size(static_cast<int>(targetWidth), static_cast<int>(targetHeight)),
                   0.0, 0.0, getOpenCVInterpolation(method));

        // 转换回 ImageData（保持原始格式）
        return matToImageData(&dstMat, image.format);
    } catch (const cv::Exception& e) {
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<types::ImageData> ImageProcessor::resizeKeepAspectRatio(
    const types::ImageData& image,
    uint32_t targetWidth,
    uint32_t targetHeight,
    InterpolationMethod method
) {
    if (!image.isValid()) {
        return std::nullopt;
    }

    // 如果只指定了一个维度，根据另一个维度计算
    if (targetWidth == 0 && targetHeight == 0) {
        return std::nullopt;
    }

    if (targetWidth == 0) {
        // 根据高度计算宽度
        double ratio = static_cast<double>(targetHeight) / static_cast<double>(image.height);
        targetWidth = static_cast<uint32_t>(std::round(image.width * ratio));
    } else if (targetHeight == 0) {
        // 根据宽度计算高度
        double ratio = static_cast<double>(targetWidth) / static_cast<double>(image.width);
        targetHeight = static_cast<uint32_t>(std::round(image.height * ratio));
    } else {
        // 两个维度都指定，计算缩放比例，取较小的以保持宽高比
        double widthRatio = static_cast<double>(targetWidth) / static_cast<double>(image.width);
        double heightRatio = static_cast<double>(targetHeight) / static_cast<double>(image.height);
        double ratio = std::min(widthRatio, heightRatio);
        
        targetWidth = static_cast<uint32_t>(std::round(image.width * ratio));
        targetHeight = static_cast<uint32_t>(std::round(image.height * ratio));
    }

    return resize(image, targetWidth, targetHeight, method);
}

std::optional<types::ImageData> ImageProcessor::resizeAndCrop(
    const types::ImageData& image,
    uint32_t targetWidth,
    uint32_t targetHeight,
    InterpolationMethod method
) {
    if (!image.isValid() || targetWidth == 0 || targetHeight == 0) {
        return std::nullopt;
    }

    try {
        // 转换为 OpenCV Mat
        cv::Mat srcMat;
        imageDataToMat(image, &srcMat);
        
        if (srcMat.empty()) {
            return std::nullopt;
        }

        // 计算缩放比例（取较大的，确保能覆盖目标尺寸）
        double widthRatio = static_cast<double>(targetWidth) / static_cast<double>(image.width);
        double heightRatio = static_cast<double>(targetHeight) / static_cast<double>(image.height);
        double ratio = std::max(widthRatio, heightRatio);

        // 先缩放
        uint32_t scaledWidth = static_cast<uint32_t>(std::round(image.width * ratio));
        uint32_t scaledHeight = static_cast<uint32_t>(std::round(image.height * ratio));
        
        cv::Mat scaledMat;
        cv::resize(srcMat, scaledMat, cv::Size(static_cast<int>(scaledWidth), static_cast<int>(scaledHeight)),
                   0.0, 0.0, getOpenCVInterpolation(method));

        // 计算裁剪区域（居中裁剪）
        int cropX = (scaledWidth - targetWidth) / 2;
        int cropY = (scaledHeight - targetHeight) / 2;
        cv::Rect cropRect(cropX, cropY, static_cast<int>(targetWidth), static_cast<int>(targetHeight));

        // 裁剪
        cv::Mat dstMat = scaledMat(cropRect);

        // 转换回 ImageData
        return matToImageData(&dstMat, image.format);
    } catch (const cv::Exception& e) {
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::pair<uint32_t, uint32_t> ImageProcessor::getOptimalResolution(
    uint32_t currentWidth,
    uint32_t currentHeight,
    const ResolutionConfig& config
) {
    uint32_t width = currentWidth;
    uint32_t height = currentHeight;

    // 应用最大分辨率限制
    if (config.maxWidth.has_value() && config.maxWidth.value() > 0 && width > config.maxWidth.value()) {
        double ratio = static_cast<double>(config.maxWidth.value()) / static_cast<double>(width);
        width = config.maxWidth.value();
        if (config.keepAspectRatio) {
            height = static_cast<uint32_t>(std::round(height * ratio));
        }
    }
    if (config.maxHeight.has_value() && config.maxHeight.value() > 0 && height > config.maxHeight.value()) {
        double ratio = static_cast<double>(config.maxHeight.value()) / static_cast<double>(height);
        height = config.maxHeight.value();
        if (config.keepAspectRatio) {
            width = static_cast<uint32_t>(std::round(width * ratio));
        }
    }

    // 应用目标分辨率
    if (config.targetWidth.has_value() && config.targetWidth.value() > 0) {
        width = config.targetWidth.value();
    }
    if (config.targetHeight.has_value() && config.targetHeight.value() > 0) {
        height = config.targetHeight.value();
    }

    // 如果启用自适应分辨率，进一步调整
    if (config.adaptive) {
        // 这里可以根据性能需求进一步调整
        // 暂时使用目标分辨率
    }

    return {width, height};
}

std::pair<uint32_t, uint32_t> ImageProcessor::calculateAdaptiveResolution(
    uint32_t currentWidth,
    uint32_t currentHeight,
    int layerType
) {
    // 根据处理层类型返回推荐分辨率
    // Layer 0: CV实时处理层 - 可以降低分辨率以提高性能（100+ FPS）
    // Layer 1: YOLO中频处理层 - 中等分辨率（1-10 FPS）
    // Layer 2: 复杂CV处理层 - 按需，可以使用原始分辨率
    // Layer 3: VLM深度理解层 - 可以使用较低分辨率（0.1-1次/分钟）

    switch (layerType) {
        case 0: // CV实时处理层
            // 降低到 640x480 或更小以提高性能
            if (currentWidth > 640 || currentHeight > 480) {
                double ratio = std::min(640.0 / currentWidth, 480.0 / currentHeight);
                return {
                    static_cast<uint32_t>(std::round(currentWidth * ratio)),
                    static_cast<uint32_t>(std::round(currentHeight * ratio))
                };
            }
            break;
        case 1: // YOLO中频处理层
            // 限制到 1280x720 或更小
            if (currentWidth > 1280 || currentHeight > 720) {
                double ratio = std::min(1280.0 / currentWidth, 720.0 / currentHeight);
                return {
                    static_cast<uint32_t>(std::round(currentWidth * ratio)),
                    static_cast<uint32_t>(std::round(currentHeight * ratio))
                };
            }
            break;
        case 2: // 复杂CV处理层
            // 保持原始分辨率或适度降低
            if (currentWidth > 1920 || currentHeight > 1080) {
                double ratio = std::min(1920.0 / currentWidth, 1080.0 / currentHeight);
                return {
                    static_cast<uint32_t>(std::round(currentWidth * ratio)),
                    static_cast<uint32_t>(std::round(currentHeight * ratio))
                };
            }
            break;
        case 3: // VLM深度理解层
            // 降低到 1024x768 或更小以节省API调用成本
            if (currentWidth > 1024 || currentHeight > 768) {
                double ratio = std::min(1024.0 / currentWidth, 768.0 / currentHeight);
                return {
                    static_cast<uint32_t>(std::round(currentWidth * ratio)),
                    static_cast<uint32_t>(std::round(currentHeight * ratio))
                };
            }
            break;
        default:
            break;
    }

    // 不需要调整，返回原始分辨率
    return {currentWidth, currentHeight};
}

std::optional<types::ImageData> ImageProcessor::applyResolutionControl(
    const types::ImageData& image,
    const ResolutionConfig& config,
    InterpolationMethod method
) {
    if (!image.isValid()) {
        return std::nullopt;
    }

    // 计算最优分辨率
    auto [targetWidth, targetHeight] = getOptimalResolution(
        image.width, image.height, config
    );

    // 如果分辨率没有变化，直接返回原图
    if (targetWidth == image.width && targetHeight == image.height) {
        return image;
    }

    // 根据是否保持宽高比选择缩放方法
    if (config.keepAspectRatio) {
        return resizeKeepAspectRatio(image, targetWidth, targetHeight, method);
    } else {
        return resize(image, targetWidth, targetHeight, method);
    }
}

void ImageProcessor::imageDataToMat(const types::ImageData& image, void* outMat) {
    cv::Mat* mat = static_cast<cv::Mat*>(outMat);
    
    if (!image.isValid()) {
        *mat = cv::Mat();
        return;
    }

    int cvType = imageFormatToCVType(image.format);
    
    // 计算实际步长
    uint32_t stride = image.stride > 0 ? image.stride : (image.width * image.bytesPerPixel());
    
    // 创建 Mat（复制数据以确保安全，因为 OpenCV 可能会修改数据）
    *mat = cv::Mat(
        static_cast<int>(image.height),
        static_cast<int>(image.width),
        cvType
    );

    // 复制数据
    if (image.stride > 0 && image.stride != image.width * image.bytesPerPixel()) {
        // 有 padding，需要逐行复制
        for (uint32_t y = 0; y < image.height; ++y) {
            const uint8_t* srcRow = image.data.data() + y * image.stride;
            uint8_t* dstRow = mat->ptr<uint8_t>(static_cast<int>(y));
            std::memcpy(dstRow, srcRow, image.width * image.bytesPerPixel());
        }
    } else {
        // 连续存储，直接复制
        std::memcpy(mat->data, image.data.data(), image.data.size());
    }

    // 如果需要颜色空间转换
    if (image.format == types::ImageFormat::RGB) {
        cv::Mat temp;
        cv::cvtColor(*mat, temp, cv::COLOR_RGB2BGR);
        *mat = temp;
    } else if (image.format == types::ImageFormat::RGBA) {
        cv::Mat temp;
        cv::cvtColor(*mat, temp, cv::COLOR_RGBA2BGRA);
        *mat = temp;
    }
    // BGR 和 BGRA 已经是 OpenCV 默认格式，不需要转换
}

std::optional<types::ImageData> ImageProcessor::matToImageData(
    const void* mat,
    types::ImageFormat format
) {
    const cv::Mat* srcMat = static_cast<const cv::Mat*>(mat);
    
    if (srcMat->empty()) {
        return std::nullopt;
    }

    types::ImageData result;
    result.width = static_cast<uint32_t>(srcMat->cols);
    result.height = static_cast<uint32_t>(srcMat->rows);
    result.format = format;
    result.stride = 0; // 连续存储

    // 确定目标格式的通道数
    int targetChannels = 0;
    switch (format) {
        case types::ImageFormat::Grayscale:
            targetChannels = 1;
            break;
        case types::ImageFormat::RGB:
        case types::ImageFormat::BGR:
            targetChannels = 3;
            break;
        case types::ImageFormat::RGBA:
        case types::ImageFormat::BGRA:
            targetChannels = 4;
            break;
    }

    cv::Mat processedMat;
    
    // 根据源 Mat 和目标格式进行转换
    if (srcMat->channels() == 1 && targetChannels > 1) {
        // 灰度转彩色
        cv::cvtColor(*srcMat, processedMat, cv::COLOR_GRAY2BGR);
        if (targetChannels == 4) {
            cv::cvtColor(processedMat, processedMat, cv::COLOR_BGR2BGRA);
        }
    } else if (srcMat->channels() == 3 && targetChannels == 4) {
        // BGR 转 BGRA
        cv::cvtColor(*srcMat, processedMat, cv::COLOR_BGR2BGRA);
    } else if (srcMat->channels() == 4 && targetChannels == 3) {
        // BGRA 转 BGR
        cv::cvtColor(*srcMat, processedMat, cv::COLOR_BGRA2BGR);
    } else if (srcMat->channels() > 1 && targetChannels == 1) {
        // 彩色转灰度
        cv::cvtColor(*srcMat, processedMat, cv::COLOR_BGR2GRAY);
    } else {
        processedMat = *srcMat;
    }

    // 颜色空间转换（如果需要）
    if (format == types::ImageFormat::RGB && processedMat.channels() == 3) {
        cv::cvtColor(processedMat, processedMat, cv::COLOR_BGR2RGB);
    } else if (format == types::ImageFormat::RGBA && processedMat.channels() == 4) {
        cv::cvtColor(processedMat, processedMat, cv::COLOR_BGRA2RGBA);
    }

    // 复制数据到 ImageData
    size_t dataSize = processedMat.total() * processedMat.elemSize();
    result.data.resize(dataSize);
    std::memcpy(result.data.data(), processedMat.data, dataSize);

    return result;
}

int ImageProcessor::getOpenCVInterpolation(InterpolationMethod method) {
    switch (method) {
        case InterpolationMethod::Linear:
            return cv::INTER_LINEAR;
        case InterpolationMethod::Cubic:
            return cv::INTER_CUBIC;
        case InterpolationMethod::Lanczos:
            return cv::INTER_LANCZOS4;
        case InterpolationMethod::Nearest:
            return cv::INTER_NEAREST;
        default:
            return cv::INTER_LINEAR;
    }
}

} // namespace naw::desktop_pet::service
