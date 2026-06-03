#include "toc_creator.hpp"

#include "crc_hash.hpp"
#include "ini_parser.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>

namespace auto_toc {

namespace fs = std::filesystem;

namespace {

// Extensions that get listed in PCConsoleTOC.bin. Same set the C# code uses.
constexpr std::array<std::string_view, 13> TOCABLE_EXTENSIONS = {
    ".pcc", ".afc", ".bik", ".bin", ".tlk", ".cnd", ".upk",
    ".tfc", ".isb", ".usf", ".txt", ".ini", ".dlc",
};

[[nodiscard]] bool iequal(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string lower_copy(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

[[nodiscard]] std::string upper_copy(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

[[nodiscard]] bool is_tocable_extension(const fs::path& p) {
    const auto ext = lower_copy(p.extension().string());
    for (const auto& candidate : TOCABLE_EXTENSIONS) {
        if (ext == candidate) return true;
    }
    return false;
}

// Case-insensitive substring search; returns position in haystack or npos.
[[nodiscard]] std::size_t find_ci(std::string_view haystack,
                                   std::string_view needle) noexcept {
    if (needle.empty() || haystack.size() < needle.size()) {
        return needle.empty() ? 0 : std::string_view::npos;
    }
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (std::tolower(static_cast<unsigned char>(haystack[i + j])) !=
                std::tolower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match) return i;
    }
    return std::string_view::npos;
}

// Append all of `src` onto `dst`, moving entries (no copies of paths).
void append_moved(std::vector<fs::path>& dst, std::vector<fs::path>&& src) {
    dst.reserve(dst.size() + src.size());
    for (auto& p : src) dst.push_back(std::move(p));
}

// The on-disk TOC name field uses Windows-style backslashes regardless of
// host platform -- the games parse it that way. We normalise here.
[[nodiscard]] std::string toc_name_from(std::string_view rel_path) {
    std::string out;
    out.reserve(rel_path.size());
    for (char c : rel_path) out.push_back(c == '/' ? '\\' : c);
    return out;
}

// File size as int32, with a loud warning if a file would overflow. The
// original C# version silently casts via (int)FileInfo.Length and produces a
// corrupt TOC for files >= 2 GiB; surface that instead of pretending it works.
[[nodiscard]] std::int32_t safe_file_size(const fs::path& p) {
    std::error_code ec;
    const auto sz = fs::file_size(p, ec);
    if (ec) {
        throw std::runtime_error("Could not stat file '" + p.string() +
                                 "': " + ec.message());
    }
    constexpr std::uintmax_t INT32_LIMIT = 0x7FFFFFFFu;
    if (sz > INT32_LIMIT) {
        std::cerr << "WARNING: '" << p.string() << "' is " << sz
                  << " bytes; exceeds 2 GiB and will be truncated in the TOC.\n";
    }
    return static_cast<std::int32_t>(sz & 0x7FFFFFFFu);
}

// Hard ceiling on the on-disk size of a single TOC entry. The format stores
// each entry's serialized length in an int16 field, capping it at 32767
// bytes; subtract 28 bytes of fixed header (toc_size + flags + file size +
// SHA1), 1 byte for the null terminator, and up to 3 bytes of alignment
// padding, leaving ~32735 bytes for the name itself. Realistic ME / LE
// filenames are well under 200 bytes -- this is purely a defensive bound.
constexpr std::size_t MAX_TOC_NAME_LENGTH = 32735;

[[nodiscard]] bool is_prime(std::int32_t n) noexcept {
    if (n < 2) return false;
    if (n < 4) return true;            // 2 and 3
    if (n % 2 == 0) return false;
    for (std::int32_t d = 3; d * d <= n; d += 2) {
        if (n % d == 0) return false;
    }
    return true;
}

// Largest prime p with p <= n. For n < 2 we return n unchanged (callers
// handle the trivial-table cases directly). Primes are dense (~1/ln(n) of
// integers), so for any reasonable input we expect ~ln(n) trials -- fewer
// than 10 even for n in the tens of thousands.
[[nodiscard]] std::int32_t largest_prime_at_most(std::int32_t n) noexcept {
    if (n < 2) return n;
    while (!is_prime(n)) --n;
    return n;
}

} // anonymous namespace

std::string to_string(Game g) {
    switch (g) {
        case Game::ME3: return "ME3";
        case Game::LE1: return "LE1";
        case Game::LE2: return "LE2";
        case Game::LE3: return "LE3";
    }
    return "?";
}

std::vector<fs::path>
TocCreator::get_tocable_files(const fs::path& path) {
    std::vector<fs::path> result;
    std::error_code ec;
    if (!fs::exists(path, ec) || !fs::is_directory(path, ec)) {
        return result; // Match C#'s "missing directory yields no files".
    }
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (is_tocable_extension(entry.path())) {
            result.push_back(entry.path());
        }
    }
    return result;
}

std::vector<fs::path>
TocCreator::get_files(const fs::path& base_folder, bool is_le2_le3) {
    std::vector<fs::path> result;

    // Files at this level. Skip a pre-existing PCConsoleTOC.bin (it's not
    // part of the game data, and it's also what we may be about to overwrite).
    auto here = get_tocable_files(base_folder);
    for (auto& f : here) {
        if (!iequal(f.filename().string(), "PCConsoleTOC.bin")) {
            result.push_back(std::move(f));
        }
    }

    std::error_code ec;
    if (!fs::exists(base_folder, ec) || !fs::is_directory(base_folder, ec)) {
        return result;
    }

    const auto dir_name = base_folder.filename().string();
    const bool is_biogame = iequal(dir_name, "BioGame");

    if (!is_biogame) {
        // Outside BioGame, recurse into every subdirectory.
        for (const auto& sub : fs::directory_iterator(base_folder, ec)) {
            if (ec) break;
            if (sub.is_directory(ec)) {
                append_moved(result, get_files(sub.path(), is_le2_le3));
            }
        }
        return result;
    }

    // BioGame: only certain subfolders count for the basegame TOC.
    for (const auto& sub : fs::directory_iterator(base_folder, ec)) {
        if (ec) break;
        if (!sub.is_directory(ec)) continue;
        const auto name = sub.path().filename().string();

        if (iequal(name, "CookedPCConsole") || iequal(name, "Movies")) {
            append_moved(result, get_files(sub.path(), is_le2_le3));
        } else if (is_le2_le3 && iequal(name, "DLC")) {
            // LE2/LE3 include their DLC tree in the basegame TOC as well.
            append_moved(result, get_files(sub.path(), is_le2_le3));
        } else if (iequal(name, "Content")) {
            // LE1: BioGame/Content/Packages/ISACT is the only thing under
            // Content that gets TOC'd.
            append_moved(result,
                get_files(sub.path() / "Packages" / "ISACT", is_le2_le3));
        }
    }
    return result;
}

std::vector<fs::path>
TocCreator::get_le1_files(const std::vector<fs::path>& basegame_files,
                          const fs::path& biogame_dir) {
    const auto dlc_root = biogame_dir / "DLC";
    std::error_code ec;
    if (!fs::exists(dlc_root, ec) || !fs::is_directory(dlc_root, ec)) {
        return basegame_files;
    }

    // Map of mount priority -> DLC folder. std::map gives us ordering for
    // free; we iterate descending so the highest mount wins.
    std::map<int, fs::path> dlc_mounts;
    for (const auto& sub : fs::directory_iterator(dlc_root, ec)) {
        if (ec) break;
        if (!sub.is_directory(ec)) continue;
        const auto name = sub.path().filename().string();
        if (name.size() < 4 || !iequal({name.data(), 4}, "DLC_")) continue;

        const auto autoload = sub.path() / "autoload.ini";
        if (!fs::exists(autoload, ec)) continue;

        try {
            const auto ini = IniFile::load(autoload);
            const auto mount_str = ini.get_value("ME1DLCMOUNT", "ModMount");
            if (!mount_str) continue;
            const int mount = std::stoi(*mount_str);
            dlc_mounts[mount] = sub.path();
        } catch (const std::exception& e) {
            std::cerr << "WARNING: Could not parse mount for '"
                      << sub.path().string() << "': " << e.what() << "\n";
        }
    }

    // upper-cased filename -> chosen full path. First insertion wins, and we
    // visit DLCs in descending mount order.
    std::unordered_map<std::string, fs::path> chosen;

    for (auto it = dlc_mounts.rbegin(); it != dlc_mounts.rend(); ++it) {
        auto files = get_files(it->second, /*is_le2_le3=*/false);
        for (auto& f : files) {
            const auto key = upper_copy(f.filename().string());
            chosen.try_emplace(key, std::move(f));
        }
    }

    // Basegame files fill in anything the DLCs didn't override.
    for (const auto& f : basegame_files) {
        const auto key = upper_copy(f.filename().string());
        chosen.try_emplace(key, f);
    }

    std::vector<fs::path> out;
    out.reserve(chosen.size());
    for (auto& kv : chosen) out.push_back(std::move(kv.second));
    return out;
}

TocBinFile
TocCreator::create_basegame_toc(const fs::path& biogame_dir, Game game) {
    const bool is_le2_le3 = (game == Game::LE2 || game == Game::LE3);
    auto files = get_files(biogame_dir, is_le2_le3);

    if (game == Game::LE1) {
        files = get_le1_files(files, biogame_dir);
    }

    if (files.empty()) {
        throw std::runtime_error(
            "There are no TOCable files in the specified directory.");
    }

    // Locate the "BIOGame" segment in the path so we can produce relative
    // names. We use the first file as the reference (all files share the
    // tree root).
    const auto first_path = files.front().string();
    const auto biogame_pos = find_ci(first_path, "BIOGame");

    // For non-ME3 we also pick up the shader cache. The C# version takes the
    // path *prefix* (whatever sits above "BIOGame") and looks for
    // Engine/Shaders alongside it.
    if (game != Game::ME3 && biogame_pos != std::string::npos) {
        const auto game_root = fs::path(first_path.substr(0, biogame_pos));
        append_moved(files, get_files(game_root / "Engine" / "Shaders",
                                       is_le2_le3));
    }

    std::vector<EntryInfo> entries;
    entries.reserve(files.size());
    for (const auto& f : files) {
        const auto full = f.generic_string(); // forward slashes
        // Re-locate "BIOGame" inside this specific path; under LE1 the DLC
        // overrides may sit elsewhere on disk than the basegame.
        const auto pos = find_ci(full, "BIOGame");
        const auto rel = (pos != std::string::npos) ? full.substr(pos) : full;
        entries.emplace_back(toc_name_from(rel), safe_file_size(f));
    }

    return create_toc_for_entries(entries);
}

TocBinFile
TocCreator::create_dlc_toc(const fs::path& dlc_dir, Game game) {
    if (game == Game::LE1) {
        throw std::runtime_error(
            "LE1 does not use per-DLC TOC files; all DLC content is folded "
            "into the basegame TOC by mount priority.");
    }

    const bool is_le2_le3 = (game == Game::LE2 || game == Game::LE3);
    auto files = get_files(dlc_dir, is_le2_le3);
    if (files.empty()) {
        throw std::runtime_error(
            "There are no TOCable files in the specified directory.");
    }

    // Locate the "DLC_" prefix (the DLC folder itself, e.g. DLC_CON_JAM)
    // and strip everything up to and including the next path separator,
    // so each entry's TOC name starts inside the DLC folder.
    const auto first = files.front().generic_string();
    const auto dlc_pos = find_ci(first, "DLC_");
    if (dlc_pos == std::string::npos) {
        throw std::runtime_error("Not a TOCable DLC directory.");
    }

    std::vector<EntryInfo> entries;
    entries.reserve(files.size());
    for (const auto& f : files) {
        const auto full = f.generic_string();
        const auto pos = find_ci(full, "DLC_");
        if (pos == std::string::npos) continue;

        // Strip "DLC_FOLDERNAME/" off the front -- everything after the
        // first '/' beyond `pos`.
        const auto trimmed = full.substr(pos);
        const auto slash = trimmed.find('/');
        const auto inside_dlc = (slash == std::string::npos)
                                    ? trimmed
                                    : trimmed.substr(slash + 1);
        entries.emplace_back(toc_name_from(inside_dlc), safe_file_size(f));
    }

    return create_toc_for_entries(entries);
}

TocBinFile
TocCreator::create_toc_for_entries(const std::vector<EntryInfo>& entries) {
    TocBinFile tbf;
    if (entries.empty()) return tbf;

    // Reject pathologically long names rather than letting them silently
    // overflow the int16 toc_size field. Real ME / LE filenames are well
    // under 200 bytes, so anything anywhere near MAX_TOC_NAME_LENGTH is
    // almost certainly a bug or malformed input.
    for (const auto& entry : entries) {
        if (entry.first.size() > MAX_TOC_NAME_LENGTH) {
            throw std::runtime_error(
                "TOC entry name exceeds " +
                std::to_string(MAX_TOC_NAME_LENGTH) +
                "-byte limit (would overflow int16 size field): " +
                entry.first);
        }
    }

    // The hash uses only the filename, not the full relative path; precompute
    // those once so the inner shrink-loop doesn't re-hash on every retry.
    std::vector<std::uint32_t> hashes;
    hashes.reserve(entries.size());
    for (const auto& entry : entries) {
        const auto& name = entry.first;
        // Take everything after the last separator.
        auto pos = name.find_last_of("/\\");
        std::string_view bare =
            (pos == std::string::npos) ? std::string_view{name}
                                       : std::string_view{name}.substr(pos + 1);
        hashes.push_back(string_full_hash(bare));
    }

    // Hash-table sizing: start at the largest prime <= N, then shrink to the
    // next-smaller prime any time more than 25% of buckets sit empty, with
    // a floor of N/2. The prime constraint matters because `hash % T`
    // distributes very unevenly when T shares factors with patterns in the
    // input -- powers of 2 are pathological, and round numbers like 100 or
    // 1000 cluster too. Snapping T to a prime is essentially free (a few
    // primality trials at TOC-build time, no runtime cost) and gives tighter
    // chains for free. This deviates from the C# original, which uses N
    // directly and gets lucky or unlucky depending on N's factorization.
    const auto N = static_cast<std::int32_t>(entries.size());
    auto table_size = (N >= 2) ? largest_prime_at_most(N) : N;

    std::vector<TocBinFile::HashBucket> buckets;
    while (true) {
        buckets.assign(static_cast<std::size_t>(table_size),
                       TocBinFile::HashBucket{});

        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto bucket_idx = hashes[i] % static_cast<std::uint32_t>(table_size);
            TocBinFile::Entry e;
            e.flags = 0;
            e.name = entries[i].first;
            e.size = entries[i].second;
            buckets[bucket_idx].entries.push_back(std::move(e));
        }

        // Count empty buckets; bail to a smaller size if too sparse.
        std::int32_t empty_count = 0;
        for (const auto& b : buckets) {
            if (b.entries.empty()) ++empty_count;
        }
        if (empty_count <= table_size / 4) break; // Fill rate >= 75%.

        const auto floor_size = std::max<std::int32_t>(N / 2, 1);
        const auto target = table_size - (table_size / 4);
        const auto candidate =
            (target >= 2) ? largest_prime_at_most(target) : target;
        const auto shrunk = std::max(candidate, floor_size);
        if (shrunk >= table_size) {
            // No further progress possible (already at or below the floor).
            // Accept the suboptimal layout rather than loop forever.
            std::cerr << "WARNING: Hash table fill rate is low even at 50% "
                         "of file count; TOC will work but is suboptimal.\n";
            break;
        }
        table_size = shrunk;
    }

    tbf.hash_buckets = std::move(buckets);
    return tbf;
}

} // namespace auto_toc
