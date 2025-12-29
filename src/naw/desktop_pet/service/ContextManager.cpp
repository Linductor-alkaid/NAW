#include "naw/desktop_pet/service/ContextManager.h"

#include "naw/desktop_pet/service/ErrorHandler.h"
#include "naw/desktop_pet/service/types/TaskType.h"

#include <algorithm>
#include <sstream>

namespace naw::desktop_pet::service {

// ========== ConversationHistory 实现 ==========

void ConversationHistory::addMessage(const types::ChatMessage& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_messages.push_back(message);
}

std::vector<types::ChatMessage> ConversationHistory::getHistory(size_t maxMessages) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    size_t count = std::min(maxMessages, m_messages.size());
    if (count == 0) {
        return {};
    }

    std::vector<types::ChatMessage> result;
    result.reserve(count);

    // 从后往前取最近的N条消息
    auto startIt = m_messages.end() - count;
    for (auto it = startIt; it != m_messages.end(); ++it) {
        result.push_back(*it);
    }

    return result;
}

std::vector<types::ChatMessage> ConversationHistory::getHistoryByRange(size_t start, size_t count) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (start >= m_messages.size()) {
        return {};
    }

    size_t end = std::min(start + count, m_messages.size());
    std::vector<types::ChatMessage> result;
    result.reserve(end - start);

    auto startIt = m_messages.begin() + start;
    auto endIt = m_messages.begin() + end;
    for (auto it = startIt; it != endIt; ++it) {
        result.push_back(*it);
    }

    return result;
}

void ConversationHistory::trimHistory(size_t maxMessages) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_messages.size() <= maxMessages) {
        return;
    }

    // 保留最近的N条消息
    size_t removeCount = m_messages.size() - maxMessages;
    m_messages.erase(m_messages.begin(), m_messages.begin() + removeCount);
}

void ConversationHistory::trimHistoryByTokens(
    size_t maxTokens,
    const utils::TokenEstimator& /* estimator */,
    const std::string& modelId
) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_messages.empty()) {
        return;
    }

    // 从后往前计算Token数，保留不超过限制的消息
    size_t totalTokens = 0;
    size_t keepFrom = m_messages.size();

    for (size_t i = m_messages.size(); i > 0; --i) {
        size_t idx = i - 1;
        size_t msgTokens = m_messages[idx].estimateTokens(modelId);
        if (totalTokens + msgTokens > maxTokens) {
            break;
        }
        totalTokens += msgTokens;
        keepFrom = idx;
    }

    // 删除旧消息
    if (keepFrom > 0) {
        m_messages.erase(m_messages.begin(), m_messages.begin() + keepFrom);
    }
}

// ========== ContextManager 实现 ==========

ContextManager::ContextManager(ConfigManager& configManager)
    : m_configManager(configManager) {
    // 加载默认配置
    loadConfigFromFile();
}

void ContextManager::addMessage(const types::ChatMessage& message, const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    getOrCreateSession(sessionId).addMessage(message);
}

std::vector<types::ChatMessage> ContextManager::getHistory(size_t maxMessages, const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto* session = getSession(sessionId);
    if (!session) {
        return {};
    }

    return session->getHistory(maxMessages);
}

std::vector<types::ChatMessage> ContextManager::getHistoryByRange(
    size_t start,
    size_t count,
    const std::string& sessionId
) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto* session = getSession(sessionId);
    if (!session) {
        return {};
    }

    return session->getHistoryByRange(start, count);
}

void ContextManager::trimHistory(size_t maxMessages, const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    getOrCreateSession(sessionId).trimHistory(maxMessages);
}

void ContextManager::trimHistoryByTokens(
    size_t maxTokens,
    const std::string& modelId,
    const std::string& sessionId
) {
    std::lock_guard<std::mutex> lock(m_mutex);
    getOrCreateSession(sessionId).trimHistoryByTokens(maxTokens, m_tokenEstimator, modelId);
}

types::ChatMessage ContextManager::buildSystemPrompt(types::TaskType taskType) const {
    types::ChatMessage msg;
    msg.role = types::MessageRole::System;
    msg.setText(getSystemPromptTemplate(taskType));
    return msg;
}

