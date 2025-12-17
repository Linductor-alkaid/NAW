#include "naw/desktop_pet/service/utils/TokenCounter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>
#include <sstream>

namespace naw::desktop_pet::service::utils {

namespace {
// 将模型名规范化为小写，便于匹配
std::string toLowerCopy(const std::string& in) {
    std::string out = in;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}
} // namespace

TokenEstimator::TokenEstimator() {
    // 预设模型规则（默认字符估算）
    modelRules_.emplace("gpt-3.5-turbo", TokenModelRule{0.25, 4, TokenEstimateStrategy::ApproxChar, "cl100k_base"});
    modelRules_.emplace("gpt-4", TokenModelRule{0.25, 4, TokenEstimateStrategy::ApproxChar, "cl100k_base"});
    modelRules_.emplace("gpt-4o", TokenModelRule{0.25, 4, TokenEstimateStrategy::ApproxChar, "o200k_base"});
    modelRules_.emplace("gpt-4o-mini", TokenModelRule{0.24, 4, TokenEstimateStrategy::ApproxChar, "o200k_base"});
    modelRules_.emplace("glm-4", TokenModelRule{0.25, 4, TokenEstimateStrategy::ApproxChar, "cl100k_base"});
    modelRules_.emplace("qwen-max", TokenModelRule{0.25, 4, TokenEstimateStrategy::ApproxChar, "cl100k_base"});
    modelRules_.emplace("qwen-plus", TokenModelRule{0.25, 4, TokenEstimateStrategy::ApproxChar, "cl100k_base"});
}

TokenEstimator::TokenEstimator(std::unordered_map<std::string, TokenModelRule> rules)
    : modelRules_(std::move(rules)) {}

std::size_t TokenEstimator::estimateTokens(const std::string& model, std::string_view text) const {
    const auto rule = getModelRule(model);
    if (text.empty()) {
        return rule.fixedOverhead;
    }
    const double estimated =
        static_cast<double>(text.size()) * rule.tokensPerChar + static_cast<double>(rule.fixedOverhead);
    // 向上取整以避免低估
    return static_cast<std::size_t>(std::ceil(estimated));
}

std::size_t TokenEstimator::estimateTokensBPE(const std::string& model, std::string_view text) const {
    const auto rule = getModelRule(model);
    if (rule.strategy != TokenEstimateStrategy::BPE) {
        return estimateTokens(model, text);
    }

    // 选择对应编码表
    SimpleBPE encoder = defaultBPE_;
    if (!rule.bpeEncoding.empty()) {
        const auto it = bpeByEncoding_.find(rule.bpeEncoding);
        if (it != bpeByEncoding_.end()) {
            encoder = it->second;
        }
    }
    if (encoder.empty()) {
        return estimateTokens(model, text);
    }
    const auto tokens = encoder.countTokens(text, rule.fixedOverhead);
    return tokens + rule.fixedOverhead;
}

void TokenEstimator::setModelRule(std::string model, TokenModelRule rule) {
    modelRules_[normalizeModel(model)] = rule;
}

TokenModelRule TokenEstimator::getModelRule(const std::string& model) const {
    const auto key = normalizeModel(model);
    const auto it = modelRules_.find(key);
    if (it != modelRules_.end()) {
        return it->second;
    }
    return defaultRule_;
}

void TokenEstimator::setDefaultBPE(SimpleBPE bpe) {
    defaultBPE_ = std::move(bpe);
}

void TokenEstimator::setNamedBPE(std::string encodingName, SimpleBPE bpe) {
    bpeByEncoding_[toLowerCopy(encodingName)] = std::move(bpe);
}

std::string TokenEstimator::normalizeModel(const std::string& model) {
    return toLowerCopy(model);
}

std::size_t SimpleBPE::countTokens(std::string_view text, std::size_t fallbackFixed) const {
    if (text.empty()) {
        return fallbackFixed;
    }
    if (ranks_.empty()) {
        // fallback to character-level estimation (approx 1 token per 4 chars)
        const double estimated =
            static_cast<double>(text.size()) * 0.25 + static_cast<double>(fallbackFixed);
        return static_cast<std::size_t>(std::ceil(estimated));
    }

    // 简化的BPE估算：按空白和标点切片，再以子串匹配 ranks
    // 这不是完整BPE分词，仅为轻量近似；可替换为真实分词器。
    std::size_t tokens = 0;
    std::string chunk;
    chunk.reserve(text.size());
    auto flush = [&]() {
        if (chunk.empty()) return;
        // 贪心匹配：尝试长度从大到小
        std::size_t i = 0;
        while (i < chunk.size()) {
            std::size_t matched = 0;
            // 最长子串限制为 8 以避免过长扫描
            const std::size_t maxLen = std::min<std::size_t>(8, chunk.size() - i);
            for (std::size_t len = maxLen; len >= 1; --len) {
                auto sub = chunk.substr(i, len);
                if (ranks_.find(sub) != ranks_.end()) {
                    matched = len;
                    break;
                }
                if (len == 1) break;
            }
            if (matched == 0) matched = 1; // 未命中则按单字节计1
            tokens += 1;
            i += matched;
        }
        chunk.clear();
    };

    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) || std::ispunct(static_cast<unsigned char>(c))) {
            flush();
            tokens += 1; // 将空白/标点视作单独token
        } else {
            chunk.push_back(c);
        }
    }
    flush();
    return tokens;
}

void TokenCounter::record(const std::string& model,
                          std::size_t promptTokens,
                          std::size_t completionTokens) {
    const std::size_t total = promptTokens + completionTokens;

    std::lock_guard<std::mutex> lock(mutex_);
    total_.promptTokens += promptTokens;
    total_.completionTokens += completionTokens;
    total_.totalTokens += total;
    total_.calls += 1;

    auto& usage = perModel_[model];
    usage.promptTokens += promptTokens;
    usage.completionTokens += completionTokens;
    usage.totalTokens += total;
    usage.calls += 1;
}

void TokenCounter::recordText(const std::string& model,
                              std::string_view prompt,
                              std::string_view completion,
                              const TokenEstimator& estimator) {
    const auto promptTokens = estimator.estimateTokens(model, prompt);
    const auto completionTokens = estimator.estimateTokens(model, completion);
    record(model, promptTokens, completionTokens);
}

TokenUsage TokenCounter::totalUsage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_;
}

std::map<std::string, TokenUsage> TokenCounter::modelUsage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return perModel_;
}

void TokenCounter::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    total_ = {};
    perModel_.clear();
}

} // namespace naw::desktop_pet::service::utils

