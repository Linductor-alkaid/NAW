#pragma once

#include "naw/desktop_pet/service/ConfigManager.h"
#include "naw/desktop_pet/service/ErrorTypes.h"
#include "naw/desktop_pet/service/ToolManager.h"
#include "naw/desktop_pet/service/types/ChatMessage.h"
#include "naw/desktop_pet/service/types/RequestResponse.h"
#include "naw/desktop_pet/service/types/TaskType.h"
#include "naw/desktop_pet/service/utils/TokenCounter.h"

// 前向声明
namespace naw::desktop_pet::service {
    class APIClient;
}

#include <chrono>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace naw::desktop_pet::service {

// 前向声明（Agent状态结构，需要与Agent模块对接）
struct AgentState {
    std::string currentState;  // Agent当前状态（情绪、目标等）
    std::optional<std::string> memorySummary;  // Agent记忆摘要（可选）
};

// 项目上下文结构
struct ProjectContext {
    std::string projectRoot;      // 项目根路径
    std::string structureSummary; // 项目结构摘要
    std::vector<std::string> relevantFiles;  // 相关文件列表
};

// 代码上下文结构
struct CodeContext {
    std::vector<std::string> filePaths;  // 相关文件路径列表
    std::optional<std::string> fileContent;  // 文件内容（可选）
    std::optional<std::string> focusArea;    // 焦点区域（函数、类等）
};

// 记忆事件结构
struct MemoryEvent {
    std::string eventType;      // 事件类型
    std::string content;         // 事件内容
    std::chrono::system_clock::time_point timestamp;  // 时间戳
    float importanceScore{0.5f};  // 重要性评分（0-1）
};

// 上下文配置结构
struct ContextConfig {
    types::TaskType taskType{types::TaskType::CasualChat};
    size_t maxTokens{4096};  // 最大Token数
    bool includeConversationHistory{true};  // 是否包含对话历史
    bool includeAgentState{false};          // 是否包含Agent状态
    bool includeProjectContext{false};      // 是否包含项目上下文
    bool includeCodeContext{false};         // 是否包含代码上下文
    bool includeMemoryEvents{false};        // 是否包含记忆事件
    size_t maxHistoryMessages{50};          // 最大历史消息数
    std::optional<std::string> projectPath;  // 项目路径（可选，如果为空则自动检测）
};

/**
 * @brief 对话历史存储结构
 */
class ConversationHistory {
public:
    ConversationHistory() = default;
    ~ConversationHistory() = default;

    // 禁止拷贝/移动（因为包含mutex）
    ConversationHistory(const ConversationHistory&) = delete;
    ConversationHistory& operator=(const ConversationHistory&) = delete;
    ConversationHistory(ConversationHistory&&) = delete;
    ConversationHistory& operator=(ConversationHistory&&) = delete;

    void addMessage(const types::ChatMessage& message);
    std::vector<types::ChatMessage> getHistory(size_t maxMessages) const;
    std::vector<types::ChatMessage> getHistoryByRange(size_t start, size_t count) const;
    void trimHistory(size_t maxMessages);
    void trimHistoryByTokens(size_t maxTokens, const utils::TokenEstimator& estimator, const std::string& modelId);
    size_t size() const { return m_messages.size(); }
    bool empty() const { return m_messages.empty(); }
    void clear() { m_messages.clear(); }

private:
    mutable std::mutex m_mutex;
    std::deque<types::ChatMessage> m_messages;
};

/**
 * @brief 上下文管理器：根据任务类型动态构建和管理上下文
 */
class ContextManager {
public:
    explicit ContextManager(ConfigManager& configManager, APIClient* apiClient = nullptr);
    ~ContextManager();

    // 禁止拷贝/移动
    ContextManager(const ContextManager&) = delete;
    ContextManager& operator=(const ContextManager&) = delete;
    ContextManager(ContextManager&&) = delete;
    ContextManager& operator=(ContextManager&&) = delete;

    // ========== 对话历史管理 ==========
    /**
     * @brief 添加消息到历史
     * @param message 消息
     * @param sessionId 会话ID（可选，支持多会话）
     */
    void addMessage(const types::ChatMessage& message, const std::string& sessionId = "default");

    /**
     * @brief 获取历史消息
     * @param maxMessages 最大消息数
     * @param sessionId 会话ID（可选）
     * @return 消息列表
     */
    std::vector<types::ChatMessage> getHistory(size_t maxMessages = 50, const std::string& sessionId = "default") const;

    /**
     * @brief 获取指定范围的历史消息
     * @param start 起始索引
     * @param count 消息数量
     * @param sessionId 会话ID（可选）
     * @return 消息列表
     */
    std::vector<types::ChatMessage> getHistoryByRange(
        size_t start,
        size_t count,
        const std::string& sessionId = "default"
    ) const;

    /**
     * @brief 裁剪历史消息（按消息数）
     * @param maxMessages 最大消息数
     * @param sessionId 会话ID（可选）
     */
    void trimHistory(size_t maxMessages, const std::string& sessionId = "default");

    /**
     * @brief 裁剪历史消息（按Token数）
     * @param maxTokens 最大Token数
     * @param modelId 模型ID（用于Token估算）
     * @param sessionId 会话ID（可选）
     */
    void trimHistoryByTokens(size_t maxTokens, const std::string& modelId, const std::string& sessionId = "default");