types::ChatMessage ContextManager::buildAgentStateContext(const AgentState& agentState) const {
    types::ChatMessage msg;
    msg.role = types::MessageRole::System;

    std::ostringstream oss;
    oss << "Agent State:\n";
    oss << "- Current State: " << agentState.currentState << "\n";
    if (agentState.memorySummary.has_value()) {
        oss << "- Memory Summary: " << *agentState.memorySummary << "\n";
    }

    msg.setText(oss.str());
    return msg;
}

types::ChatMessage ContextManager::buildProjectContext(
    const ProjectContext& projectContext,
    types::TaskType taskType
) const {
    (void)taskType; // 保留参数以备将来使用
    types::ChatMessage msg;
    msg.role = types::MessageRole::System;

    std::ostringstream oss;
    oss << "Project Context:\n";
    oss << "- Project Root: " << projectContext.projectRoot << "\n";
    oss << "- Structure Summary: " << projectContext.structureSummary << "\n";
    if (!projectContext.relevantFiles.empty()) {
        oss << "- Relevant Files:\n";
        for (const auto& file : projectContext.relevantFiles) {
            oss << "  - " << file << "\n";
        }
    }

    msg.setText(oss.str());
    return msg;
}

types::ChatMessage ContextManager::buildCodeContext(const CodeContext& codeContext) const {
    types::ChatMessage msg;
    msg.role = types::MessageRole::User;

    std::ostringstream oss;
    oss << "Code Context:\n";
    if (!codeContext.filePaths.empty()) {
        oss << "- Files:\n";
        for (const auto& path : codeContext.filePaths) {
            oss << "  - " << path << "\n";
        }
    }
    if (codeContext.fileContent.has_value()) {
        oss << "\nFile Content:\n";
        oss << *codeContext.fileContent;
    }
    if (codeContext.focusArea.has_value()) {
        oss << "\nFocus Area: " << *codeContext.focusArea << "\n";
    }

    msg.setText(oss.str());
    return msg;
}

types::ChatMessage ContextManager::buildMemoryContext(
    const std::vector<MemoryEvent>& events,
    types::TaskType /* taskType */
) const {
    types::ChatMessage msg;
    msg.role = types::MessageRole::System;

    std::ostringstream oss;
    oss << "Memory Events:\n";
    for (const auto& event : events) {
        oss << "- [" << event.eventType << "] " << event.content;
        oss << " (importance: " << event.importanceScore << ")\n";
    }

    msg.setText(oss.str());
    return msg;
}

std::vector<types::ChatMessage> ContextManager::buildContext(
    const ContextConfig& config,
    const std::string& userMessage,
    const std::string& modelId,
    const std::string& sessionId
) {
    std::vector<types::ChatMessage> messages;

    // 1. System Prompt（始终包含）
    messages.push_back(buildSystemPrompt(config.taskType));

    // 2. Agent状态（如果启用）
    if (config.includeAgentState) {
        // 这里需要从外部获取AgentState，暂时跳过
        // AgentState agentState = getAgentState();
        // messages.push_back(buildAgentStateContext(agentState));
    }

    // 3. 项目上下文（如果启用）
    if (config.includeProjectContext) {
        // 这里需要从外部获取ProjectContext，暂时跳过
        // ProjectContext projectContext = getProjectContext();
        // messages.push_back(buildProjectContext(projectContext, config.taskType));
    }

    // 4. 代码上下文（如果启用）
    if (config.includeCodeContext) {
        // 这里需要从外部获取CodeContext，暂时跳过
        // CodeContext codeContext = getCodeContext();
        // messages.push_back(buildCodeContext(codeContext));
    }

    // 5. 记忆事件（如果启用）
    if (config.includeMemoryEvents) {
        // 这里需要从外部获取MemoryEvents，暂时跳过
        // std::vector<MemoryEvent> events = getMemoryEvents();
        // messages.push_back(buildMemoryContext(events, config.taskType));
    }

    // 6. 对话历史（如果启用）
    if (config.includeConversationHistory) {
        auto history = getHistory(config.maxHistoryMessages, sessionId);
        messages.insert(messages.end(), history.begin(), history.end());
    }

    // 7. 用户消息
    types::ChatMessage userMsg;
    userMsg.role = types::MessageRole::User;
    userMsg.setText(userMessage);
    messages.push_back(userMsg);

    // 8. 检查Token限制并裁剪
    size_t totalTokens = estimateTokens(messages, modelId);
    if (totalTokens > config.maxTokens) {
        trimContext(messages, config.maxTokens, modelId, config.taskType);
    }

    return messages;
}

