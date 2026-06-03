#include "toc_bin_file.hpp"

#include <fstream>
#include <stdexcept>
#include <type_traits>

namespace auto_toc {

namespace {

// Append `value` little-endian to the buffer. ME / LE are PC titles and the
// TOC format is little-endian regardless of host byte order; doing this by
// hand keeps the code portable to a hypothetical big-endian builder.
template <typename T>
void append_le(std::vector<std::uint8_t>& buf, T value) noexcept {
    using U = std::make_unsigned_t<T>;
    auto u = static_cast<U>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        buf.push_back(static_cast<std::uint8_t>((u >> (i * 8)) & 0xFFu));
    }
}

template <typename T>
void write_le_at(std::vector<std::uint8_t>& buf, std::size_t pos, T value) noexcept {
    using U = std::make_unsigned_t<T>;
    auto u = static_cast<U>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        buf[pos + i] = static_cast<std::uint8_t>((u >> (i * 8)) & 0xFFu);
    }
}

} // anonymous namespace

std::int16_t TocBinFile::Entry::memory_size() const noexcept {
    // 2 bytes toc_size + 2 bytes flags + 4 bytes file size + 20 bytes SHA1
    // + name + 1 byte null terminator.
    return static_cast<std::int16_t>(
        2 * sizeof(std::int16_t)   // toc_size, flags
        + sizeof(std::int32_t)     // file size
        + 20                       // SHA1
        + name.size() + 1);        // ASCII name + null terminator
}

std::int16_t TocBinFile::Entry::toc_size() const noexcept {
    const auto mem = memory_size();
    const auto pad = (mem % 4 == 0) ? 0 : (4 - (mem % 4));
    return static_cast<std::int16_t>(mem + pad);
}

std::vector<std::uint8_t> TocBinFile::save() const {
    // Only the very last entry in the very last non-empty bucket terminates
    // the table by storing 0 in its toc_size field. Find that bucket up front.
    int last_bucket_with_entries = -1;
    for (int i = static_cast<int>(hash_buckets.size()) - 1; i >= 0; --i) {
        if (!hash_buckets[i].entries.empty()) {
            last_bucket_with_entries = i;
            break;
        }
    }

    std::vector<std::uint8_t> buf;
    buf.reserve(64 * 1024);

    append_le<std::uint32_t>(buf, MAGIC_NUMBER);          // magic / endian check
    append_le<std::int32_t>(buf, 0);                      // media data count
    append_le<std::int32_t>(buf, static_cast<std::int32_t>(hash_buckets.size()));

    // Reserve zero-filled space for the hash table; we'll patch each slot
    // once we know where the bucket's first entry landed. (The C# original
    // achieves the same effect by seeking past this region and relying on
    // MemoryStream's implicit zero-fill on the next Write.)
    const std::size_t hash_table_start = buf.size();
    buf.resize(buf.size() + hash_buckets.size() * 8, 0u);

    for (std::size_t i = 0; i < hash_buckets.size(); ++i) {
        const auto& bucket = hash_buckets[i];
        const std::size_t bucket_entries_start = buf.size();

        for (std::size_t j = 0; j < bucket.entries.size(); ++j) {
            const auto& entry = bucket.entries[j];
            const bool dont_write_entry_size =
                static_cast<int>(i) == last_bucket_with_entries
                && j == bucket.entries.size() - 1;

            const std::size_t entry_start = buf.size();
            const auto toc_sz = entry.toc_size();

            append_le<std::int16_t>(buf, dont_write_entry_size
                                            ? std::int16_t{0}
                                            : toc_sz);
            append_le<std::int16_t>(buf, entry.flags);
            append_le<std::int32_t>(buf, entry.size);

            if (entry.has_sha1) {
                buf.insert(buf.end(), entry.sha1.begin(), entry.sha1.end());
            } else {
                buf.insert(buf.end(), 20, std::uint8_t{0});
            }

            buf.insert(buf.end(), entry.name.begin(), entry.name.end());
            buf.push_back(0); // ASCII null terminator

            // Pad to 4-byte alignment.
            const std::size_t expected_end =
                entry_start + static_cast<std::size_t>(toc_sz);
            while (buf.size() < expected_end) buf.push_back(0);
        }

        // Patch the hash table slot for this bucket. Empty buckets keep
        // their (offset=0, count=0) initialisation.
        if (!bucket.entries.empty()) {
            const std::size_t hash_slot_pos = hash_table_start + i * 8;
            const auto first_entry_offset =
                static_cast<std::int32_t>(bucket_entries_start) -
                static_cast<std::int32_t>(hash_slot_pos);
            write_le_at<std::int32_t>(buf, hash_slot_pos, first_entry_offset);
            write_le_at<std::int32_t>(buf, hash_slot_pos + 4,
                static_cast<std::int32_t>(bucket.entries.size()));
        }
    }

    return buf;
}

void TocBinFile::write_to_file(const std::filesystem::path& path) const {
    const auto data = save();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Failed to open TOC output file: "
                                 + path.string());
    }
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    if (!out) {
        throw std::runtime_error("Failed to write TOC file: " + path.string());
    }
}

} // namespace auto_toc
