# Layer 1: YOLO中频处理层技术选型设计方案

## 1. 需求分析

### 1.1 核心需求
- **相对准确的识别成功率**：mAP@0.5 ≥ 0.65，关键类别（人、窗口、图标）召回率 ≥ 0.80
- **极高的推理速度**：单帧推理时间 ≤ 50ms（目标20ms），支持1-10 FPS处理频率
- **极低的推理占用**：GPU显存占用 ≤ 500MB，CPU占用 ≤ 15%（单核），内存占用 ≤ 200MB
- **更多的可识别目标种类**：支持80+类别（COCO数据集），可扩展至自定义类别

### 1.2 约束条件
- 运行环境：Windows/Linux/macOS，支持GPU（NVIDIA/AMD/Intel）和CPU回退
- 集成方式：C++原生集成，避免Python依赖
- 部署便利性：模型文件 ≤ 50MB（量化后），支持动态加载

## 2. 技术选型方案

### 2.1 模型选择策略

#### 2.1.1 主模型：YOLOv8n（Nano版本）
**选择理由**：
- **速度优势**：参数量仅3.2M，推理速度最快
- **准确率平衡**：mAP@0.5:0.95 ≈ 37.3，mAP@0.5 ≈ 50.2（COCO验证集）
- **资源占用低**：FP16量化后模型大小约6MB，显存占用约150MB
- **类别覆盖**：支持COCO 80类，覆盖常见桌面场景物体

**适用场景**：
- 日常办公场景（窗口、图标、文本区域检测）
- 实时性要求高的场景
- GPU资源受限环境

#### 2.1.2 增强模型：YOLOv8s（Small版本）
**选择理由**：
- **准确率提升**：mAP@0.5:0.95 ≈ 44.9，mAP@0.5 ≈ 61.8
- **速度可接受**：参数量11.2M，推理速度约为nano版本的60-70%
- **资源占用适中**：FP16量化后模型大小约22MB，显存占用约300MB

**适用场景**：
- 复杂场景（多物体、小目标检测）
- 准确率优先场景
- GPU资源充足时自动切换

#### 2.1.3 专用模型：自定义训练模型（可选）
**扩展策略**：
- 基于YOLOv8n进行领域适应训练（Domain Adaptation）
- 针对桌面场景优化：窗口控件、UI元素、应用图标等
- 使用半监督学习扩充训练数据

### 2.2 推理引擎选择

#### 2.2.1 首选：ONNX Runtime（推荐）
**优势**：
- **跨平台支持**：Windows/Linux/macOS全平台
- **硬件加速**：支持CUDA、TensorRT、OpenVINO、DirectML
- **性能优秀**：TensorRT EP性能接近原生TensorRT
- **集成简单**：C++ API成熟，文档完善
- **模型格式统一**：ONNX格式，易于模型转换和优化

**配置方案**：
```cpp
// 优先级：TensorRT EP > CUDA EP > CPU EP
Ort::SessionOptions session_options;
session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

// TensorRT EP（NVIDIA GPU）
OrtTensorRTProviderOptions trt_options{};
trt_options.device_id = 0;
trt_options.trt_max_workspace_size = 1 << 30;  // 1GB
trt_options.trt_fp16_enable = true;  // FP16加速
session_options.AppendExecutionProvider_TensorRT(trt_options);

// CUDA EP（回退方案）
OrtCUDAProviderOptions cuda_options{};
cuda_options.device_id = 0;
cuda_options.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchExhaustive;
session_options.AppendExecutionProvider_CUDA(cuda_options);

// CPU EP（最终回退）
session_options.AppendExecutionProvider_CPU(CPUExecutionProviderOptions{});
```

**性能预期**：
- TensorRT EP：YOLOv8n单帧推理 15-25ms（RTX 3060）
- CUDA EP：YOLOv8n单帧推理 25-40ms（RTX 3060）
- CPU EP：YOLOv8n单帧推理 80-150ms（Intel i7-10700K）

#### 2.2.2 备选：TensorRT（NVIDIA专用）
**优势**：
- **极致性能**：NVIDIA GPU上最快的推理引擎
- **深度优化**：针对NVIDIA架构的专门优化
- **INT8量化**：支持INT8量化，进一步降低显存和提升速度

**劣势**：
- 仅支持NVIDIA GPU
- 需要额外的模型转换步骤
- 动态shape支持有限

**适用场景**：
- 仅部署在NVIDIA GPU环境
- 对性能有极致要求
- 可以接受更复杂的部署流程

