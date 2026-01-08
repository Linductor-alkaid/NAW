#pragma once

#include "naw/desktop_pet/service/types/ImageData.h"

#include <vector>
#include <memory>

namespace naw::desktop_pet::service {

/**
 * @brief Layer 0 处理结果
 */
struct VisionLayer0Result {
    // 帧差检测结果
    double frameDiffScore{0.0};              // 帧差评分（0-1）
    std::vector<types::Rect> changedRegions; // 变化区域列表
    
    // 色彩分析结果
    double colorChangeScore{0.0};           // 色彩变化评分（0-1）
    std::vector<float> dominantColors;       // 主色调（RGB，可选）
    
    // 运动检测结果
    double motionScore{0.0};                // 运动评分（0-1）
    std::vector<types::Rect> motionRegions; // 运动区域列表
    
    // 综合评分
    double overallChangeScore{0.0};         // 综合变化评分（0-1）
    bool shouldTriggerLayer1{false};        // 是否应该触发Layer 1
};

/**
 * @brief Layer 0 配置参数
 */
struct VisionLayer0Config {
    // 帧差检测参数
    double frameDiffThreshold{0.1};        // 帧差阈值
    int morphKernelSize{3};                // 形态学核大小
    
    // 色彩分析参数
    int histogramBins{32};                 // 直方图bins数量
    double colorChangeThreshold{0.15};     // 色彩变化阈值
    bool enableDominantColor{false};       // 是否提取主色调
    
    // 运动检测参数
    bool enableMotionDetection{true};      // 是否启用运动检测
    int opticalFlowPoints{100};            // 光流特征点数量
    double motionThreshold{0.1};           // 运动阈值
    
    // 综合评分参数
    double frameDiffWeight{0.4};            // 帧差权重
    double colorChangeWeight{0.3};          // 色彩变化权重
    double motionWeight{0.3};               // 运动权重
    double overallThreshold{0.2};          // 综合阈值
    
    // 性能参数
    uint32_t processingWidth{640};        // 处理宽度（降低分辨率）
    uint32_t processingHeight{480};       // 处理高度
    bool enableAdaptiveThreshold{true};    // 是否启用自适应阈值
};

/**
 * @brief Layer 0: CV实时处理层
 * 
 * 负责实时检测屏幕变化，为后续的YOLO层和VLM层提供触发信号。
 * 该层需要达到100+ FPS的处理性能。
 * 
 * 功能包括：
 * - 帧差检测：检测当前帧与前一帧的差异
 * - 色彩分析：分析色彩变化和主色调
 * - 运动检测：使用光流检测运动
 * - 变化阈值判断：综合评分并判断是否需要触发下一层
 */
class VisionLayer0 {
public:
    /**
     * @brief 构造函数
     * @param config 配置参数
     */
    explicit VisionLayer0(const VisionLayer0Config& config = VisionLayer0Config());
    
    /**
     * @brief 析构函数
     */
    ~VisionLayer0();
    
    /**
     * @brief 处理单帧图像
     * @param frame 输入图像数据
     * @return 处理结果
     */
    VisionLayer0Result processFrame(const types::ImageData& frame);
    
    /**
     * @brief 重置状态（清除前一帧缓存）
     */
    void reset();
    
    /**
     * @brief 更新配置
     * @param config 新配置
     */
    void updateConfig(const VisionLayer0Config& config);
    
    /**
     * @brief 获取当前配置
     * @return 当前配置的引用
     */
    const VisionLayer0Config& getConfig() const { return config_; }

private:
    // 前向声明内部实现类（PIMPL模式，避免在头文件中暴露OpenCV）
    class Impl;
    std::unique_ptr<Impl> pImpl_;
    
    VisionLayer0Config config_;
};

} // namespace naw::desktop_pet::service
