#include "naw/desktop_pet/service/VisionLayer0.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace naw::desktop_pet::service {

// 内部实现类（PIMPL模式）
class VisionLayer0::Impl {
public:
    explicit Impl(const VisionLayer0Config& config)
        : config_(config)
        , adaptiveThreshold_(config.overallThreshold)
    {
    }

    VisionLayer0Result processFrame(const types::ImageData& frame) {
        VisionLayer0Result result;
        
        if (!frame.isValid()) {
            return result;
        }

        // 转换为 OpenCV Mat 并降低分辨率
        cv::Mat currentMat = imageDataToMat(frame);
        if (currentMat.empty()) {
            return result;
        }

        // 降低分辨率以提高性能
        cv::Mat processedMat;
        if (currentMat.cols != static_cast<int>(config_.processingWidth) ||
            currentMat.rows != static_cast<int>(config_.processingHeight)) {
            cv::resize(currentMat, processedMat,
                      cv::Size(static_cast<int>(config_.processingWidth),
                              static_cast<int>(config_.processingHeight)),
                      0, 0, cv::INTER_LINEAR);
        } else {
            processedMat = currentMat;
        }

        // 转换为灰度图（用于帧差和运动检测）
        cv::Mat grayMat;
        if (processedMat.channels() == 3) {
            cv::cvtColor(processedMat, grayMat, cv::COLOR_BGR2GRAY);
        } else if (processedMat.channels() == 4) {
            cv::cvtColor(processedMat, grayMat, cv::COLOR_BGRA2GRAY);
        } else if (processedMat.channels() == 1) {
            grayMat = processedMat;
        } else {
            return result;
        }

        // 如果有前一帧，进行各种检测
        if (!previousFrame_.empty() && previousFrame_.size() == grayMat.size()) {
            // 帧差检测
            result.frameDiffScore = detectFrameDifference(
                grayMat, previousFrame_, result.changedRegions);

            // 色彩分析（使用彩色图）
            result.colorChangeScore = analyzeColor(
                processedMat, previousColorFrame_, result.dominantColors);

            // 运动检测
            if (config_.enableMotionDetection) {
                result.motionScore = detectMotion(
                    grayMat, previousFrame_, result.motionRegions);
            }
        } else {
            // 第一帧，初始化
            result.frameDiffScore = 0.0;
            result.colorChangeScore = 0.0;
            result.motionScore = 0.0;
        }

        // 计算综合评分
        result.overallChangeScore = 
            config_.frameDiffWeight * result.frameDiffScore +
            config_.colorChangeWeight * result.colorChangeScore +
            config_.motionWeight * result.motionScore;

        // 判断是否需要触发下一层
        double threshold = config_.enableAdaptiveThreshold ? 
                          adaptiveThreshold_ : config_.overallThreshold;
        result.shouldTriggerLayer1 = result.overallChangeScore >= threshold;

        // 更新自适应阈值
        if (config_.enableAdaptiveThreshold) {
            updateAdaptiveThreshold(result);
        }

        // 保存当前帧作为下一帧的前一帧
        previousFrame_ = grayMat.clone();
        previousColorFrame_ = processedMat.clone();

        return result;
    }

    void reset() {
        previousFrame_.release();
        previousColorFrame_.release();
        previousPoints_.clear();
        adaptiveThreshold_ = config_.overallThreshold;
    }

    void updateConfig(const VisionLayer0Config& config) {
        config_ = config;
        if (!config_.enableAdaptiveThreshold) {
            adaptiveThreshold_ = config_.overallThreshold;
        }
    }

    const VisionLayer0Config& getConfig() const {
        return config_;
    }

private:
    // 将 ImageData 转换为 cv::Mat
    cv::Mat imageDataToMat(const types::ImageData& image) {
        if (!image.isValid()) {
            return cv::Mat();
        }

        int cvType = 0;
        switch (image.format) {
            case types::ImageFormat::Grayscale:
                cvType = CV_8UC1;
                break;
            case types::ImageFormat::RGB:
            case types::ImageFormat::BGR:
                cvType = CV_8UC3;
                break;
            case types::ImageFormat::RGBA:
            case types::ImageFormat::BGRA:
                cvType = CV_8UC4;
                break;
        }

        uint32_t stride = image.stride > 0 ? image.stride : 
                         (image.width * image.bytesPerPixel());

        cv::Mat mat(static_cast<int>(image.height),
                   static_cast<int>(image.width),
                   cvType);

        // 复制数据
        if (image.stride > 0 && image.stride != image.width * image.bytesPerPixel()) {
            // 有 padding，逐行复制
            for (uint32_t y = 0; y < image.height; ++y) {
                const uint8_t* srcRow = image.data.data() + y * image.stride;
                uint8_t* dstRow = mat.ptr<uint8_t>(static_cast<int>(y));
                std::memcpy(dstRow, srcRow, image.width * image.bytesPerPixel());
            }
        } else {
            // 连续存储，直接复制
            std::memcpy(mat.data, image.data.data(), 
                       std::min(image.data.size(), 
                               static_cast<size_t>(mat.total() * mat.elemSize())));
        }

        // 颜色空间转换（RGB -> BGR）
        if (image.format == types::ImageFormat::RGB) {
            cv::Mat temp;
            cv::cvtColor(mat, temp, cv::COLOR_RGB2BGR);
            mat = temp;
        } else if (image.format == types::ImageFormat::RGBA) {
            cv::Mat temp;
            cv::cvtColor(mat, temp, cv::COLOR_RGBA2BGRA);
            mat = temp;
        }

        return mat;
    }