size_t ContextManager::estimateTokens(
    const std::vector<types::ChatMessage>& messages,
    const std::string& modelId
) const {
    size_t total = 0;
    for (const auto& msg : messages) {
        total += msg.estimateTokens(modelId);
    }
    return total;
}

bool ContextManager::checkTokenLimit(
    const std::vector<types::ChatMessage>& messages,
    size_t maxTokens,
    const std::string& modelId
) const {
    return estimateTokens(messages, modelId) > maxTokens;
}

void ContextManager::trimContext(
    std::vector<types::ChatMessage>& messages,
    size_t maxTokens,
    const std::string& modelId,
    types::TaskType taskType
) const {
    if (messages.empty()) {
        return;
    }

    // 计算每个消息的重要性评分
    std::vector<std::pair<size_t, float>> messageScores;
    for (size_t i = 0; i < messages.size(); ++i) {
        float importance = calculateMessageImportance(messages[i], taskType, i, messages.size());
        messageScores.push_back({i, importance});
    }

    // 按重要性排序（降序）
    std::sort(messageScores.begin(), messageScores.end(),
              [](const auto& a, const auto& b) {
                  return a.second > b.second;
              });

    // 选择要保留的消息（优先保留重要消息）
    std::vector<bool> keep(messages.size(), false);
    size_t totalTokens = 0;

    // System prompt始终保留
    for (size_t i = 0; i < messages.size(); ++i) {
        if (messages[i].role == types::MessageRole::System) {
            keep[i] = true;
            totalTokens += messages[i].estimateTokens(modelId);
        }
    }

    // 按重要性顺序添加消息，直到达到Token限制
    for (const auto& [idx, score] : messageScores) {
        if (keep[idx]) {
            continue;  // 已保留
        }

        size_t msgTokens = messages[idx].estimateTokens(modelId);
        if (totalTokens + msgTokens > maxTokens) {
            break;  // 超过限制，停止添加
        }

        keep[idx] = true;
        totalTokens += msgTokens;
    }

    // 构建新的消息列表（保留标记为keep的消息）
    std::vector<types::ChatMessage> newMessages;
    for (size_t i = 0; i < messages.size(); ++i) {
        if (keep[i]) {
            newMessages.push_back(messages[i]);
        }
    }

    messages = std::move(newMessages);
}

float ContextManager::calculateMessageImportance(
    const types::ChatMessage& message,
    types::TaskType taskType,
    size_t messageIndex,
    size_t totalMessages
) const {
    float importance = 0.0f;

    // 1. 消息角色（50%）：System消息有更高的基础重要性
    switch (message.role) {
        case types::MessageRole::System:
            importance += 0.5f;  // System消息基础重要性更高
            break;
        case types::MessageRole::User:
            importance += 0.3f;
            break;
        case types::MessageRole::Assistant:
            importance += 0.2f;
            break;
        case types::MessageRole::Tool:
            importance += 0.1f;
            break;
    }

    // 2. 消息时间（30%）：越新越重要（但System消息不受时间因子影响太大）
    if (totalMessages > 0 && message.role != types::MessageRole::System) {
        // 计算从末尾的距离：索引越大（越新），距离越小，重要性越高
        float distanceFromEnd = static_cast<float>(totalMessages - messageIndex - 1) / static_cast<float>(totalMessages);
        importance += 0.3f * (1.0f - distanceFromEnd);  // 距离越小（越新）权重越高
    } else if (message.role == types::MessageRole::System) {
        // System消息的时间因子较小，确保其重要性始终高于User消息
        importance += 0.1f;
    }

    // 3. 任务类型相关性（20%）
    if (types::isCodeRelatedTask(taskType)) {
        // 代码相关任务：包含代码的消息更重要
        auto textView = message.textView();
        if (textView.has_value()) {
            std::string text = std::string(*textView);
            // 简单判断：包含代码关键词
            if (text.find("class ") != std::string::npos ||
                text.find("function ") != std::string::npos ||
                text.find("def ") != std::string::npos ||
                text.find("void ") != std::string::npos ||
                text.find("#include") != std::string::npos) {
                importance += 0.2f;
            }
        }
    } else {
        // 非代码任务：所有消息同等重要
        importance += 0.1f;
    }

    // 4. 消息长度（10%）：过短的消息可能不重要
    auto textView = message.textView();
    if (textView.has_value()) {
        size_t length = textView->size();
        if (length > 100) {
            importance += 0.1f;
        } else if (length > 50) {
            importance += 0.05f;
        }
    }

    return std::min(1.0f, std::max(0.0f, importance));
}

