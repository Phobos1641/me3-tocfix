// toc_bin_file.hpp -- in-memory representation of a PCConsoleTOC.bin.
//
// The on-disk format is:
//   uint32  magic (0x3AB70C13)
//   uint32  media-data count (always 0 on PC)
//   uint32  hash-bucket count
//   <hash table: per bucket> { int32 offset_to_first_entry; int32 entry_count; }
//   <entry chains, one per non-empty bucket, in bucket order>
//
// `offset_to_first_entry` is *relative to that hash-table slot* (not file
// origin). Each entry is then padded so the next entry starts on a 4-byte
// boundary; the very last entry of the very last non-empty bucket has its
// `toc_size` field stored as 0 to terminate iteration.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace auto_toc {

class TocBinFile {
public:
    static constexpr std::uint32_t MAGIC_NUMBER = 0x3AB70C13u;

    struct Entry {
        std::int16_t flags = 0;
        std::int32_t size  = 0;
        std::array<std::uint8_t, 20> sha1{};
        bool has_sha1 = false;
        std::string name;

        // Bytes this entry occupies in memory before alignment padding.
        [[nodiscard]] std::int16_t memory_size() const noexcept;

        // Bytes this entry occupies on disk (memory_size + 4-byte alignment).
        [[nodiscard]] std::int16_t toc_size() const noexcept;
    };

    struct HashBucket {
        std::vector<Entry> entries;
    };

    std::vector<HashBucket> hash_buckets;

    // Serialise this TOC structure to the binary on-disk format.
    [[nodiscard]] std::vector<std::uint8_t> save() const;

    // Convenience: serialise and write to disk in one step.
    void write_to_file(const std::filesystem::path& path) const;
};

} // namespace auto_toc
