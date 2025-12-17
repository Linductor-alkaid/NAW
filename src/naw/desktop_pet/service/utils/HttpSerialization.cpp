#include "naw/desktop_pet/service/utils/HttpSerialization.h"

#include <array>
#include <sstream>
#include <iomanip>
#include <cctype>

namespace naw::desktop_pet::service::utils {

std::string encodeUrlComponent(const std::string& value) {
    auto isUnreserved = [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
    };

    std::ostringstream encoded;
    encoded << std::uppercase << std::hex << std::setfill('0');

    for (unsigned char c : value) {
        if (isUnreserved(c)) {
            encoded << static_cast<char>(c);
        } else {
            encoded << '%' << std::setw(2) << static_cast<int>(c);
        }
    }
    return encoded.str();
}

std::string serializeForm(const std::map<std::string, std::string>& form) {
    std::ostringstream body;
    bool first = true;
    for (const auto& [k, v] : form) {
        if (!first) body << "&";
        first = false;
        body << encodeUrlComponent(k) << "=" << encodeUrlComponent(v);
    }
    return body.str();
}

std::string toJsonBody(const nlohmann::json& j, bool pretty) {
    if (pretty) {
        return j.dump(2);
    }
    return j.dump();
}

std::optional<nlohmann::json> parseJsonSafe(const std::string& text, std::string* error) {
    try {
        return nlohmann::json::parse(text);
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return std::nullopt;
    }
}

std::string encodeBase64(const std::vector<uint8_t>& data) {
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 2 < data.size()) {
        uint32_t triple = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(kTable[(triple >> 18) & 0x3F]);
        out.push_back(kTable[(triple >> 12) & 0x3F]);
        out.push_back(kTable[(triple >> 6) & 0x3F]);
        out.push_back(kTable[triple & 0x3F]);
        i += 3;
    }

    if (i < data.size()) {
        uint32_t triple = 0;
        triple |= data[i] << 16;
        if (i + 1 < data.size()) {
            triple |= data[i + 1] << 8;
        }
        out.push_back(kTable[(triple >> 18) & 0x3F]);
        out.push_back(kTable[(triple >> 12) & 0x3F]);
        if (i + 1 < data.size()) {
            out.push_back(kTable[(triple >> 6) & 0x3F]);
        } else {
            out.push_back('=');
        }
        out.push_back('=');
    }

    return out;
}

std::string encodeBase64(const std::string& data) {
    return encodeBase64(std::vector<uint8_t>(data.begin(), data.end()));
}

std::optional<std::vector<uint8_t>> decodeBase64(const std::string& text) {
    auto decodeChar = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        if (c == '=') return -1;
        return -2; // invalid
    };

    std::vector<uint8_t> output;
    output.reserve((text.size() / 4) * 3);

    uint32_t buffer = 0;
    int bitsCollected = 0;
    for (char c : text) {
        int val = decodeChar(c);
        if (val == -2) {
            return std::nullopt; // invalid char
        }
        if (val == -1) {
            // padding; stop processing further chars
            break;
        }
        buffer = (buffer << 6) | val;
        bitsCollected += 6;
        if (bitsCollected >= 8) {
            bitsCollected -= 8;
            output.push_back(static_cast<uint8_t>((buffer >> bitsCollected) & 0xFF));
        }
    }
    return output;
}

} // namespace naw::desktop_pet::service::utils
