#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace naw::desktop_pet::service::types {

enum class TaskPriority {
    Critical,
    High,
    Normal,
    Low
};

inline std::string taskPriorityToString(TaskPriority v) {
    switch (v) {
        case TaskPriority::Critical: return "Critical";
        case TaskPriority::High: return "High";
        case TaskPriority::Normal: return "Normal";
        case TaskPriority::Low: return "Low";
    }
    return "Normal";
}

namespace task_priority_detail {
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
} // namespace task_priority_detail

inline std::optional<TaskPriority> stringToTaskPriority(std::string_view s) {
    using task_priority_detail::iequals;
    if (iequals(s, "Critical")) return TaskPriority::Critical;
    if (iequals(s, "High")) return TaskPriority::High;
    if (iequals(s, "Normal")) return TaskPriority::Normal;
    if (iequals(s, "Low")) return TaskPriority::Low;
    return std::nullopt;
}

// Returns true if a has higher priority than b
inline bool comparePriority(TaskPriority a, TaskPriority b) {
    auto rank = [](TaskPriority p) -> int {
        switch (p) {
            case TaskPriority::Critical: return 0;
            case TaskPriority::High: return 1;
            case TaskPriority::Normal: return 2;
            case TaskPriority::Low: return 3;
        }
        return 2;
    };
    return rank(a) < rank(b);
}

} // namespace naw::desktop_pet::service::types

