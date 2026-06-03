#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "toc_bin_file.hpp"
#include "toc_creator.hpp"

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #ifdef _MSVC
    #pragma comment(lib, "advapi32.lib")
  #endif
#endif

namespace fs = std::filesystem;
using namespace auto_toc;

namespace {

constexpr std::string_view USAGE =
    "Usage:\n"
    "  auto_toc <path-to-game-exe>\n"
    "      e.g. \"D:\\Origin Games\\Mass Effect Legendary Edition\\Game\\ME3"
              "\\Binaries\\Win64\\MassEffect3.exe\"\n"
    "  auto_toc -r <game>\n"
    "      Detect install path from the Windows registry. <game> is one of:\n"
    "      ME3, LE1, LE2, LE3.\n";

[[nodiscard]] bool ends_with_ci(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const auto a = std::tolower(static_cast<unsigned char>(
            s[s.size() - suffix.size() + i]));
        const auto b = std::tolower(static_cast<unsigned char>(suffix[i]));
        if (a != b) return false;
    }
    return true;
}

#ifdef _WIN32

[[nodiscard]] std::optional<Game> parse_game_name(std::string_view name) {
    auto lower = std::string{name};
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "me3") return Game::ME3;
    if (lower == "le1") return Game::LE1;
    if (lower == "le2") return Game::LE2;
    if (lower == "le3") return Game::LE3;
    return std::nullopt;
}

#endif // _WIN32

// ---- Portable PE-format FileVersion reader --------------------------------
//
// All Windows executables are PE (Portable Executable) files: a thin DOS-stub
// header pointing at a PE/COFF header, followed by an optional header with
// data directories, and the section table. The version info we want lives in
// the resource section (data directory index 2, RT_VERSION resource) as a
// VS_VERSIONINFO record, inside which sits a VS_FIXEDFILEINFO struct headed
// by the well-known signature 0xFEEF04BD.
//
// Originally this was implemented with the Win32 GetFileVersionInfo API,
// which works fine on Windows but leaves Linux/macOS callers blind --
// detect_from_exe couldn't tell ME3 from LE3 anywhere except Windows. The
// PE format is a stable, public specification, so parsing it directly is
// straightforward and works identically on every platform.

namespace pe {

constexpr std::uint16_t DOS_MAGIC      = 0x5A4Du;       // "MZ"
constexpr std::uint32_t NT_MAGIC       = 0x00004550u;   // "PE\0\0"
constexpr std::uint16_t OPT_MAGIC_PE32 = 0x010Bu;
constexpr std::uint16_t OPT_MAGIC_PE32P = 0x020Bu;
constexpr std::size_t   DATA_DIR_RESOURCE_INDEX = 2;
constexpr std::uint32_t VS_FIXEDFILEINFO_SIG = 0xFEEF04BDu;

// Read a little-endian integer of type T at a given byte offset. Returns
// nullopt if the read would extend past the end of the buffer; this lets
// callers fail gracefully on malformed or truncated files without us needing
// a single try-catch in the actual parser.
template <typename T>
[[nodiscard]] std::optional<T>
read_le(const std::vector<std::uint8_t>& buf, std::size_t offset) noexcept {
    if (offset > buf.size() || offset + sizeof(T) > buf.size()) {
        return std::nullopt;
    }
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<T>(static_cast<T>(buf[offset + i]) << (i * 8));
    }
    return value;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>>
read_file_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return std::nullopt;
    const auto size = in.tellg();
    if (size < 0) return std::nullopt;
    in.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (size > 0 &&
        !in.read(reinterpret_cast<char*>(bytes.data()),
                 static_cast<std::streamsize>(size))) {
        return std::nullopt;
    }
    return bytes;
}

[[nodiscard]] std::optional<std::ifstream::pos_type>
get_file_size(const fs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return std::nullopt;
    const auto size = in.tellg();
    if (size < 0) return std::nullopt;
    return size;
}

