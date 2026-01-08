#include "naw/desktop_pet/service/VisionLayer0.h"

#include <cassert>
#include <iostream>
#include <vector>
#include <chrono>

using naw::desktop_pet::service::VisionLayer0;
using naw::desktop_pet::service::VisionLayer0Config;
using naw::desktop_pet::service::VisionLayer0Result;
using naw::desktop_pet::service::types::ImageData;
using naw::desktop_pet::service::types::ImageFormat;

// 创建测试图像数据
ImageData createTestImage(uint32_t width, uint32_t height, ImageFormat format, 
                         uint8_t r = 128, uint8_t g = 128, uint8_t b = 128) {
    ImageData image;
    image.width = width;
    image.height = height;
    image.format = format;
    image.stride = 0;
    
    size_t dataSize = width * height * image.bytesPerPixel();
    image.data.resize(dataSize);
    
    // 填充测试数据（单色或渐变）
    if (format == ImageFormat::Grayscale) {
        for (size_t i = 0; i < dataSize; ++i) {
            image.data[i] = r;
        }
    } else if (format == ImageFormat::BGR || format == ImageFormat::RGB) {
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                size_t idx = (y * width + x) * 3;
                if (format == ImageFormat::BGR) {
                    image.data[idx + 0] = b;
                    image.data[idx + 1] = g;
                    image.data[idx + 2] = r;
                } else {
                    image.data[idx + 0] = r;
                    image.data[idx + 1] = g;
                    image.data[idx + 2] = b;
                }
            }
        }
    } else if (format == ImageFormat::BGRA || format == ImageFormat::RGBA) {
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                size_t idx = (y * width + x) * 4;
                if (format == ImageFormat::BGRA) {
                    image.data[idx + 0] = b;
                    image.data[idx + 1] = g;
                    image.data[idx + 2] = r;
                    image.data[idx + 3] = 255;
                } else {
                    image.data[idx + 0] = r;
                    image.data[idx + 1] = g;
                    image.data[idx + 2] = b;
                    image.data[idx + 3] = 255;
                }
            }
        }
    }
    
    return image;
}

// 创建有变化的测试图像
ImageData createChangedImage(const ImageData& base, uint32_t changeX, uint32_t changeY,
                             uint32_t changeWidth, uint32_t changeHeight,
                             uint8_t r = 255, uint8_t g = 0, uint8_t b = 0) {
    ImageData changed = base;
    
    if (base.format == ImageFormat::BGR || base.format == ImageFormat::RGB) {
        for (uint32_t y = changeY; y < std::min(changeY + changeHeight, base.height); ++y) {
            for (uint32_t x = changeX; x < std::min(changeX + changeWidth, base.width); ++x) {
                size_t idx = (y * base.width + x) * 3;
                if (base.format == ImageFormat::BGR) {
                    changed.data[idx + 0] = b;
                    changed.data[idx + 1] = g;
                    changed.data[idx + 2] = r;
                } else {
                    changed.data[idx + 0] = r;
                    changed.data[idx + 1] = g;
                    changed.data[idx + 2] = b;
                }
            }
        }
    }
    
    return changed;
}

// 测试帧差检测
void testFrameDifference() {
    std::cout << "Testing frame difference detection...\n";
    
    VisionLayer0Config config;
    config.processingWidth = 640;
    config.processingHeight = 480;
    VisionLayer0 layer0(config);
    
    // 创建第一帧（全黑）
    auto frame1 = createTestImage(1920, 1080, ImageFormat::BGR, 0, 0, 0);
    auto result1 = layer0.processFrame(frame1);
    
    // 第一帧应该没有变化
    assert(result1.frameDiffScore == 0.0);
    assert(result1.changedRegions.empty());
    std::cout << "  First frame (no change): score = " << result1.frameDiffScore << "\n";
    
    // 创建第二帧（相同）
    auto frame2 = createTestImage(1920, 1080, ImageFormat::BGR, 0, 0, 0);
    auto result2 = layer0.processFrame(frame2);
    
    // 相同帧应该没有变化
    assert(result2.frameDiffScore < 0.1); // 允许小的数值误差
    std::cout << "  Same frame (no change): score = " << result2.frameDiffScore << "\n";
    
    // 创建第三帧（有变化区域）
    auto frame3 = createChangedImage(frame2, 100, 100, 200, 200, 255, 255, 255);
    auto result3 = layer0.processFrame(frame3);
    
    // 应该有变化
    assert(result3.frameDiffScore > 0.0);
    assert(!result3.changedRegions.empty());
    std::cout << "  Changed frame: score = " << result3.frameDiffScore 
              << ", regions = " << result3.changedRegions.size() << "\n";
    
    std::cout << "  Frame difference tests passed!\n";
}

