#pragma once

#include "naw/desktop_pet/service/APIClient.h"
#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/ErrorTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace naw::desktop_pet::service {

/**
 * @brief 上下文信息提纯器
 * 
 * 使用嵌入模型和重排序模型对过长或包含无用信息的上下文进行智能提纯。
 * 支持对工具输出、项目上下文、代码上下文等进行优化。
 */
class ContextRefiner {
public:
    explicit ContextRefiner(ConfigManager& configManager, APIClient& apiClient);
    ~ContextRefiner() = default;

    // 禁止拷贝/移动
    ContextRefiner(const ContextRefiner&) = delete;
    ContextRefiner& operator=(const ContextRefiner&) = delete;
    ContextRefiner(ContextRefiner&&) = delete;
    ContextRefiner& operator=(ContextRefiner&&) = delete;

    /**
     * @brief 提纯上下文内容
     * @param text 原始文本
     * @param query 查询文本（用于重排序，可选）
     * @param error 错误信息输出（可选）
     * @return 提纯后的文本，如果失败或不需要提纯则返回原始文本
     */
    std::string refineContext(
        const std::string& text,
        const std::optional<std::string>& query = std::nullopt,
        ErrorInfo* error = nullptr
    );

    /**
     * @brief 检查是否启用提纯功能
     */
    bool isEnabled() const;

private:
    /**
     * @brief 将文本分割为块
     * @param text 原始文本
     * @return 文本块列表
     */
    std::vector<std::string> chunkText(const std::string& text) const;

    /**
     * @brief 自适应筛选相关片段
     * @param chunks 文本块列表
     * @param rerankResults 重排序结果
     * @return 筛选后的文本块索引列表
     */
    std::vector<size_t> adaptiveFilter(
        const std::vector<std::string>& chunks,
        const std::vector<APIClient::RerankResult>& rerankResults
    ) const;

    /**
     * @brief 估算文本的字符数
     */
    size_t estimateChars(const std::string& text) const;

    /**
     * @brief 估算文本的Token数（简单估算）
     */
    size_t estimateTokens(const std::string& text) const;

    ConfigManager& m_configManager;
    APIClient& m_apiClient;

    // 配置缓存
    bool m_enabled{true};
    size_t m_thresholdChars{2000};
    size_t m_thresholdTokens{500};
    size_t m_chunkSize{500};
    size_t m_chunkOverlap{50};
    float m_adaptiveThreshold{0.5f};
    int m_topK{10};
};

} // namespace naw::desktop_pet::service