// Convert a relative-virtual-address (used inside the PE file's data
// directories) to an absolute file offset. We do this by finding the section
// header whose virtual address range contains the RVA, then translating
// RVA -> raw-file offset using that section's PointerToRawData.
[[nodiscard]] std::optional<std::size_t>
rva_to_file_offset(const std::vector<std::uint8_t>& pe,
                   std::uint32_t rva,
                   std::size_t section_table_offset,
                   std::uint16_t num_sections) {
    constexpr std::size_t SECTION_HEADER_SIZE = 40;
    for (std::uint16_t i = 0; i < num_sections; ++i) {
        const auto base = section_table_offset + i * SECTION_HEADER_SIZE;
        const auto vsize   = read_le<std::uint32_t>(pe, base + 0x08);
        const auto vaddr   = read_le<std::uint32_t>(pe, base + 0x0C);
        const auto raw_ptr = read_le<std::uint32_t>(pe, base + 0x14);
        if (!vsize || !vaddr || !raw_ptr) return std::nullopt;
        if (rva >= *vaddr && rva < *vaddr + *vsize) {
            return *raw_ptr + (rva - *vaddr);
        }
    }
    return std::nullopt;
}

} // namespace pe

[[nodiscard]] std::optional<std::string>
get_file_version_string(const fs::path& exe_path) {
    using namespace pe;

    auto bytes_opt = read_file_bytes(exe_path);
    if (!bytes_opt) return std::nullopt;
    const auto& bytes = *bytes_opt;

    // 1. DOS header: "MZ" at byte 0; e_lfanew (offset to PE header) at 0x3C.
    const auto dos_magic = read_le<std::uint16_t>(bytes, 0);
    if (!dos_magic || *dos_magic != DOS_MAGIC) return std::nullopt;
    const auto e_lfanew = read_le<std::uint32_t>(bytes, 0x3C);
    if (!e_lfanew) return std::nullopt;

    // 2. PE signature.
    const auto pe_sig = read_le<std::uint32_t>(bytes, *e_lfanew);
    if (!pe_sig || *pe_sig != NT_MAGIC) return std::nullopt;

    // 3. COFF header (immediately after the 4-byte PE signature). We need
    //    NumberOfSections (offset 0x02) and SizeOfOptionalHeader (offset 0x10).
    const std::size_t coff_offset = static_cast<std::size_t>(*e_lfanew) + 4;
    const auto num_sections = read_le<std::uint16_t>(bytes, coff_offset + 0x02);
    const auto opt_hdr_size = read_le<std::uint16_t>(bytes, coff_offset + 0x10);
    if (!num_sections || !opt_hdr_size) return std::nullopt;

    // 4. Optional header. Magic at offset 0 tells us PE32 vs PE32+, which
    //    determines where the data directories live (96 vs 112 bytes in).
    const std::size_t opt_hdr_offset = coff_offset + 20;
    const auto opt_magic = read_le<std::uint16_t>(bytes, opt_hdr_offset);
    if (!opt_magic) return std::nullopt;

    std::size_t data_dirs_offset = 0;
    if (*opt_magic == OPT_MAGIC_PE32) {
        data_dirs_offset = opt_hdr_offset + 96;
    } else if (*opt_magic == OPT_MAGIC_PE32P) {
        data_dirs_offset = opt_hdr_offset + 112;
    } else {
        return std::nullopt;
    }

    // 5. Data directory entry #2 is the resource directory.
    const auto res_dir_entry =
        data_dirs_offset + DATA_DIR_RESOURCE_INDEX * 8;
    const auto res_rva  = read_le<std::uint32_t>(bytes, res_dir_entry);
    const auto res_size = read_le<std::uint32_t>(bytes, res_dir_entry + 4);
    if (!res_rva || !res_size || *res_rva == 0 || *res_size == 0) {
        return std::nullopt; // Binary has no resource section.
    }

    // 6. Convert the resource RVA to a raw file offset.
    const std::size_t section_table_offset =
        opt_hdr_offset + static_cast<std::size_t>(*opt_hdr_size);
    const auto res_file_offset = rva_to_file_offset(
        bytes, *res_rva, section_table_offset, *num_sections);
    if (!res_file_offset) return std::nullopt;

    // 7. Scan inside the resource section for the VS_FIXEDFILEINFO signature.
    //    We could fully walk the IMAGE_RESOURCE_DIRECTORY tree (Type=16 →
    //    Name=1 → Language → DataEntry), but the magic 0xFEEF04BD is so
    //    specific that scanning the resource bytes works reliably and is
    //    far less code. We keep a sanity check on the major version to
    //    reject stray byte patterns that happen to match the magic.
    const std::size_t scan_start = *res_file_offset;
    const std::size_t scan_end = std::min(
        bytes.size(),
        scan_start + static_cast<std::size_t>(*res_size));
    if (scan_end < scan_start + 16) return std::nullopt;

    // VS_FIXEDFILEINFO is 4-byte aligned within the resource per the spec.
    for (std::size_t pos = scan_start; pos + 16 <= scan_end; pos += 4) {
        const auto sig = read_le<std::uint32_t>(bytes, pos);
        if (!sig || *sig != VS_FIXEDFILEINFO_SIG) continue;

        // Layout from the magic: dwSignature, dwStrucVersion, then
        // dwFileVersionMS (major<<16 | minor), dwFileVersionLS (build<<16 | rev).
        const auto fvms = read_le<std::uint32_t>(bytes, pos + 8);
        const auto fvls = read_le<std::uint32_t>(bytes, pos + 12);
        if (!fvms || !fvls) continue;

        const auto major = (*fvms >> 16) & 0xFFFFu;
        const auto minor =  *fvms        & 0xFFFFu;
        const auto build = (*fvls >> 16) & 0xFFFFu;
        const auto rev   =  *fvls        & 0xFFFFu;

        // Plausibility filter for false positives. ME3/LE3 use 1.x and 2.x;
        // any normal Windows binary's major version is well under 100.
        if (major == 0 || major > 100) continue;

        return std::to_string(major) + "." + std::to_string(minor) + "."
             + std::to_string(build) + "." + std::to_string(rev);
    }

    return std::nullopt;
}