// 测试色彩分析
void testColorAnalysis() {
    std::cout << "Testing color analysis...\n";
    
    VisionLayer0Config config;
    config.processingWidth = 640;
    config.processingHeight = 480;
    VisionLayer0 layer0(config);
    
    // 创建第一帧（红色）
    auto frame1 = createTestImage(1920, 1080, ImageFormat::BGR, 255, 0, 0);
    auto result1 = layer0.processFrame(frame1);
    
    // 第一帧应该没有色彩变化
    assert(result1.colorChangeScore == 0.0);
    std::cout << "  First frame (no color change): score = " << result1.colorChangeScore << "\n";
    
    // 创建第二帧（相同颜色）
    auto frame2 = createTestImage(1920, 1080, ImageFormat::BGR, 255, 0, 0);
    auto result2 = layer0.processFrame(frame2);
    
    // 相同颜色应该没有变化
    assert(result2.colorChangeScore < 0.1);
    std::cout << "  Same color (no change): score = " << result2.colorChangeScore << "\n";
    
    // 创建第三帧（不同颜色 - 蓝色）
    auto frame3 = createTestImage(1920, 1080, ImageFormat::BGR, 0, 0, 255);
    auto result3 = layer0.processFrame(frame3);
    
    // 应该有色彩变化
    assert(result3.colorChangeScore > 0.0);
    std::cout << "  Different color (changed): score = " << result3.colorChangeScore << "\n";
    
    // 测试主色调提取
    VisionLayer0Config configWithDominantColor = config;
    configWithDominantColor.enableDominantColor = true;
    VisionLayer0 layer0WithColor(configWithDominantColor);
    
    auto frame4 = createTestImage(1920, 1080, ImageFormat::BGR, 100, 150, 200);
    auto result4 = layer0WithColor.processFrame(frame4);
    auto result5 = layer0WithColor.processFrame(frame4);
    
    // 如果启用了主色调提取，应该有主色调数据
    if (result5.dominantColors.size() > 0) {
        std::cout << "  Dominant colors extracted: " << result5.dominantColors.size() / 3 << " colors\n";
    }
    
    std::cout << "  Color analysis tests passed!\n";
}

// 测试运动检测
void testMotionDetection() {
    std::cout << "Testing motion detection...\n";
    
    VisionLayer0Config config;
    config.processingWidth = 640;
    config.processingHeight = 480;
    config.enableMotionDetection = true;
    VisionLayer0 layer0(config);
    
    // 创建第一帧
    auto frame1 = createTestImage(1920, 1080, ImageFormat::BGR, 128, 128, 128);
    auto result1 = layer0.processFrame(frame1);
    
    // 第一帧应该没有运动
    assert(result1.motionScore == 0.0);
    std::cout << "  First frame (no motion): score = " << result1.motionScore << "\n";
    
    // 创建第二帧（相同）
    auto frame2 = createTestImage(1920, 1080, ImageFormat::BGR, 128, 128, 128);
    auto result2 = layer0.processFrame(frame2);
    
    // 静止场景应该没有运动
    assert(result2.motionScore < 0.1);
    std::cout << "  Static scene (no motion): score = " << result2.motionScore << "\n";
    
    // 创建第三帧（有变化，模拟运动）
    auto frame3 = createChangedImage(frame2, 200, 200, 100, 100, 255, 255, 255);
    auto result3 = layer0.processFrame(frame3);
    
    // 应该有运动（虽然可能很小）
    std::cout << "  Changed scene (motion): score = " << result3.motionScore << "\n";
    
    // 测试禁用运动检测
    VisionLayer0Config configNoMotion = config;
    configNoMotion.enableMotionDetection = false;
    VisionLayer0 layer0NoMotion(configNoMotion);
    
    auto result4 = layer0NoMotion.processFrame(frame1);
    auto result5 = layer0NoMotion.processFrame(frame3);
    
    // 禁用时运动评分应该为0
    assert(result5.motionScore == 0.0);
    std::cout << "  Motion detection disabled: score = " << result5.motionScore << "\n";
    
    std::cout << "  Motion detection tests passed!\n";
}