bool ContextManager::loadConfigFromFile(ErrorInfo* /* err */) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 读取配置
    auto maxHistory = m_configManager.get("context.max_history_messages");
    if (maxHistory.has_value()) {
        if (maxHistory->is_number_unsigned()) {
            m_config.maxHistoryMessages = maxHistory->get<size_t>();
        } else if (maxHistory->is_number_integer()) {
            m_config.maxHistoryMessages = static_cast<size_t>(maxHistory->get<int64_t>());
        }
    }

    auto maxTokens = m_configManager.get("context.max_context_tokens");
    if (maxTokens.has_value()) {
        if (maxTokens->is_number_unsigned()) {
            m_config.maxTokens = maxTokens->get<size_t>();
        } else if (maxTokens->is_number_integer()) {
            int64_t value = maxTokens->get<int64_t>();
            if (value > 0) {
                m_config.maxTokens = static_cast<size_t>(value);
            }
        }
    }

    auto includeAgentState = m_configManager.get("context.default_include_agent_state");
    if (includeAgentState.has_value() && includeAgentState->is_boolean()) {
        m_config.includeAgentState = includeAgentState->get<bool>();
    }

    auto includeProjectContext = m_configManager.get("context.default_include_project_context");
    if (includeProjectContext.has_value() && includeProjectContext->is_boolean()) {
        m_config.includeProjectContext = includeProjectContext->get<bool>();
    }

    return true;
}

void ContextManager::updateConfig(const ContextConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
}

ConversationHistory& ContextManager::getOrCreateSession(const std::string& sessionId) {
    auto it = m_conversations.find(sessionId);
    if (it == m_conversations.end()) {
        it = m_conversations.try_emplace(sessionId).first;
    }
    return it->second;
}

const ConversationHistory* ContextManager::getSession(const std::string& sessionId) const {
    auto it = m_conversations.find(sessionId);
    if (it == m_conversations.end()) {
        return nullptr;
    }
    return &it->second;
}

std::string ContextManager::getSystemPromptTemplate(types::TaskType taskType) const {
    // 根据任务类型返回不同的System Prompt模板
    switch (taskType) {
        case types::TaskType::CodeGeneration:
            return "You are a helpful code generation assistant. Generate high-quality, well-structured code following best practices and coding standards.";
        case types::TaskType::CodeAnalysis:
            return "You are a code analysis expert. Provide detailed analysis of code, including structure, patterns, potential issues, and improvements.";
        case types::TaskType::CodeReview:
            return "You are a code reviewer. Review code carefully and provide constructive feedback on code quality, performance, security, and maintainability.";
        case types::TaskType::CodeExplanation:
            return "You are a code explanation assistant. Explain code clearly and comprehensively, helping users understand how it works.";
        case types::TaskType::BugFix:
            return "You are a bug fixing expert. Analyze bugs carefully and provide accurate fixes with explanations.";
        case types::TaskType::ProjectAnalysis:
            return "You are a project analysis expert. Analyze project structure, architecture, and provide insights.";
        case types::TaskType::ArchitectureDesign:
            return "You are an architecture design expert. Help design scalable, maintainable software architectures.";
        case types::TaskType::Documentation:
            return "You are a documentation expert. Generate clear, comprehensive documentation for code and projects.";
        case types::TaskType::TechnicalQnA:
            return "You are a technical Q&A assistant. Answer technical questions accurately and comprehensively.";
        case types::TaskType::CodeDiscussion:
            return "You are a code discussion assistant. Engage in meaningful discussions about code, design, and implementation.";
        default:
            return "You are a helpful AI assistant. Provide accurate, helpful, and friendly responses.";
    }
}

} // namespace naw::desktop_pet::service

