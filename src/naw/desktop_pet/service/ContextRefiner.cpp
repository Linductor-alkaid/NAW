#include "naw/desktop_pet/service/ContextRefiner.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <regex>

namespace naw::desktop_pet::service {

ContextRefiner::ContextRefiner(ConfigManager& configManager, APIClient& apiClient)
    : m_configManager(configManager)
    , m_apiClient(apiClient)
{
    // 加载配置
    if (auto v = m_configManager.get("context_refinement.enabled"); v.has_value() && v->is_boolean()) {
        m_enabled = v->get<bool>();
    }

    if (auto v = m_configManager.get("context_refinement.threshold_chars"); v.has_value()) {
        if (v->is_number_unsigned()) {
            m_thresholdChars = v->get<size_t>();
        } else if (v->is_number_integer()) {
            int64_t val = v->get<int64_t>();
            if (val > 0) m_thresholdChars = static_cast<size_t>(val);
        }
    }

    if (auto v = m_configManager.get("context_refinement.threshold_tokens"); v.has_value()) {
        if (v->is_number_unsigned()) {
            m_thresholdTokens = v->get<size_t>();
        } else if (v->is_number_integer()) {
            int64_t val = v->get<int64_t>();
            if (val > 0) m_thresholdTokens = static_cast<size_t>(val);
        }
    }

    if (auto v = m_configManager.get("context_refinement.chunk_size"); v.has_value()) {
        if (v->is_number_unsigned()) {
            m_chunkSize = v->get<size_t>();
        } else if (v->is_number_integer()) {
            int64_t val = v->get<int64_t>();
            if (val > 0) m_chunkSize = static_cast<size_t>(val);
        }
    }

    if (auto v = m_configManager.get("context_refinement.chunk_overlap"); v.has_value()) {
        if (v->is_number_unsigned()) {
            m_chunkOverlap = v->get<size_t>();
        } else if (v->is_number_integer()) {
            int64_t val = v->get<int64_t>();
            if (val > 0) m_chunkOverlap = static_cast<size_t>(val);
        }
    }

    if (auto v = m_configManager.get("context_refinement.rerank.adaptive_threshold"); v.has_value() && v->is_number()) {
        m_adaptiveThreshold = v->get<float>();
    }

    if (auto v = m_configManager.get("context_refinement.rerank.top_k"); v.has_value()) {
        if (v->is_number_integer()) {
            int val = v->get<int>();
            if (val > 0) m_topK = val;
        }
    }
}

bool ContextRefiner::isEnabled() const {
    return m_enabled;
}

std::string ContextRefiner::refineContext(
    const std::string& text,
    const std::optional<std::string>& query,
    ErrorInfo* error
) {
    // 如果未启用，直接返回原始文本
    if (!m_enabled) {
        return text;
    }

    // 检查是否需要提纯
    size_t charCount = estimateChars(text);
    size_t tokenCount = estimateTokens(text);

    if (charCount < m_thresholdChars && tokenCount < m_thresholdTokens) {
        // 不需要提纯
        return text;
    }

    try {
        // 文本分块
        std::vector<std::string> chunks = chunkText(text);
        if (chunks.empty()) {
            return text;
        }

        // ========== 嵌入模型和重排序模型已禁用 ==========
        // 使用简单策略：直接选择前 topK 个块
        // 如果块数少于 topK，则返回所有块
        
        std::vector<size_t> selectedIndices;
        size_t maxChunks = std::min<size_t>(static_cast<size_t>(m_topK), chunks.size());
        for (size_t i = 0; i < maxChunks; ++i) {
            selectedIndices.push_back(i);
        }

        // 构建提纯后的文本
        if (selectedIndices.empty()) {
            // 如果没有选中任何块，返回原始文本
            return text;
        }

        std::ostringstream result;
        for (size_t i = 0; i < selectedIndices.size(); ++i) {
            if (i > 0) {
                result << "\n\n";
            }
            result << chunks[selectedIndices[i]];
        }

        return result.str();

    } catch (const std::exception& e) {
        // 异常处理，回退到原始文本
        if (error) {
            error->errorType = ErrorType::UnknownError;
            error->errorCode = 0;
            error->message = std::string("Exception during refinement: ") + e.what();
        }
        return text;
    } catch (...) {
        // 未知异常，回退到原始文本
        if (error) {
            error->errorType = ErrorType::UnknownError;
            error->errorCode = 0;
            error->message = "Unknown exception during refinement";
        }
        return text;
    }
}

std::vector<std::string> ContextRefiner::chunkText(const std::string& text) const {
    std::vector<std::string> chunks;

    if (text.empty()) {
        return chunks;
    }

    // 优先在段落边界分割
    std::vector<size_t> splitPoints;
    
    // 查找段落分隔符（双换行）
    size_t pos = 0;
    while ((pos = text.find("\n\n", pos)) != std::string::npos) {
        splitPoints.push_back(pos);
        pos += 2;
    }

    // 如果没有段落分隔符，查找单换行
    if (splitPoints.empty()) {
        pos = 0;
        while ((pos = text.find('\n', pos)) != std::string::npos) {
            splitPoints.push_back(pos);
            pos += 1;
        }
    }

    // 如果没有换行，按固定大小分割
    if (splitPoints.empty()) {
        size_t start = 0;
        while (start < text.size()) {
            size_t end = std::min(start + m_chunkSize, text.size());
            chunks.push_back(text.substr(start, end - start));
            start = end > m_chunkOverlap ? end - m_chunkOverlap : end;
        }
        return chunks;
    }

    // 使用滑动窗口在分割点之间创建块
    size_t currentPos = 0;
    std::string currentChunk;

    for (size_t splitPoint : splitPoints) {
        std::string segment = text.substr(currentPos, splitPoint - currentPos + 2);
        
        // 如果当前块加上新段落后超过大小限制，保存当前块并开始新块
        if (!currentChunk.empty() && 
            (currentChunk.size() + segment.size() > m_chunkSize)) {
            chunks.push_back(currentChunk);
            
            // 保留重叠部分
            if (m_chunkOverlap > 0 && currentChunk.size() > m_chunkOverlap) {
                currentChunk = currentChunk.substr(currentChunk.size() - m_chunkOverlap);
            } else {
                currentChunk.clear();
            }
        }
        
        currentChunk += segment;
        currentPos = splitPoint + 2;
    }

    // 添加最后一个块
    if (!currentChunk.empty()) {
        chunks.push_back(currentChunk);
    }

    // 处理剩余的文本
    if (currentPos < text.size()) {
        std::string remaining = text.substr(currentPos);
        if (!remaining.empty()) {
            if (chunks.empty() || chunks.back().size() + remaining.size() <= m_chunkSize) {
                if (chunks.empty()) {
                    chunks.push_back(remaining);
                } else {
                    chunks.back() += remaining;
                }
            } else {
                chunks.push_back(remaining);
            }
        }
    }

    // 确保没有空块
    chunks.erase(
        std::remove_if(chunks.begin(), chunks.end(),
            [](const std::string& s) { return s.empty(); }),
        chunks.end()
    );

    return chunks;
}

std::vector<size_t> ContextRefiner::adaptiveFilter(
    const std::vector<std::string>& chunks,
    const std::vector<APIClient::RerankResult>& rerankResults
) const {
    std::vector<size_t> selectedIndices;

    if (rerankResults.empty()) {
        // 如果没有重排序结果，返回所有块的索引
        for (size_t i = 0; i < chunks.size(); ++i) {
            selectedIndices.push_back(i);
        }
        return selectedIndices;
    }

    // 计算分数统计信息
    std::vector<float> scores;
    for (const auto& result : rerankResults) {
        scores.push_back(result.score);
    }

    if (scores.empty()) {
        // 如果没有分数，返回所有块的索引
        for (size_t i = 0; i < chunks.size(); ++i) {
            selectedIndices.push_back(i);
        }
        return selectedIndices;
    }

    // 计算平均分和标准差
    float sum = 0.0f;
    for (float score : scores) {
        sum += score;
    }
    float mean = sum / scores.size();

    float variance = 0.0f;
    for (float score : scores) {
        float diff = score - mean;
        variance += diff * diff;
    }
    float stdDev = std::sqrt(variance / scores.size());

    // 自适应阈值：使用平均值和标准差
    float threshold = std::max(m_adaptiveThreshold, mean - stdDev);

    // 至少保留 top-k 个结果
    size_t minKeep = std::min<size_t>(m_topK, rerankResults.size());

    // 筛选结果
    std::vector<std::pair<size_t, float>> candidates;
    for (const auto& result : rerankResults) {
        if (result.index < chunks.size()) {
            candidates.push_back({result.index, result.score});
        }
    }

    // 按分数排序
    std::sort(candidates.begin(), candidates.end(),
        [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

    // 选择满足阈值的结果，但至少保留 minKeep 个
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (i < minKeep || candidates[i].second >= threshold) {
            selectedIndices.push_back(candidates[i].first);
        }
    }

    // 如果所有分数都很低，至少保留前几个
    if (selectedIndices.empty() && !candidates.empty()) {
        for (size_t i = 0; i < std::min<size_t>(3, candidates.size()); ++i) {
            selectedIndices.push_back(candidates[i].first);
        }
    }

    // 按原始顺序排序索引
    std::sort(selectedIndices.begin(), selectedIndices.end());

    return selectedIndices;
}

size_t ContextRefiner::estimateChars(const std::string& text) const {
    return text.size();
}

size_t ContextRefiner::estimateTokens(const std::string& text) const {
    // 简单估算：中文字符按2个token计算，其他字符按0.25个token计算
    size_t tokens = 0;
    for (char c : text) {
        if ((c & 0x80) != 0) {
            // 可能是中文字符（UTF-8）
            tokens += 2;
        } else if (std::isspace(c)) {
            tokens += 1;
        } else {
            tokens += 1;
        }
    }
    return tokens / 4; // 粗略估算
}

} // namespace naw::desktop_pet::service