#### 2.2.3 备选：OpenVINO（Intel专用）
**优势**：
- **Intel硬件优化**：针对Intel CPU和集成显卡优化
- **性能优秀**：在Intel硬件上性能优于ONNX Runtime CPU EP
- **免费开源**：Intel官方支持

**适用场景**：
- Intel CPU环境（无独立GPU）
- Intel集成显卡环境

### 2.3 模型优化策略

#### 2.3.1 量化方案
**FP16量化（推荐）**：
- **速度提升**：相比FP32提升1.5-2倍
- **显存降低**：显存占用降低50%
- **准确率损失**：< 1% mAP下降
- **实现方式**：ONNX Runtime自动量化或TensorRT FP16模式

**INT8量化（极致优化）**：
- **速度提升**：相比FP32提升2-3倍
- **显存降低**：显存占用降低75%
- **准确率损失**：2-5% mAP下降（需要校准数据集）
- **实现方式**：TensorRT INT8量化或ONNX Runtime QDQ量化

**量化策略**：
1. 默认使用FP16量化模型
2. GPU资源极度受限时使用INT8量化
3. 提供FP32模型作为准确率基准（用于验证）

#### 2.3.2 动态输入优化
**多分辨率策略**：
- **小分辨率**（640×480）：日常检测，速度优先
- **中分辨率**（1280×720）：平衡速度和准确率（默认）
- **大分辨率**（1920×1080）：复杂场景，准确率优先

**自适应分辨率**：
```cpp
// 根据场景复杂度动态调整输入分辨率
int selectInputResolution(const SceneComplexity& complexity) {
    if (complexity.isLow()) {
        return 640;  // 640×480
    } else if (complexity.isMedium()) {
        return 1280;  // 1280×720
    } else {
        return 1920;  // 1920×1080
    }
}
```

#### 2.3.3 批量推理优化
**策略**：
- Layer 0触发时，累积2-3帧进行批量推理
- 批量大小根据GPU显存动态调整（1-4帧）
- 批量推理可提升30-50%吞吐量

**实现**：
```cpp
class BatchInference {
    std::vector<cv::Mat> frame_buffer;
    const size_t max_batch_size = 4;
    
    void addFrame(const cv::Mat& frame) {
        frame_buffer.push_back(frame);
        if (frame_buffer.size() >= max_batch_size) {
            processBatch();
        }
    }
    
    void processBatch() {
        // 将多帧合并为batch tensor
        // 执行批量推理
        // 分离结果
    }
};
```

### 2.4 多模型组合策略

#### 2.4.1 模型切换机制
**基于场景复杂度**：
```cpp
enum class ModelTier {
    NANO,    // YOLOv8n：速度优先
    SMALL,   // YOLOv8s：平衡模式
    CUSTOM   // 自定义模型：特定场景
};

ModelTier selectModel(const SceneMetrics& metrics) {
    if (metrics.object_count < 5 && metrics.complexity < 0.3) {
        return ModelTier::NANO;
    } else if (metrics.object_count < 15) {
        return ModelTier::SMALL;
    } else {
        return ModelTier::CUSTOM;  // 复杂场景使用专用模型
    }
}
```

**基于硬件性能**：
```cpp
ModelTier selectModelByHardware(const HardwareInfo& hw) {
    if (hw.has_gpu && hw.gpu_memory_mb >= 2048) {
        return ModelTier::SMALL;  // 高性能GPU使用更大模型
    } else if (hw.has_gpu) {
        return ModelTier::NANO;   // 低端GPU使用小模型
    } else {
        return ModelTier::NANO;   // CPU回退使用最小模型
    }
}
```

#### 2.4.2 模型热切换
**实现**：
- 预加载多个模型到内存
- 根据场景动态切换，无需重新加载
- 使用模型池管理多个模型实例

```cpp
class ModelPool {
    std::map<ModelTier, std::unique_ptr<YOLOInference>> models;
    
    YOLOInference* getModel(ModelTier tier) {
        if (models.find(tier) == models.end()) {
            models[tier] = std::make_unique<YOLOInference>(tier);
        }
        return models[tier].get();
    }
};
```

### 2.5 类别扩展策略

#### 2.5.1 COCO 80类基础覆盖
**包含类别**：
- 人物：person
- 交通工具：car, bus, truck, motorcycle, bicycle等
- 动物：cat, dog, bird等
- 家具：chair, couch, bed等
- 电子设备：laptop, mouse, keyboard, cell phone等
- 其他：bottle, cup, book等