    // 帧差检测
    double detectFrameDifference(const cv::Mat& current, const cv::Mat& previous,
                                 std::vector<types::Rect>& changedRegions) {
        if (current.empty() || previous.empty() || 
            current.size() != previous.size()) {
            return 0.0;
        }

        // 计算帧差
        cv::Mat diff;
        cv::absdiff(current, previous, diff);

        // 二值化
        cv::Mat binary;
        cv::threshold(diff, binary, 
                     static_cast<double>(config_.frameDiffThreshold * 255.0),
                     255, cv::THRESH_BINARY);

        // 形态学操作去除噪声
        cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_RECT,
            cv::Size(config_.morphKernelSize, config_.morphKernelSize));
        cv::Mat morphed;
        cv::morphologyEx(binary, morphed, cv::MORPH_OPEN, kernel);
        cv::morphologyEx(morphed, morphed, cv::MORPH_CLOSE, kernel);

        // 连通域分析
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(morphed, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        // 计算变化区域和评分
        changedRegions.clear();
        int changedPixels = 0;
        int totalPixels = morphed.rows * morphed.cols;

        for (const auto& contour : contours) {
            if (contour.size() < 3) continue;

            cv::Rect boundingRect = cv::boundingRect(contour);
            types::Rect rect;
            rect.x = boundingRect.x;
            rect.y = boundingRect.y;
            rect.width = static_cast<uint32_t>(boundingRect.width);
            rect.height = static_cast<uint32_t>(boundingRect.height);
            changedRegions.push_back(rect);

            // 计算该区域内的变化像素数
            cv::Mat roi = morphed(boundingRect);
            changedPixels += cv::countNonZero(roi);
        }

        // 计算变化评分（0-1）
        double score = static_cast<double>(changedPixels) / static_cast<double>(totalPixels);
        return std::min(1.0, score);
    }

    // 色彩分析
    double analyzeColor(const cv::Mat& current, const cv::Mat& previous,
                       std::vector<float>& dominantColors) {
        if (current.empty() || previous.empty() ||
            current.size() != previous.size()) {
            return 0.0;
        }

        // 降采样以提高性能
        cv::Mat currentSampled, previousSampled;
        int sampleFactor = 4; // 每4个像素采样一次
        cv::resize(current, currentSampled,
                  cv::Size(current.cols / sampleFactor, current.rows / sampleFactor),
                  0, 0, cv::INTER_AREA);
        cv::resize(previous, previousSampled,
                  cv::Size(previous.cols / sampleFactor, previous.rows / sampleFactor),
                  0, 0, cv::INTER_AREA);

        // 转换为HSV色彩空间
        cv::Mat currentHSV, previousHSV;
        cv::cvtColor(currentSampled, currentHSV, cv::COLOR_BGR2HSV);
        cv::cvtColor(previousSampled, previousHSV, cv::COLOR_BGR2HSV);

        // 计算直方图
        int histSize = config_.histogramBins;
        float range[] = {0, 256};
        const float* histRange = {range};

        std::vector<cv::Mat> currentChannels, previousChannels;
        cv::split(currentHSV, currentChannels);
        cv::split(previousHSV, previousChannels);

        cv::Mat currentHist, previousHist;
        cv::calcHist(&currentChannels[0], 1, nullptr, cv::Mat(), currentHist,
                    1, &histSize, &histRange, true, false);
        cv::calcHist(&previousChannels[0], 1, nullptr, cv::Mat(), previousHist,
                    1, &histSize, &histRange, true, false);

        // 归一化直方图
        cv::normalize(currentHist, currentHist, 0, 1, cv::NORM_MINMAX, -1, cv::Mat());
        cv::normalize(previousHist, previousHist, 0, 1, cv::NORM_MINMAX, -1, cv::Mat());

        // 计算直方图差异（使用相关性方法）
        double correlation = cv::compareHist(currentHist, previousHist, cv::HISTCMP_CORREL);
        double colorChangeScore = 1.0 - correlation; // 相关性越低，变化越大

        // 可选：提取主色调（如果启用）
        if (config_.enableDominantColor) {
            extractDominantColors(currentSampled, dominantColors);
        } else {
            dominantColors.clear();
        }

        return std::max(0.0, std::min(1.0, colorChangeScore));
    }

    // 提取主色调（使用K-means聚类）
    void extractDominantColors(const cv::Mat& image, std::vector<float>& colors) {
        if (image.empty() || image.channels() != 3) {
            colors.clear();
            return;
        }

        // 将图像重塑为样本点
        cv::Mat samples = image.reshape(1, image.rows * image.cols);
        samples.convertTo(samples, CV_32F);

        // K-means聚类（提取3种主色调）
        int K = 3;
        cv::Mat labels, centers;
        cv::kmeans(samples, K, labels,
                  cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 10, 1.0),
                  3, cv::KMEANS_PP_CENTERS, centers);

