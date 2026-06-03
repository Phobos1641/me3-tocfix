// crc_hash.hpp -- UE3-style CRC32 hash used by the TOC hash table.

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace auto_toc {

namespace detail {

// ASCII-only upper-casing -- the source strings are filenames and we want
// behaviour identical to the C# `string.ToUpper()` for the ASCII range.
[[nodiscard]] constexpr char to_upper_ascii(char c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - ('a' - 'A')) : c;
}

[[nodiscard]] constexpr std::array<std::uint32_t, 256> make_crc_table() noexcept {
    constexpr std::uint32_t POLYNOMIAL = 0x04C11DB7u;
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t idx = 0; idx < 256; ++idx) {
        std::uint32_t crc = idx << 24;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80000000u) ? ((crc << 1) ^ POLYNOMIAL)
                                      : (crc << 1);
        }
        table[idx] = crc;
    }
    return table;
}

} // namespace detail

inline constexpr auto CRC_TABLE = detail::make_crc_table();

// UE3 hashes its strings as UTF-16, but only the ASCII subset ever appears
// in TOC names. The "two passes per char" mirrors that: first the low byte
// of the UTF-16 code unit, then the high byte (always 0 for ASCII).
[[nodiscard]] constexpr std::uint32_t string_full_hash(std::string_view str) noexcept {
    std::uint32_t hash = 0;
    for (char c : str) {
        const auto upper = static_cast<std::uint8_t>(detail::to_upper_ascii(c));
        hash = ((hash >> 8) & 0x00FFFFFFu) ^ CRC_TABLE[(hash ^ upper) & 0xFFu];
        hash = ((hash >> 8) & 0x00FFFFFFu) ^ CRC_TABLE[hash & 0xFFu];
    }
    return hash;
}

} // namespace auto_toc