// 测试综合评分和阈值判断
void testOverallScore() {
    std::cout << "Testing overall score and threshold judgment...\n";
    
    VisionLayer0Config config;
    config.processingWidth = 640;
    config.processingHeight = 480;
    config.overallThreshold = 0.2;
    config.enableAdaptiveThreshold = false;
    VisionLayer0 layer0(config);
    
    // 创建第一帧
    auto frame1 = createTestImage(1920, 1080, ImageFormat::BGR, 128, 128, 128);
    auto result1 = layer0.processFrame(frame1);
    
    // 第一帧综合评分应该为0
    assert(result1.overallChangeScore == 0.0);
    assert(!result1.shouldTriggerLayer1);
    std::cout << "  First frame: overall score = " << result1.overallChangeScore 
              << ", trigger = " << result1.shouldTriggerLayer1 << "\n";
    
    // 创建第二帧（相同）
    auto frame2 = createTestImage(1920, 1080, ImageFormat::BGR, 128, 128, 128);
    auto result2 = layer0.processFrame(frame2);
    
    // 相同帧不应该触发
    assert(result2.overallChangeScore < config.overallThreshold);
    assert(!result2.shouldTriggerLayer1);
    std::cout << "  Same frame: overall score = " << result2.overallChangeScore 
              << ", trigger = " << result2.shouldTriggerLayer1 << "\n";
    
    // 创建第三帧（有明显变化）
    auto frame3 = createChangedImage(frame2, 0, 0, 640, 480, 255, 0, 0);
    auto result3 = layer0.processFrame(frame3);
    
    // 有明显变化时应该触发（如果评分超过阈值）
    std::cout << "  Changed frame: overall score = " << result3.overallChangeScore 
              << ", trigger = " << result3.shouldTriggerLayer1 << "\n";
    
    // 测试自适应阈值
    VisionLayer0Config configAdaptive = config;
    configAdaptive.enableAdaptiveThreshold = true;
    VisionLayer0 layer0Adaptive(configAdaptive);
    
    auto result4 = layer0Adaptive.processFrame(frame1);
    auto result5 = layer0Adaptive.processFrame(frame3);
    
    std::cout << "  Adaptive threshold: overall score = " << result5.overallChangeScore 
              << ", trigger = " << result5.shouldTriggerLayer1 << "\n";
    
    std::cout << "  Overall score tests passed!\n";
}

// 测试重置功能
void testReset() {
    std::cout << "Testing reset functionality...\n";
    
    VisionLayer0Config config;
    config.processingWidth = 640;
    config.processingHeight = 480;
    VisionLayer0 layer0(config);
    
    // 处理几帧
    auto frame1 = createTestImage(1920, 1080, ImageFormat::BGR, 128, 128, 128);
    layer0.processFrame(frame1);
    layer0.processFrame(frame1);
    
    // 重置
    layer0.reset();
    
    // 重置后处理第一帧应该没有变化
    auto result = layer0.processFrame(frame1);
    assert(result.frameDiffScore == 0.0);
    assert(result.colorChangeScore == 0.0);
    assert(result.motionScore == 0.0);
    
    std::cout << "  Reset tests passed!\n";
}

// 测试配置更新
void testConfigUpdate() {
    std::cout << "Testing config update...\n";
    
    VisionLayer0Config config;
    config.processingWidth = 640;
    config.processingHeight = 480;
    VisionLayer0 layer0(config);
    
    // 更新配置
    VisionLayer0Config newConfig = config;
    newConfig.processingWidth = 320;
    newConfig.processingHeight = 240;
    newConfig.overallThreshold = 0.3;
    layer0.updateConfig(newConfig);
    
    // 验证配置已更新
    const auto& updatedConfig = layer0.getConfig();
    assert(updatedConfig.processingWidth == 320);
    assert(updatedConfig.processingHeight == 240);
    assert(updatedConfig.overallThreshold == 0.3);
    
    std::cout << "  Config update tests passed!\n";
}

// 性能测试
void testPerformance() {
    std::cout << "Testing performance...\n";
    
    VisionLayer0Config config;
    config.processingWidth = 640;
    config.processingHeight = 480;
    VisionLayer0 layer0(config);
    
    // 创建测试图像
    auto frame = createTestImage(1920, 1080, ImageFormat::BGR, 128, 128, 128);
    
    // 预热
    for (int i = 0; i < 10; ++i) {
        layer0.processFrame(frame);
    }
    
    // 性能测试
    const int testFrames = 1000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < testFrames; ++i) {
        layer0.processFrame(frame);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    double fps = (testFrames * 1000.0) / duration.count();
    std::cout << "  Processed " << testFrames << " frames in " << duration.count() << " ms\n";
    std::cout << "  FPS: " << fps << "\n";
    
    // 目标：>100 FPS
    if (fps >= 100.0) {
        std::cout << "  Performance target met (>= 100 FPS)!\n";
    } else {
        std::cout << "  Warning: Performance below target (< 100 FPS)\n";
    }
    
    std::cout << "  Performance tests completed!\n";
}

// 主函数
int main() {
    std::cout << "=== VisionLayer0 Unit Tests ===\n\n";
    
    try {
        testFrameDifference();
        std::cout << "\n";
        
        testColorAnalysis();
        std::cout << "\n";
        
        testMotionDetection();
        std::cout << "\n";
        
        testOverallScore();
        std::cout << "\n";
        
        testReset();
        std::cout << "\n";
        
        testConfigUpdate();
        std::cout << "\n";
        
        testPerformance();
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
