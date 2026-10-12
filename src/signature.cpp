#include "launcher/error.hpp"
#include "launcher/manifest.hpp"
#include <algorithm>

namespace launcher {
std::vector<std::uint8_t> decode_base64(std::string_view text) {
    auto bad = [] { fail(ErrorCategory::Signature, "Noncanonical or malformed base64"); };
    if (text.empty() || text.size() % 4 || text.size() > 16 * 1024 * 1024)
        bad();
    std::vector<std::uint8_t> bytes;
    bytes.reserve(text.size() / 4 * 3);
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (std::size_t i = 0; i < text.size(); i += 4) {
        unsigned val = 0, pad = 0;
        for (unsigned j = 0; j < 4; ++j) {
            const auto c = text[i + j];
            if (c == '=') {
                ++pad;
                if (i + 4 != text.size() || j < 2)
                    bad();
                val <<= 6;
            } else {
                const auto n = alphabet.find(c);
                if (pad || n == alphabet.npos)
                    bad();
                val = (val << 6) | static_cast<unsigned>(n);
            }
        }
        if (pad > 2 || (pad == 2 && (val & 0xffff)) || (pad == 1 && (val & 0xff)))
            bad();
        bytes.push_back(static_cast<std::uint8_t>(val >> 16));
        if (pad < 2)
            bytes.push_back(static_cast<std::uint8_t>(val >> 8));
        if (pad < 1)
            bytes.push_back(static_cast<std::uint8_t>(val));
    }
    return bytes;
}
std::array<std::uint8_t, 32> public_key_from_hex(std::string_view text) {
    if (text.size() != 64)
        fail(ErrorCategory::Signature, "Public key must be 32 bytes");
    std::array<std::uint8_t, 32> out{};
    auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        fail(ErrorCategory::Signature, "Invalid key hex");
    };
    for (std::size_t i = 0; i < 32; ++i)
        out[i] = static_cast<std::uint8_t>((digit(text[i * 2]) << 4) | digit(text[i * 2 + 1]));
    return out;
}
} // namespace launcher