// ---- Windows-only registry helpers ----------------------------------------

#ifdef _WIN32

[[nodiscard]] std::string narrow(const std::wstring& s) {
    if (s.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, s.data(),
                                           static_cast<int>(s.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string out(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), needed, nullptr, nullptr);
    return out;
}

[[nodiscard]] std::optional<std::string>
read_registry_string(HKEY root, const std::wstring& subkey,
                     const std::wstring& value) {
    HKEY h = nullptr;
    // KEY_WOW64_64KEY: Mass Effect Legendary Edition is a 64-bit install and
    // its key sits in the native 64-bit view. (We could omit this and read
    // ME3's Wow6432Node path directly, but being explicit avoids surprises
    // when this binary is built as 32-bit.)
    if (RegOpenKeyExW(root, subkey.c_str(), 0,
                      KEY_READ | KEY_WOW64_64KEY, &h) != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(h, value.c_str(), nullptr, &type, nullptr, &bytes)
            != ERROR_SUCCESS
        || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        RegCloseKey(h);
        return std::nullopt;
    }

    std::wstring data(bytes / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(h, value.c_str(), nullptr, &type,
                         reinterpret_cast<LPBYTE>(data.data()), &bytes)
            != ERROR_SUCCESS) {
        RegCloseKey(h);
        return std::nullopt;
    }
    RegCloseKey(h);

    // Strip trailing null characters that RegQueryValueEx tends to include.
    while (!data.empty() && data.back() == L'\0') data.pop_back();
    return narrow(data);
}

[[nodiscard]] std::optional<fs::path>
get_gamepath_from_registry(Game game) {
    if (game == Game::ME3) {
        // ME3 is a 32-bit game; its install key lives under Wow6432Node when
        // viewed from a 64-bit process.
        if (auto v = read_registry_string(
                HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Wow6432Node\\BioWare\\Mass Effect 3",
                L"Install Dir")) {
            return fs::path(*v);
        }
        return std::nullopt;
    }

    // LE installer writes a key whose name contains a U+2122 TRADE MARK SIGN.
    // EA's installer hasn't been entirely consistent about this -- some
    // installs end up with the plain ASCII name. Try the trademark variant
    // first (the more common one) then fall back.
    constexpr auto VALUE = L"Install Dir";
    if (auto v = read_registry_string(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\BioWare\\Mass Effect\u2122 Legendary Edition",
            VALUE)) {
        return fs::path(*v);
    }
    if (auto v = read_registry_string(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\BioWare\\Mass Effect Legendary Edition",
            VALUE)) {
        return fs::path(*v);
    }
    return std::nullopt;
}

#endif // _WIN32

// Detect the game (and its base directory) from a path to one of the
// supported Mass Effect executables.
[[nodiscard]] std::pair<fs::path, Game>
detect_from_exe(const fs::path& exe_path) {
    std::error_code ec;
    if (!fs::exists(exe_path, ec)) {
        throw std::runtime_error("Executable does not exist: "
                                 + exe_path.string());
    }
    const auto path_str = exe_path.string();
    auto lower = path_str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const auto bin_pos = lower.rfind("binaries");
    if (bin_pos == std::string::npos) {
        throw std::runtime_error(
            "Could not locate 'Binaries' folder in path: " + path_str);
    }
    const fs::path game_dir = path_str.substr(0, bin_pos);

    if (ends_with_ci(path_str, "MassEffect1.exe")) return {game_dir, Game::LE1};
    if (ends_with_ci(path_str, "MassEffect2.exe")) return {game_dir, Game::LE2};
    if (ends_with_ci(path_str, "MassEffect3.exe")) {
        // Original ME3 has FileVersion 1.x; LE3 ships as 2.x. The PE-format
        // version reader is platform-independent, so this works on Linux /
        // macOS too -- handy for headless build pipelines.
        if (auto ver = get_file_version_string(exe_path)) {
            if (!ver->empty() && ver->front() == '1') {
                return {game_dir, Game::ME3};
            }
            return {game_dir, Game::LE3};
        }
        // No version resource (corrupt or pathological binary). Fall back
        // to LE3 since it's the modern target most people care about, but
        // warn so the user knows we guessed.
        std::cerr << "NOTE: Could not read FileVersion of MassEffect3.exe; "
                     "assuming LE3. If this is original ME3, pass the path "
                     "to MassEffect1.exe / MassEffect2.exe / MassEffect3.exe "
                     "from the actual install or rename a copy unambiguously.\n";
        return {game_dir, Game::LE3};
    }
    throw std::runtime_error(
        "Executable is not a supported Mass Effect game: " + path_str);
}

void run_basegame_toc(const fs::path& biogame_dir, Game game) {
    try {
        const auto toc = TocCreator::create_basegame_toc(biogame_dir, game);
        toc.write_to_file(biogame_dir / "PCConsoleTOC.bin");
        std::cout << "  basegame TOC: " << biogame_dir.string() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "  basegame TOC FAILED for " << biogame_dir.string()
                  << ": " << e.what() << "\n";
    }
}

void run_dlc_toc(const fs::path& dlc_dir, Game game) {
    try {
        const auto toc = TocCreator::create_dlc_toc(dlc_dir, game);
        toc.write_to_file(dlc_dir / "PCConsoleTOC.bin");
        std::cout << "  DLC TOC:      " << dlc_dir.string() << "\n";
    } catch (const std::exception& e) {
        // Most "failures" here are perfectly fine -- packed (SFAR) DLCs have
        // no TOCable files on disk. Match the original C#'s soft behaviour.
        std::cerr << "  DLC TOC skipped for " << dlc_dir.string()
                  << " (" << e.what() << ")\n";
    }
}

void generate_toc_from_gamedir(const fs::path& game_dir, Game game) {
    const auto biogame = game_dir / "BIOGame";
    const auto dlc_dir = biogame / "DLC";
    const auto toc_bin = biogame / "PCConsoleTOC.bin";
    const auto toc_bootstrap = biogame / "PCConsoleTOC.txt";

    std::vector<std::future<void>> jobs;
    jobs.push_back(std::async(std::launch::async,
        [&] { run_basegame_toc(biogame, game); }));

    if (game != Game::LE1) {
        std::error_code ec;
        if (fs::exists(dlc_dir, ec) && fs::is_directory(dlc_dir, ec)) {
            for (const auto& sub : fs::directory_iterator(dlc_dir, ec)) {
                if (ec) break;
                if (!sub.is_directory(ec)) continue;
                const fs::path d = sub.path();
                jobs.push_back(std::async(std::launch::async,
                    [d, game] { run_dlc_toc(d, game); }));
            }
        } else {
            std::cout << "  DLC folder not present; basegame only.\n";
        }
    }

    for (auto& j : jobs) j.get();

    const auto &toc_size = pe::get_file_size(toc_bin);
    if (!toc_size) {
        std::cout << "  Failed to get master TOC size; skip writing bootstrap.\n";
        return;
    }

    std::ofstream out(toc_bootstrap, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error(
        "Could not write bootstrap TOC: " + toc_bootstrap.string());

    /** A stock OG ME3 file looks like this:
      * 858836 0 0 ..\BioGame\PCConsoleTOC.bin 0
      *
      * Note the case BioGame vs BIOGame
      * Since the actual directory on an OG ME3 install is
      * actually "BIOGame", use that when writing the new bootstrap.
      */

    out << std::dec << toc_size.value() << " 0 0 ..\\BIOGame\\PCConsoleTOC.bin 0\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    try {
        fs::path game_dir;
        Game game = Game::ME3;

        if (argc == 2) {
            const fs::path arg = argv[1];
            if (ends_with_ci(arg.string(), ".exe")) {
                std::tie(game_dir, game) = detect_from_exe(arg);
            } else {
                // Treat the argument as the game's root directory and assume
                // ME3 (matching the C# fallback).
                game_dir = arg;
            }
        } else if (argc == 3 && std::string_view{argv[1]} == "-r") {
#ifdef _WIN32
            const auto parsed = parse_game_name(argv[2]);
            if (!parsed) {
                std::cerr << "Not a supported Mass Effect game: "
                          << argv[2] << "\n";
                return 1;
            }
            game = *parsed;
            const auto from_reg = get_gamepath_from_registry(game);
            if (!from_reg) {
                std::cerr << "Unable to detect gamepath from registry.\n";
                return 1;
            }
            game_dir = *from_reg;
            switch (game) {
                case Game::LE1: game_dir = game_dir / "Game" / "ME1"; break;
                case Game::LE2: game_dir = game_dir / "Game" / "ME2"; break;
                case Game::LE3: game_dir = game_dir / "Game" / "ME3"; break;
                case Game::ME3: break;
            }
            std::cout << "Game location detected in registry: "
                      << game_dir.string() << "\n";
#else
            std::cerr << "-r is only supported on Windows.\n";
            return 1;
#endif
        } else {
            std::cout << USAGE;
            return (argc == 1) ? 0 : 1;
        }

        std::cout << "Generating " << to_string(game) << " TOCs in "
                  << game_dir.string() << "\n";
        generate_toc_from_gamedir(game_dir, game);
        std::cout << "Done!\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 2;
    }
}
