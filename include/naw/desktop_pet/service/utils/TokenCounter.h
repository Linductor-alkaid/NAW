#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace naw::desktop_pet::service::utils {

enum class TokenEstimateStrategy {
    ApproxChar, // 现有字符估算
    BPE,        // 精确BPE估算
};

struct TokenModelRule {
    double tokensPerChar = 0.25;   // 约4字符1 token 的默认估算
    std::size_t fixedOverhead = 4; // 模型固定开销（prompt/resp均适用）
    TokenEstimateStrategy strategy = TokenEstimateStrategy::ApproxChar;
    std::string bpeEncoding;       // 可选：BPE编码表名称/ID
};

/**
 * @brief 轻量BPE编码表（简化版，用于本地精确估算）
 *
 * 说明：
 *  - tokens: 字节对编码的 token -> rank 映射
 *  - encoderName: 对应的模型/编码器标识（如 cl100k_base）
 *  - 为避免引入外部依赖，使用最小必要表，必要时可通过 setEncoding 覆盖。
 */
class SimpleBPE {
public:
    SimpleBPE() = default;
    explicit SimpleBPE(std::string name,
                       std::unordered_map<std::string, int> ranks)
        : encoderName_(std::move(name)), ranks_(std::move(ranks)) {}

    const std::string& name() const { return encoderName_; }

    /**
     * @brief 估算文本的token数（基于已加载的ranks；若空则返回字符估算）
     */
    std::size_t countTokens(std::string_view text, std::size_t fallbackFixed = 0) const;

    void setRanks(std::string name, std::unordered_map<std::string, int> ranks) {
        encoderName_ = std::move(name);
        ranks_ = std::move(ranks);
    }

    bool empty() const { return ranks_.empty(); }

private:
    std::string encoderName_;
    std::unordered_map<std::string, int> ranks_;
};

class TokenEstimator {
public:
    TokenEstimator();
    explicit TokenEstimator(std::unordered_map<std::string, TokenModelRule> rules);

    /**
     * @brief 根据模型名估算文本token数（轻量估算，不调用外部服务）
     * @param model 模型标识（大小写不敏感）
     * @param text  文本内容
     */
    std::size_t estimateTokens(const std::string& model, std::string_view text) const;

    /**
     * @brief 使用BPE策略估算（若模型规则未启用BPE则回退字符估算）
     */
    std::size_t estimateTokensBPE(const std::string& model, std::string_view text) const;

    /**
     * @brief 覆盖/新增模型规则
     */
    void setModelRule(std::string model, TokenModelRule rule);

    /**
     * @brief 查询当前模型规则（若不存在返回默认规则）
     */
    TokenModelRule getModelRule(const std::string& model) const;

    /**
     * @brief 设置全局/默认BPE编码表（用于未指定bpeEncoding的模型）
     */
    void setDefaultBPE(SimpleBPE bpe);

    /**
     * @brief 为特定编码器名称设置BPE词表
     */
    void setNamedBPE(std::string encodingName, SimpleBPE bpe);

private:
    TokenModelRule defaultRule_{};
    std::unordered_map<std::string, TokenModelRule> modelRules_;
    SimpleBPE defaultBPE_;
    std::unordered_map<std::string, SimpleBPE> bpeByEncoding_;

    static std::string normalizeModel(const std::string& model);
};

struct TokenUsage {
    std::uint64_t promptTokens{0};
    std::uint64_t completionTokens{0};
    std::uint64_t totalTokens{0};
    std::uint64_t calls{0};
};

class TokenCounter {
public:
    TokenCounter() = default;

    /**
     * @brief 按已知token数记录一次调用
     */
    void record(const std::string& model,
                std::size_t promptTokens,
                std::size_t completionTokens);

    /**
     * @brief 使用估算器按文本记录一次调用
     */
    void recordText(const std::string& model,
                    std::string_view prompt,
                    std::string_view completion,
                    const TokenEstimator& estimator);

    /**
     * @brief 获取整体用量快照
     */
    TokenUsage totalUsage() const;

    /**
     * @brief 获取分模型用量快照
     */
    std::map<std::string, TokenUsage> modelUsage() const;

    /**
     * @brief 清空所有计数
     */
    void reset();

private:
    mutable std::mutex mutex_;
    TokenUsage total_;
    std::map<std::string, TokenUsage> perModel_;
};

} // namespace naw::desktop_pet::service::utils

