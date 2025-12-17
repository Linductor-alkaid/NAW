#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace naw::desktop_pet::service::utils {

// 流式回调占位：按块接收响应正文
using StreamHandler = std::function<void(std::string_view chunk)>;

// URL 组件百分号编码（UTF-8 字节级）
std::string encodeUrlComponent(const std::string& value);

// application/x-www-form-urlencoded 序列化
std::string serializeForm(const std::map<std::string, std::string>& form);

// JSON 序列化为字符串（可选缩进）
std::string toJsonBody(const nlohmann::json& j, bool pretty = false);

// 安全解析 JSON，失败返回 std::nullopt，并可写入错误信息
std::optional<nlohmann::json> parseJsonSafe(const std::string& text, std::string* error = nullptr);

// Base64 编码/解码（标准字符表，无换行）
std::string encodeBase64(const std::vector<uint8_t>& data);
std::string encodeBase64(const std::string& data);
std::optional<std::vector<uint8_t>> decodeBase64(const std::string& text);

} // namespace naw::desktop_pet::service::utils
