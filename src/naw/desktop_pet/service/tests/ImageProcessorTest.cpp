#include "naw/desktop_pet/service/ImageProcessor.h"

#include <cassert>
#include <iostream>
#include <vector>

using naw::desktop_pet::service::ImageProcessor;
using naw::desktop_pet::service::types::ImageData;
using naw::desktop_pet::service::types::ImageFormat;

// 创建测试图像数据
ImageData createTestImage(uint32_t width, uint32_t height, ImageFormat format) {
    ImageData image;
    image.width = width;
    image.height = height;
    image.format = format;
    image.stride = 0;
    
    size_t dataSize = width * height * image.bytesPerPixel();
    image.data.resize(dataSize);
    
    // 填充测试数据（简单的渐变）
    for (size_t i = 0; i < dataSize; ++i) {
        image.data[i] = static_cast<uint8_t>(i % 256);
    }
    
    return image;
}

// 测试图像压缩
void testImageCompression() {
    std::cout << "Testing image compression...\n";
    
    // 创建测试图像
    auto image = createTestImage(640, 480, ImageFormat::BGR);
    assert(image.isValid());
    
    // 测试 JPEG 压缩
    {
        auto compressed = ImageProcessor::compressToJPEG(image, 85);
        assert(compressed.has_value());
        assert(!compressed->empty());
        std::cout << "  JPEG compression: " << compressed->size() << " bytes\n";
    }
    
    // 测试 PNG 压缩
    {
        auto compressed = ImageProcessor::compressToPNG(image, 3);
        assert(compressed.has_value());
        assert(!compressed->empty());
        std::cout << "  PNG compression: " << compressed->size() << " bytes\n";
    }
    
    // 测试无效图像
    {
        ImageData invalidImage;
        auto compressed = ImageProcessor::compressToJPEG(invalidImage, 85);
        assert(!compressed.has_value());
    }
    
    std::cout << "  Image compression tests passed!\n";
}

// 测试图像缩放
void testImageResize() {
    std::cout << "Testing image resize...\n";
    
    // 创建测试图像
    auto image = createTestImage(1920, 1080, ImageFormat::BGR);
    assert(image.isValid());
    
    // 测试基本缩放
    {
        auto resized = ImageProcessor::resize(image, 640, 480, ImageProcessor::InterpolationMethod::Linear);
        assert(resized.has_value());
        assert(resized->width == 640);
        assert(resized->height == 480);
        assert(resized->format == image.format);
        std::cout << "  Basic resize: " << resized->width << "x" << resized->height << "\n";
    }
    
    // 测试保持宽高比缩放
    {
        auto resized = ImageProcessor::resizeKeepAspectRatio(image, 1280, 0, ImageProcessor::InterpolationMethod::Linear);
        assert(resized.has_value());
        assert(resized->width == 1280);
        // 高度应该根据宽高比计算：1080 * (1280 / 1920) = 720
        assert(resized->height == 720);
        std::cout << "  Keep aspect ratio resize: " << resized->width << "x" << resized->height << "\n";
    }
    
    // 测试缩放并裁剪
    {
        auto resized = ImageProcessor::resizeAndCrop(image, 800, 600, ImageProcessor::InterpolationMethod::Linear);
        assert(resized.has_value());
        assert(resized->width == 800);
        assert(resized->height == 600);
        std::cout << "  Resize and crop: " << resized->width << "x" << resized->height << "\n";
    }
    
    // 测试无效参数
    {
        auto resized = ImageProcessor::resize(image, 0, 0, ImageProcessor::InterpolationMethod::Linear);
        assert(!resized.has_value());
    }
    
    std::cout << "  Image resize tests passed!\n";
}

// 测试分辨率配置
void testResolutionConfig() {
    std::cout << "Testing resolution configuration...\n";
    
    ImageProcessor::ResolutionConfig config;
    config.maxWidth = 1920;
    config.maxHeight = 1080;
    config.targetWidth = 1280;
    config.targetHeight = 720;
    config.keepAspectRatio = true;
    config.adaptive = false;
    
    // 测试在限制内的分辨率
    {
        auto [width, height] = ImageProcessor::getOptimalResolution(1600, 900, config);
        assert(width == 1280);
        assert(height == 720);
        std::cout << "  Optimal resolution (within limits): " << width << "x" << height << "\n";
    }
    
    // 测试超出最大限制的分辨率
    {
        auto [width, height] = ImageProcessor::getOptimalResolution(2560, 1440, config);
        // 应该被限制到最大分辨率，然后按目标分辨率缩放
        assert(width <= config.maxWidth.value());
        assert(height <= config.maxHeight.value());
        std::cout << "  Optimal resolution (exceeds max): " << width << "x" << height << "\n";
    }
    
    // 测试只设置最大限制
    {
        ImageProcessor::ResolutionConfig maxOnlyConfig;
        maxOnlyConfig.maxWidth = 1920;
        maxOnlyConfig.maxHeight = 1080;
        maxOnlyConfig.keepAspectRatio = true;
        
        auto [width, height] = ImageProcessor::getOptimalResolution(2560, 1440, maxOnlyConfig);
        assert(width == 1920);
        assert(height == 1080);
        std::cout << "  Optimal resolution (max only): " << width << "x" << height << "\n";
    }
    
    std::cout << "  Resolution configuration tests passed!\n";
}