#### 2.5.2 桌面场景扩展类别
**自定义类别**（通过迁移学习添加）：
- UI元素：button, menu, toolbar, window_frame
- 应用图标：browser_icon, editor_icon, terminal_icon
- 文本区域：code_block, text_area, console_output
- 状态指示：loading_indicator, error_message, success_badge

**实现方式**：
1. 使用YOLOv8的迁移学习功能
2. 在COCO预训练模型基础上fine-tune
3. 使用半监督学习扩充标注数据

#### 2.5.3 类别优先级管理
**关键类别**（高优先级，低置信度阈值）：
- person: 0.3
- window_frame: 0.25
- laptop: 0.3

**一般类别**（标准阈值）：
- 其他COCO类别: 0.5

**自定义类别**（根据训练数据调整）：
- UI元素: 0.4
- 应用图标: 0.35

## 3. 性能优化方案

### 3.1 预处理优化
**图像预处理加速**：
- 使用OpenCV的GPU加速（cv::cuda）
- 使用SIMD指令优化（AVX2/AVX512）
- 减少不必要的颜色空间转换

```cpp
// GPU加速预处理
cv::cuda::GpuMat gpu_frame;
cv::cuda::resize(gpu_frame, gpu_frame, cv::Size(640, 640));
cv::cuda::cvtColor(gpu_frame, gpu_frame, cv::COLOR_BGR2RGB);
```

### 3.2 后处理优化
**NMS优化**：
- 使用GPU加速NMS（CUDA kernel）
- 使用快速NMS算法（Fast NMS）
- 批量NMS处理

**结果解析优化**：
- 使用SIMD指令加速tensor解析
- 减少内存拷贝操作
- 使用对象池复用检测结果对象

### 3.3 内存管理优化
**内存池**：
- 预分配输入/输出tensor内存
- 使用内存池避免频繁分配/释放
- GPU内存固定分配（pinned memory）

**显存管理**：
- 动态显存分配策略
- 显存使用监控和告警
- 自动降级到CPU（显存不足时）

### 3.4 并发优化
**异步推理**：
- 使用异步API（ONNX Runtime异步执行）
- 推理和预处理并行
- 多帧流水线处理

```cpp
class AsyncInference {
    std::queue<InferenceTask> task_queue;
    std::thread inference_thread;
    
    void inferenceLoop() {
        while (running) {
            if (!task_queue.empty()) {
                auto task = task_queue.front();
                task_queue.pop();
                auto result = model->inferAsync(task.input);
                task.callback(result);
            }
        }
    }
};
```

## 4. 硬件适配方案

### 4.1 GPU适配优先级
1. **NVIDIA GPU**：TensorRT EP > CUDA EP
2. **AMD GPU**：ROCm EP（如果ONNX Runtime支持）> OpenCL EP
3. **Intel GPU**：OpenVINO EP > DirectML EP

### 4.2 CPU回退策略
**触发条件**：
- 无GPU可用
- GPU显存不足
- GPU温度过高
- 用户手动选择CPU模式

**CPU优化**：
- 使用ONNX Runtime CPU EP的优化选项
- 启用多线程推理（线程数=CPU核心数）
- 使用Intel MKL-DNN加速（Intel CPU）

### 4.3 混合推理策略
**GPU+CPU混合**：
- 简单场景使用CPU（节省GPU资源）
- 复杂场景使用GPU（保证准确率）
- 根据实时负载动态分配

## 5. 实现架构设计

### 5.1 类结构设计
```cpp
class VisionLayer1 {
public:
    struct DetectionResult {
        std::vector<BoundingBox> boxes;
        std::vector<float> scores;
        std::vector<int> class_ids;
        float inference_time_ms;
    };
    
    struct ModelConfig {
        ModelTier tier;
        std::string model_path;
        float confidence_threshold;
        float nms_threshold;
        int input_width;
        int input_height;
    };
    
    // 初始化
    bool initialize(const ModelConfig& config);
    
    // 物体检测
    DetectionResult detect(const cv::Mat& image);
    
    // 批量检测
    std::vector<DetectionResult> detectBatch(
        const std::vector<cv::Mat>& images
    );
    
    // 场景分类（基于检测结果）
    SceneType classifyScene(const DetectionResult& detections);
    
    // 模型切换
    bool switchModel(ModelTier tier);
    
    // 性能统计
    PerformanceStats getStats() const;
    
private:
    std::unique_ptr<YOLOInference> m_inference;
    ModelPool m_model_pool;
    Preprocessor m_preprocessor;
    Postprocessor m_postprocessor;
    PerformanceMonitor m_perf_monitor;
};
```