        // 提取主色调（BGR格式，转换为RGB）
        colors.clear();
        for (int i = 0; i < centers.rows; ++i) {
            float b = centers.at<float>(i, 0);
            float g = centers.at<float>(i, 1);
            float r = centers.at<float>(i, 2);
            colors.push_back(r / 255.0f);
            colors.push_back(g / 255.0f);
            colors.push_back(b / 255.0f);
        }
    }

    // 运动检测（使用稀疏光流）
    double detectMotion(const cv::Mat& current, const cv::Mat& previous,
                       std::vector<types::Rect>& motionRegions) {
        if (current.empty() || previous.empty() ||
            current.size() != previous.size()) {
            return 0.0;
        }

        // 检测特征点（使用Shi-Tomasi角点检测）
        std::vector<cv::Point2f> currentPoints;
        cv::goodFeaturesToTrack(previous, currentPoints, config_.opticalFlowPoints,
                               0.01, 10, cv::Mat(), 3, false, 0.04);

        if (currentPoints.empty()) {
            return 0.0;
        }

        // 计算光流
        std::vector<cv::Point2f> nextPoints;
        std::vector<uchar> status;
        std::vector<float> err;
        cv::calcOpticalFlowPyrLK(previous, current, currentPoints, nextPoints,
                                status, err, cv::Size(15, 15), 2,
                                cv::TermCriteria(cv::TermCriteria::COUNT + 
                                               cv::TermCriteria::EPS, 10, 0.03));

        // 计算运动向量
        double totalMotion = 0.0;
        int validPoints = 0;
        std::vector<cv::Point2f> motionPoints;

        for (size_t i = 0; i < currentPoints.size(); ++i) {
            if (status[i] == 1) {
                cv::Point2f motion = nextPoints[i] - currentPoints[i];
                double magnitude = std::sqrt(motion.x * motion.x + motion.y * motion.y);
                totalMotion += magnitude;
                validPoints++;

                // 如果运动幅度超过阈值，记录该点
                if (magnitude > config_.motionThreshold * 10.0) {
                    motionPoints.push_back(currentPoints[i]);
                }
            }
        }

        // 计算平均运动强度
        double avgMotion = validPoints > 0 ? totalMotion / validPoints : 0.0;
        double motionScore = std::min(1.0, avgMotion / 50.0); // 归一化到0-1

        // 如果运动点足够多，计算运动区域
        motionRegions.clear();
        if (motionPoints.size() > 5) {
            // 使用凸包或边界框来定义运动区域
            std::vector<cv::Point2f> hull;
            cv::convexHull(motionPoints, hull);

            if (hull.size() >= 3) {
                cv::Rect boundingRect = cv::boundingRect(hull);
                types::Rect rect;
                rect.x = boundingRect.x;
                rect.y = boundingRect.y;
                rect.width = static_cast<uint32_t>(boundingRect.width);
                rect.height = static_cast<uint32_t>(boundingRect.height);
                motionRegions.push_back(rect);
            }
        }

        // 保存当前点作为下一帧的前一点
        previousPoints_ = currentPoints;

        return motionScore;
    }

    // 更新自适应阈值
    void updateAdaptiveThreshold(const VisionLayer0Result& result) {
        // 简单的自适应算法：根据最近的变化模式调整阈值
        // 如果变化持续较大，提高阈值；如果变化较小，降低阈值
        const double alpha = 0.1; // 学习率
        const double targetScore = 0.3; // 目标评分

        if (result.overallChangeScore > targetScore) {
            // 变化较大，提高阈值
            adaptiveThreshold_ = adaptiveThreshold_ * (1.0 + alpha);
        } else {
            // 变化较小，降低阈值
            adaptiveThreshold_ = adaptiveThreshold_ * (1.0 - alpha);
        }

        // 限制阈值范围
        adaptiveThreshold_ = std::max(0.05, std::min(0.5, adaptiveThreshold_));
    }

    VisionLayer0Config config_;
    cv::Mat previousFrame_;          // 前一帧（灰度图）
    cv::Mat previousColorFrame_;     // 前一帧（彩色图）
    std::vector<cv::Point2f> previousPoints_; // 前一帧特征点（用于光流）
    double adaptiveThreshold_;       // 自适应阈值
};

// VisionLayer0 公共接口实现

VisionLayer0::VisionLayer0(const VisionLayer0Config& config)
    : pImpl_(std::make_unique<Impl>(config))
    , config_(config)
{
}

VisionLayer0::~VisionLayer0() = default;

VisionLayer0Result VisionLayer0::processFrame(const types::ImageData& frame) {
    return pImpl_->processFrame(frame);
}

void VisionLayer0::reset() {
    pImpl_->reset();
}

void VisionLayer0::updateConfig(const VisionLayer0Config& config) {
    config_ = config;
    pImpl_->updateConfig(config);
}

} // namespace naw::desktop_pet::service