    // ========== 上下文构建器 ==========
    /**
     * @brief 构建System Prompt
     * @param taskType 任务类型
     * @return System Prompt消息
     */
    types::ChatMessage buildSystemPrompt(types::TaskType taskType) const;

    /**
     * @brief 构建Agent状态上下文
     * @param agentState Agent状态
     * @return Agent状态消息
     */
    types::ChatMessage buildAgentStateContext(const AgentState& agentState) const;

    /**
     * @brief 构建项目上下文
     * @param projectContext 项目上下文
     * @param taskType 任务类型
     * @return 项目上下文消息
     */
    types::ChatMessage buildProjectContext(const ProjectContext& projectContext, types::TaskType taskType) const;

    /**
     * @brief 构建代码上下文
     * @param codeContext 代码上下文
     * @return 代码上下文消息
     */
    types::ChatMessage buildCodeContext(const CodeContext& codeContext) const;

    /**
     * @brief 构建记忆事件上下文
     * @param events 记忆事件列表
     * @param taskType 任务类型
     * @return 记忆事件消息
     */
    types::ChatMessage buildMemoryContext(const std::vector<MemoryEvent>& events, types::TaskType taskType) const;

    /**
     * @brief 构建完整上下文
     * @param config 上下文配置
     * @param userMessage 用户消息
     * @param modelId 模型ID（用于Token估算）
     * @param sessionId 会话ID（可选）
     * @return 完整的消息列表
     */
    std::vector<types::ChatMessage> buildContext(
        const ContextConfig& config,
        const std::string& userMessage,
        const std::string& modelId,
        const std::string& sessionId = "default"
    );

    // ========== 上下文窗口管理 ==========
    /**
     * @brief 估算消息列表的Token数
     * @param messages 消息列表
     * @param modelId 模型ID
     * @return Token数
     */
    size_t estimateTokens(const std::vector<types::ChatMessage>& messages, const std::string& modelId) const;

    /**
     * @brief 检查Token限制
     * @param messages 消息列表
     * @param maxTokens 最大Token数
     * @param modelId 模型ID
     * @return 是否超过限制
     */
    bool checkTokenLimit(
        const std::vector<types::ChatMessage>& messages,
        size_t maxTokens,
        const std::string& modelId
    ) const;

    /**
     * @brief 智能裁剪上下文
     * @param messages 消息列表（会被修改）
     * @param maxTokens 最大Token数
     * @param modelId 模型ID
     * @param taskType 任务类型
     */
    void trimContext(
        std::vector<types::ChatMessage>& messages,
        size_t maxTokens,
        const std::string& modelId,
        types::TaskType taskType
    ) const;

    /**
     * @brief 计算消息重要性评分
     * @param message 消息
     * @param taskType 任务类型
     * @param messageIndex 消息索引（用于时间权重）
     * @param totalMessages 总消息数
     * @return 重要性评分（0-1）
     */
    float calculateMessageImportance(
        const types::ChatMessage& message,
        types::TaskType taskType,
        size_t messageIndex,
        size_t totalMessages
    ) const;

    // ========== 上下文配置管理 ==========
    /**
     * @brief 从配置文件加载上下文配置
     * @param err 错误信息输出（可选）
     * @return 是否加载成功
     */
    bool loadConfigFromFile(ErrorInfo* err = nullptr);

    /**
     * @brief 更新配置
     * @param config 新配置值（部分更新）
     */
    void updateConfig(const ContextConfig& config);

    /**
     * @brief 获取当前配置
     * @return 当前配置
     */
    ContextConfig getConfig() const { return m_config; }

    // ========== 工具与LLM集成 ==========

    /**
     * @brief 设置工具管理器（用于Function Calling）
     * @param toolManager 工具管理器指针（可以为 nullptr，表示不使用工具）
     */
    void setToolManager(ToolManager* toolManager);

    /**
     * @brief 将工具列表填充到ChatRequest
     * 
     * 如果设置了工具管理器，从ToolManager获取工具列表并填充到ChatRequest。
     * 这是一个便捷方法，内部调用ToolManager::populateToolsToRequest()。
     * 
     * @param request 要填充的ChatRequest对象（会被修改）
     * @param filter 工具过滤条件（可选，默认不过滤）
     * @param toolChoice 工具选择策略："auto"（默认）、"none"、特定工具名
     * @param error 如果填充失败，输出错误信息（可选）
     * @return 如果成功返回 true，否则返回 false（如果未设置工具管理器，返回 false）
     */
    bool populateToolsToRequest(
        types::ChatRequest& request,
        const ToolFilter& filter = {},
        const std::string& toolChoice = "auto",
        ErrorInfo* error = nullptr
    );

private:
    ConfigManager& m_configManager;
    mutable std::mutex m_mutex;

    // 对话历史：sessionId -> ConversationHistory
    std::unordered_map<std::string, ConversationHistory> m_conversations;

    // 上下文配置
    ContextConfig m_config;

    // Token估算器
    utils::TokenEstimator m_tokenEstimator;

    // 工具管理器（可选，用于Function Calling）
    ToolManager* m_toolManager{nullptr};

    // 内部方法
    /**
     * @brief 获取或创建会话历史
     */
    ConversationHistory& getOrCreateSession(const std::string& sessionId);

    /**
     * @brief 获取会话历史（const版本）
     */
    const ConversationHistory* getSession(const std::string& sessionId) const;

    /**
     * @brief 根据任务类型获取System Prompt模板
     */
    std::string getSystemPromptTemplate(types::TaskType taskType) const;
};

} // namespace naw::desktop_pet::service