// 测试自适应分辨率
void testAdaptiveResolution() {
    std::cout << "Testing adaptive resolution...\n";
    
    // 测试 Layer 0 (CV实时处理层)
    {
        auto [width, height] = ImageProcessor::calculateAdaptiveResolution(1920, 1080, 0);
        assert(width <= 640);
        assert(height <= 480);
        std::cout << "  Layer 0 adaptive: " << width << "x" << height << "\n";
    }
    
    // 测试 Layer 1 (YOLO中频处理层)
    {
        auto [width, height] = ImageProcessor::calculateAdaptiveResolution(2560, 1440, 1);
        assert(width <= 1280);
        assert(height <= 720);
        std::cout << "  Layer 1 adaptive: " << width << "x" << height << "\n";
    }
    
    // 测试 Layer 2 (复杂CV处理层)
    {
        auto [width, height] = ImageProcessor::calculateAdaptiveResolution(3840, 2160, 2);
        assert(width <= 1920);
        assert(height <= 1080);
        std::cout << "  Layer 2 adaptive: " << width << "x" << height << "\n";
    }
    
    // 测试 Layer 3 (VLM深度理解层)
    {
        auto [width, height] = ImageProcessor::calculateAdaptiveResolution(1920, 1080, 3);
        assert(width <= 1024);
        assert(height <= 768);
        std::cout << "  Layer 3 adaptive: " << width << "x" << height << "\n";
    }
    
    // 测试不需要调整的情况
    {
        auto [width, height] = ImageProcessor::calculateAdaptiveResolution(640, 480, 0);
        assert(width == 640);
        assert(height == 480);
        std::cout << "  No adjustment needed: " << width << "x" << height << "\n";
    }
    
    std::cout << "  Adaptive resolution tests passed!\n";
}

// 测试分辨率控制应用
void testApplyResolutionControl() {
    std::cout << "Testing apply resolution control...\n";
    
    // 创建测试图像
    auto image = createTestImage(2560, 1440, ImageFormat::BGR);
    assert(image.isValid());
    
    // 测试应用分辨率控制
    {
        ImageProcessor::ResolutionConfig config;
        config.maxWidth = 1920;
        config.maxHeight = 1080;
        config.keepAspectRatio = true;
        
        auto processed = ImageProcessor::applyResolutionControl(
            image, config, ImageProcessor::InterpolationMethod::Linear
        );
        assert(processed.has_value());
        assert(processed->width <= config.maxWidth.value());
        assert(processed->height <= config.maxHeight.value());
        std::cout << "  Applied resolution control: " << processed->width << "x" << processed->height << "\n";
    }
    
    // 测试不需要处理的情况
    {
        ImageProcessor::ResolutionConfig config;
        config.maxWidth = 3840;
        config.maxHeight = 2160;
        
        auto processed = ImageProcessor::applyResolutionControl(
            image, config, ImageProcessor::InterpolationMethod::Linear
        );
        assert(processed.has_value());
        assert(processed->width == image.width);
        assert(processed->height == image.height);
        std::cout << "  No processing needed: " << processed->width << "x" << processed->height << "\n";
    }
    
    std::cout << "  Apply resolution control tests passed!\n";
}

// 测试不同图像格式
void testDifferentFormats() {
    std::cout << "Testing different image formats...\n";
    
    // 测试 RGB 格式
    {
        auto image = createTestImage(640, 480, ImageFormat::RGB);
        auto resized = ImageProcessor::resize(image, 320, 240, ImageProcessor::InterpolationMethod::Linear);
        assert(resized.has_value());
        assert(resized->format == ImageFormat::RGB);
        std::cout << "  RGB format: OK\n";
    }
    
    // 测试 RGBA 格式
    {
        auto image = createTestImage(640, 480, ImageFormat::RGBA);
        auto resized = ImageProcessor::resize(image, 320, 240, ImageProcessor::InterpolationMethod::Linear);
        assert(resized.has_value());
        assert(resized->format == ImageFormat::RGBA);
        std::cout << "  RGBA format: OK\n";
    }
    
    // 测试 BGRA 格式
    {
        auto image = createTestImage(640, 480, ImageFormat::BGRA);
        auto resized = ImageProcessor::resize(image, 320, 240, ImageProcessor::InterpolationMethod::Linear);
        assert(resized.has_value());
        assert(resized->format == ImageFormat::BGRA);
        std::cout << "  BGRA format: OK\n";
    }
    
    // 测试灰度格式
    {
        auto image = createTestImage(640, 480, ImageFormat::Grayscale);
        auto resized = ImageProcessor::resize(image, 320, 240, ImageProcessor::InterpolationMethod::Linear);
        assert(resized.has_value());
        assert(resized->format == ImageFormat::Grayscale);
        std::cout << "  Grayscale format: OK\n";
    }
    
    std::cout << "  Different formats tests passed!\n";
}

int main() {
    std::cout << "=== ImageProcessor Unit Tests ===\n\n";
    
    try {
        testImageCompression();
        std::cout << "\n";
        
        testImageResize();
        std::cout << "\n";
        
        testResolutionConfig();
        std::cout << "\n";
        
        testAdaptiveResolution();
        std::cout << "\n";
        
        testApplyResolutionControl();
        std::cout << "\n";
        
        testDifferentFormats();
        std::cout << "\n";
        
        std::cout << "=== All tests passed! ===\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Test failed with unknown exception\n";
        return 1;
    }
}
