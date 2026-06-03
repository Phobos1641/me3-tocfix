// toc_creator.hpp -- enumerates TOCable files for a Mass Effect game tree
// and packs them into a TocBinFile.
//
// The four supported games each have their own quirks:
//
//   ME3   -- BioGame/CookedPCConsole + Movies; DLC subfolders TOC'd separately.
//   LE1   -- BioGame/CookedPCConsole + Movies + Content/Packages/ISACT;
//            DLC mount priorities resolve filename collisions, and *all* of
//            its files end up in the basegame TOC. No per-DLC TOCs.
//   LE2   -- like ME3 but BioGame's DLC tree is also enumerated for the
//            basegame TOC, in addition to having per-DLC TOCs.
//   LE3   -- same as LE2.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "toc_bin_file.hpp"

namespace auto_toc {

enum class Game {
    ME3,
    LE1,
    LE2,
    LE3,
};

[[nodiscard]] std::string to_string(Game g);

class TocCreator {
public:
    // (Path-on-TOC, file size in bytes). Path-on-TOC is what gets written into
    // the entry's `name` field -- always backslash-separated.
    using EntryInfo = std::pair<std::string, std::int32_t>;

    // Top-level entry points. Both throw std::runtime_error on failure
    // (bad input, no TOCable files, etc).

    // Build the basegame TOC for the BIOGame directory of `game`.
    [[nodiscard]] static TocBinFile
    create_basegame_toc(const std::filesystem::path& biogame_dir, Game game);

    // Build a per-DLC TOC for a specific DLC folder (e.g. .../DLC/DLC_CON_JAM).
    // LE1 doesn't ship per-DLC TOCs; calling this with LE1 throws.
    [[nodiscard]] static TocBinFile
    create_dlc_toc(const std::filesystem::path& dlc_dir, Game game);

    // Lower-level: enumerate all TOCable files under `base_folder`, applying
    // BioGame-specific subfolder filtering when appropriate.
    [[nodiscard]] static std::vector<std::filesystem::path>
    get_files(const std::filesystem::path& base_folder, bool is_le2_le3);

    // Just the TOCable files in a single directory (no recursion).
    [[nodiscard]] static std::vector<std::filesystem::path>
    get_tocable_files(const std::filesystem::path& path);

    // LE1's mount-priority resolution. Returns the basegame file list with
    // DLC overrides applied; only one instance of each (uppercase) filename
    // remains, with the highest-priority mount winning.
    [[nodiscard]] static std::vector<std::filesystem::path>
    get_le1_files(const std::vector<std::filesystem::path>& basegame_files,
                  const std::filesystem::path& biogame_dir);

    // Pack a list of (toc_name, size) pairs into a TocBinFile, choosing
    // a hash-table size that keeps the fill rate reasonable.
    [[nodiscard]] static TocBinFile
    create_toc_for_entries(const std::vector<EntryInfo>& entries);
};

} // namespace auto_toc
