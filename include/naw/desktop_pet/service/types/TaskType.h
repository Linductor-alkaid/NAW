#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace naw::desktop_pet::service::types {

// Task types (aligned with docs/design/服务层设计方案.md)
enum class TaskType {
    // 对话类任务
    CasualChat,
    CodeDiscussion,
    TechnicalQnA,

    // 代码相关任务
    CodeGeneration,
    CodeAnalysis,
    CodeReview,
    CodeExplanation,
    BugFix,

    // 项目理解任务
    ProjectAnalysis,
    ArchitectureDesign,
    Documentation,

    // Agent相关任务
    AgentDecision,
    AgentReasoning,
    ContextUnderstanding,

    // 语音视觉相关任务
    SpeechRecognition,
    SpeechSynthesis,
    VisionUnderstanding,
    SceneAnalysis,
    ProactiveResponse,

    // 工具调用相关任务
    ToolCalling,
    CodeToolExecution
};

// Enum to string (PascalCase as in design doc)
inline std::string taskTypeToString(TaskType v) {
    switch (v) {
        case TaskType::CasualChat: return "CasualChat";
        case TaskType::CodeDiscussion: return "CodeDiscussion";
        case TaskType::TechnicalQnA: return "TechnicalQnA";
        case TaskType::CodeGeneration: return "CodeGeneration";
        case TaskType::CodeAnalysis: return "CodeAnalysis";
        case TaskType::CodeReview: return "CodeReview";
        case TaskType::CodeExplanation: return "CodeExplanation";
        case TaskType::BugFix: return "BugFix";
        case TaskType::ProjectAnalysis: return "ProjectAnalysis";
        case TaskType::ArchitectureDesign: return "ArchitectureDesign";
        case TaskType::Documentation: return "Documentation";
        case TaskType::AgentDecision: return "AgentDecision";
        case TaskType::AgentReasoning: return "AgentReasoning";
        case TaskType::ContextUnderstanding: return "ContextUnderstanding";
        case TaskType::SpeechRecognition: return "SpeechRecognition";
        case TaskType::SpeechSynthesis: return "SpeechSynthesis";
        case TaskType::VisionUnderstanding: return "VisionUnderstanding";
        case TaskType::SceneAnalysis: return "SceneAnalysis";
        case TaskType::ProactiveResponse: return "ProactiveResponse";
        case TaskType::ToolCalling: return "ToolCalling";
        case TaskType::CodeToolExecution: return "CodeToolExecution";
    }
    return "Unknown";
}

namespace task_type_detail {
inline char asciiLower(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
    return c;
}
inline bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (asciiLower(a[i]) != asciiLower(b[i])) return false;
    }
    return true;
}
} // namespace task_type_detail

// String to enum (case-insensitive). Expected input like "CodeAnalysis".
inline std::optional<TaskType> stringToTaskType(std::string_view s) {
    using task_type_detail::iequals;
    if (iequals(s, "CasualChat")) return TaskType::CasualChat;
    if (iequals(s, "CodeDiscussion")) return TaskType::CodeDiscussion;
    if (iequals(s, "TechnicalQnA")) return TaskType::TechnicalQnA;
    if (iequals(s, "CodeGeneration")) return TaskType::CodeGeneration;
    if (iequals(s, "CodeAnalysis")) return TaskType::CodeAnalysis;
    if (iequals(s, "CodeReview")) return TaskType::CodeReview;
    if (iequals(s, "CodeExplanation")) return TaskType::CodeExplanation;
    if (iequals(s, "BugFix")) return TaskType::BugFix;
    if (iequals(s, "ProjectAnalysis")) return TaskType::ProjectAnalysis;
    if (iequals(s, "ArchitectureDesign")) return TaskType::ArchitectureDesign;
    if (iequals(s, "Documentation")) return TaskType::Documentation;
    if (iequals(s, "AgentDecision")) return TaskType::AgentDecision;
    if (iequals(s, "AgentReasoning")) return TaskType::AgentReasoning;
    if (iequals(s, "ContextUnderstanding")) return TaskType::ContextUnderstanding;
    if (iequals(s, "SpeechRecognition")) return TaskType::SpeechRecognition;
    if (iequals(s, "SpeechSynthesis")) return TaskType::SpeechSynthesis;
    if (iequals(s, "VisionUnderstanding")) return TaskType::VisionUnderstanding;
    if (iequals(s, "SceneAnalysis")) return TaskType::SceneAnalysis;
    if (iequals(s, "ProactiveResponse")) return TaskType::ProactiveResponse;
    if (iequals(s, "ToolCalling")) return TaskType::ToolCalling;
    if (iequals(s, "CodeToolExecution")) return TaskType::CodeToolExecution;
    return std::nullopt;
}

inline std::string_view getTaskTypeDescription(TaskType v) {
    switch (v) {
        case TaskType::CasualChat: return "Casual chat";
        case TaskType::CodeDiscussion: return "Code discussion";
        case TaskType::TechnicalQnA: return "Technical QnA";
        case TaskType::CodeGeneration: return "Code generation";
        case TaskType::CodeAnalysis: return "Code analysis";
        case TaskType::CodeReview: return "Code review";
        case TaskType::CodeExplanation: return "Code explanation";
        case TaskType::BugFix: return "Bug fix";
        case TaskType::ProjectAnalysis: return "Project analysis";
        case TaskType::ArchitectureDesign: return "Architecture design";
        case TaskType::Documentation: return "Documentation";
        case TaskType::AgentDecision: return "Agent decision";
        case TaskType::AgentReasoning: return "Agent reasoning";
        case TaskType::ContextUnderstanding: return "Context understanding";
        case TaskType::SpeechRecognition: return "Speech recognition";
        case TaskType::SpeechSynthesis: return "Speech synthesis";
        case TaskType::VisionUnderstanding: return "Vision understanding";
        case TaskType::SceneAnalysis: return "Scene analysis";
        case TaskType::ProactiveResponse: return "Proactive response";
        case TaskType::ToolCalling: return "Tool calling";
        case TaskType::CodeToolExecution: return "Code tool execution";
    }
    return "Unknown task";
}

inline bool isCodeRelatedTask(TaskType v) {
    switch (v) {
        case TaskType::CodeDiscussion:
        case TaskType::TechnicalQnA:
        case TaskType::CodeGeneration:
        case TaskType::CodeAnalysis:
        case TaskType::CodeReview:
        case TaskType::CodeExplanation:
        case TaskType::BugFix:
        case TaskType::ProjectAnalysis:
        case TaskType::ArchitectureDesign:
        case TaskType::CodeToolExecution:
            return true;
        default:
            return false;
    }
}

inline bool isMultimodalTask(TaskType v) {
    switch (v) {
        case TaskType::SpeechRecognition:
        case TaskType::SpeechSynthesis:
        case TaskType::VisionUnderstanding:
        case TaskType::SceneAnalysis:
        case TaskType::ProactiveResponse:
            return true;
        default:
            return false;
    }
}

} // namespace naw::desktop_pet::service::types

