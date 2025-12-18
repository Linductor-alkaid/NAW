#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace naw::desktop_pet::service::types {

// 通用时间戳（系统时钟）
using Timestamp = std::chrono::system_clock::time_point;

// 常用标识类型（后续可替换为强类型包装）
using RequestId = std::string;
using ModelId = std::string;
using ConfigPath = std::string;

// 毫秒级 Unix 时间戳（用于日志/序列化）
inline uint64_t toUnixMillis(Timestamp tp) {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(tp.time_since_epoch()).count());
}

inline Timestamp fromUnixMillis(uint64_t ms) {
    using namespace std::chrono;
    return Timestamp(milliseconds(ms));
}

inline Timestamp nowTimestamp() { return std::chrono::system_clock::now(); }

} // namespace naw::desktop_pet::service::types