### 5.2 依赖库选择
**核心依赖**：
- **ONNX Runtime**：1.16.0+（推理引擎）
- **OpenCV**：4.8.0+（图像处理）
- **CUDA**：11.8+（GPU加速，可选）
- **TensorRT**：8.6+（NVIDIA优化，可选）

**可选依赖**：
- **OpenVINO**：2023.0+（Intel优化）
- **Eigen**：3.4+（矩阵运算加速）

### 5.3 文件结构
```
include/naw/desktop_pet/service/
├── VisionLayer1.h
├── YOLOInference.h
├── ModelPool.h
├── Preprocessor.h
└── Postprocessor.h

src/naw/desktop_pet/service/
├── VisionLayer1.cpp
├── YOLOInference.cpp
├── ModelPool.cpp
├── Preprocessor.cpp
└── Postprocessor.cpp

models/
├── yolov8n.onnx          (FP32)
├── yolov8n_fp16.onnx     (FP16量化)
├── yolov8n_int8.onnx     (INT8量化)
├── yolov8s.onnx
├── yolov8s_fp16.onnx
└── custom_desktop.onnx   (自定义模型)
```

## 6. 性能目标与验收标准

### 6.1 性能目标
| 指标 | 目标值 | 备注 |
|------|--------|------|
| 单帧推理时间 | ≤ 50ms（目标20ms） | YOLOv8n + TensorRT EP |
| 批量推理吞吐 | ≥ 20 FPS | Batch size=4 |
| GPU显存占用 | ≤ 500MB | FP16量化模型 |
| CPU占用 | ≤ 15% | 单核，异步推理 |
| 内存占用 | ≤ 200MB | 包含模型和缓存 |
| mAP@0.5 | ≥ 0.65 | COCO验证集 |
| 关键类别召回率 | ≥ 0.80 | person, laptop等 |

### 6.2 验收标准
- [ ] 单测验证：YOLOv8n模型加载成功
- [ ] 单测验证：物体检测结果正确（检测框、类别、置信度）
- [ ] 单测验证：NMS处理正确（去除重复检测）
- [ ] 单测验证：场景分类正确（基于检测结果）
- [ ] 性能测试：单帧推理时间 ≤ 50ms（GPU）
- [ ] 性能测试：GPU显存占用 ≤ 500MB
- [ ] 性能测试：支持1-10 FPS处理频率
- [ ] 兼容性测试：支持NVIDIA/AMD/Intel GPU和CPU回退
- [ ] 稳定性测试：连续运行1小时无内存泄漏

## 7. 实施路线图

### 7.1 Phase 1：基础集成（1-2周）
- [ ] 集成ONNX Runtime
- [ ] 实现YOLOv8n模型加载和推理
- [ ] 实现基础预处理和后处理
- [ ] 实现CPU/GPU自动选择

### 7.2 Phase 2：性能优化（1-2周）
- [ ] 实现FP16量化模型支持
- [ ] 实现TensorRT EP集成
- [ ] 实现批量推理
- [ ] 实现异步推理

### 7.3 Phase 3：功能完善（1周）
- [ ] 实现多模型切换机制
- [ ] 实现场景分类功能
- [ ] 实现性能监控和统计
- [ ] 完善错误处理和日志

### 7.4 Phase 4：扩展优化（持续）
- [ ] 自定义模型训练和集成
- [ ] INT8量化支持
- [ ] 更多硬件平台适配
- [ ] 类别扩展和fine-tuning

## 8. 风险评估与应对

### 8.1 技术风险
**风险1：推理速度不达标**
- **应对**：使用TensorRT EP、INT8量化、降低输入分辨率
- **备选**：使用更小的模型（YOLOv8n → YOLOv5n）

**风险2：显存占用过高**
- **应对**：使用FP16/INT8量化、动态batch size、显存监控
- **备选**：强制CPU模式或使用更小的模型

**风险3：准确率不足**
- **应对**：使用YOLOv8s模型、提高输入分辨率、fine-tuning
- **备选**：集成多个模型进行ensemble

### 8.2 兼容性风险
**风险：跨平台兼容性问题**
- **应对**：优先使用ONNX Runtime（跨平台支持最好）
- **备选**：为不同平台提供不同的推理引擎实现

## 9. 参考资料

- [YOLOv8官方文档](https://docs.ultralytics.com/)
- [ONNX Runtime文档](https://onnxruntime.ai/docs/)
- [TensorRT开发者指南](https://docs.nvidia.com/deeplearning/tensorrt/)
- [COCO数据集](https://cocodataset.org/)
- [YOLO性能基准测试](https://github.com/ultralytics/ultralytics)
