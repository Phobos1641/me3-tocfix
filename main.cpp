#include <print>
#include <filesystem>
#include <vector>
#include <set>
#include <unordered_set>
#include <string_view>
#include <algorithm>
#include <fstream>
#include <limits>
#include <cassert>

#include "hash.hpp"
#include "win32.hpp"

/** Notes on paths
  *
  * Assume base path: c/Games/MassEffect3/
  *
  * Base game files:
  * - ./BIOGame/
  *
  * Base game file scan must ignore the following subdirectories:
  * - ./BIOGame/DLC/
  * - ./BIOGame/Patches/
  * - ./BIOGame/Splash/
  * if LEGE
  * - ./BioGame/Config/
  * endif
  *
  * DLC:
  * - ./BIOGame/DLC/
  *
  * DLC file scan must only include subdirectory names beginning with the string "DLC_"
  *
  * Extensions to scan for:
  * - .afc
  * - .bik
  * - .bin
  * - .cnd
  * - .pcc
  * - .upk
  * - .tfc
  * - .tlk
  * - .txt
  * if LEGENDARY_EDITION
  * - .dlc
  * - .ini
  * - .isb
  * - .usf
  * endif
  *
  */

/**
  * Notes on the PCConsoleTOC.bin file:
  *
  * The file uses Windows-style directory separators.
  * Paths are relative to the BioGame / BIOGame directory and includes filenames.
  * Hashes are made on the uppercase full path.
  *
  * if LEGENDARY_EDITION
  * Store a separate PCConsoleTOC.bin file in the BioGame/DLC/ directory.
  * else
  * Store one PCConsoleTOC.bin file for all files, including the BIOGame/DLC/ directory.
  * endif
  *
  */

bool iequals(std::string_view lhs, std::string_view rhs)
{
    return std::ranges::equal(lhs, rhs, [](const unsigned char &a, const unsigned char &b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

std::vector<std::filesystem::path> getAllFiles(const std::filesystem::path &root)
{
    if (!std::filesystem::exists(root))
        throw std::runtime_error("Balls!");

    const std::unordered_set<std::string> &vExtensions{
        ".afc",
        ".bik",
        ".bin",
        ".cnd",
        ".pcc",
        ".upk",
        ".tfc",
        ".tlk",
        ".txt",
#if LEGENDARY_EDITION
        ".dlc",
        ".ini",
        ".isb",
        ".usf",
#endif
    };

    std::vector<std::filesystem::path> result;

    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::none, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
    {
        if (ec)
        {
            std::println(stderr, "Error: Failed parsing {}", it->path().string());

            ec.clear();

            continue;
        }

        const std::filesystem::directory_entry &de = *it;

        // NOTE: Skip any non-regular files
        if (!de.is_regular_file(ec))
        {
            ec.clear();

            continue;
        }

        // NOTE: Skip any stray file named after the TOC binary index
        if (iequals(de.path().filename().string(), "PCConsoleTOC.bin"))
        {
            ec.clear();

            continue;
        }

        std::string ext = de.path().extension().string();

        #ifndef _WIN32
        // NOTE: Do case insensitive match on Unix-type (ext4, btrfs, bcachefs) filesystems
        std::transform(ext.begin(), ext.end(), ext.begin(), [](const unsigned char &c) { return std::tolower(c); });
        #endif

        if (vExtensions.contains(ext))
            result.push_back(de.path());
    }

    return result;
}

void printHelp(int, char *argv[])
{
    assert(argv != NULL);

    const std::filesystem::path &p = argv[0];
    const std::string &s = p.filename();

    std::println("{} PATH_TO_PCConsoleTOC.bin", s);
}

constexpr int32_t g_nTOCMagic = 0x3AB70C13;

/** Table size calculation notes
  *
  * Assume minimum size: file count / 2
  *
  * Document me...
  *
  */

/** Format specification
  *
  * uint32_t magic
  * uint32_t reserved
  * int32_t calculated table size
  *
  */

#pragma pack(push,1)
struct STOCEntry
{
    int32_t size = 0; // Size of the file
    uint32_t hash = 0; // UE3 hash of the filename
    std::array<uint8_t, 20> sha1 = {0}; // NOTE: Not used
    std::string name = {}; // Full file name including path, relative to the base game directory (i.e. BIOGame/DLC/DLC_HyperFun/...)
};

struct STOCHeader
{
    uint8_t magic[4] = {0x13, 0x0C, 0xB7, 0x3A};

    uint32_t reserved1 = 0;

    uint32_t tableSize = 0;

    uint32_t r1 = 0;
    uint32_t r2 = 0;

    // Entries...
};
#pragma pack(pop)

void writeS32(std::fstream &fs, const int32_t &value)
{
    fs.write(reinterpret_cast<const char *>(&value), sizeof(value));
}

void writeU16(std::fstream &fs, const uint16_t &value)
{
    fs.write(reinterpret_cast<const char *>(&value), sizeof(value));
}

void writeU8(std::fstream &fs, const uint8_t &value)
{
    fs.write(reinterpret_cast<const char *>(&value), sizeof(value));
}

void writeBuffer(std::fstream &fs, const void *buf, const uint32_t &len)
{
    fs.write(reinterpret_cast<const char *>(buf), len);
}

template <typename _Type, std::size_t _Size>
void writeArray(std::fstream &fs, const std::array<_Type, _Size> &arr)
{
    const std::size_t &len = arr.size() * sizeof(_Type);

    // NOTE: The only place where this is used for now should be a SHA-1 of 20 bytes
    assert(len == 20);

    fs.write(reinterpret_cast<const char *>(arr.data()), len);
}

void writeEntries(std::fstream &fs, const std::vector<STOCEntry> &files)
{
    int32_t tableSize = files.size();
    const int32_t minTableSize = tableSize / 2;

    std::set<uint32_t> uniques;

    while (true)
    {
        for (auto file_data: files)
        {
            uniques.insert(file_data.hash % tableSize);
        }

        // NOTE: Cast to silence annoying signedness warning
        if (static_cast<std::size_t>(tableSize) - uniques.size() <= static_cast<std::size_t>(tableSize / 4))
        {
            std::println(stderr, "Table size calculation break");

            break;
        }

        // NOTE: Reduce by 1/4 and check against minimum table size
        tableSize -= tableSize / 4;

        if (tableSize <= minTableSize)
        {
            std::println(stderr, "Table size calculation now at or lower than minimum table size");

            break;
        }

        uniques.clear();
    }

    std::vector<
        std::vector<STOCEntry>
    > buckets(tableSize);

    for (const auto &file_data: files)
    {
        buckets[file_data.hash % tableSize].emplace_back(file_data);
    }

    // NOTE: Write header
    writeS32(fs, g_nTOCMagic);
    writeS32(fs, 0);

    writeS32(fs, tableSize);

    int32_t entryPos = tableSize * 8;
    int32_t tablePos = 0;

    for (std::size_t i = 0; i < buckets.size(); ++i)
    {
        const auto &bucket = buckets[i];

        if (bucket.empty())
        {
            // NOTE: Offset
            writeS32(fs, 0);

            // NOTE: Number of entries
            writeS32(fs, 0);
        }
        else
        {
            writeS32(fs, entryPos - tablePos);
            writeS32(fs, bucket.size());

            for (const auto &file_data: bucket)
            {
                const int filePathLength = file_data.name.length();
                assert(filePathLength < std::numeric_limits<uint16_t>::max());

                // NOTE: The size of the entry -- entry header plus the total path's length including the null character
                uint16_t entryLength = uint16_t(28 + filePathLength + 1);

#ifdef LEGENDARY_EDITION
                entryLength += (4 - entryLength % 4) % 4;
#endif

                entryPos += entryLength;
            }
        }

        tablePos += 8;
    }

    std::streampos lastPos = 0;

    for (std::size_t i = 0; i < buckets.size(); ++i)
    {
        const auto &bucket = buckets[i];
        for (const auto &file_data: bucket)
        {
            const int filePathLength = file_data.name.length();

            // NOTE: The size of the entry -- entry header plus the total path's length including the null character
            uint16_t entryLength = uint16_t(28 + filePathLength + 1);

#ifdef LEGENDARY_EDITION
            // NOTE: Calculate padding needed for a 4 byte boundary alignment
            entryLength += (4 - entryLength % 4) % 4;
#endif

            lastPos = fs.tellp();

            // NOTE: Write (uint16_t) entry size and padding
            writeU16(fs, entryLength);
            writeU16(fs, 0);

            writeS32(fs, file_data.size);

            // NOTE: Write unused 20 byte SHA-1
            writeArray(fs, file_data.sha1);

            // NOTE: Write the path including its NULL terminating char
            writeBuffer(fs, file_data.name.c_str(), filePathLength + 1);

#ifdef LEGENDARY_EDITION
            for (; pad > 0; --pad)
            {
                writeU8(fs, 0);
            }
#endif
        }
    }

#if 0
    // NOTE: The last entry should not have a size
    if (lastPos > 0)
    {
        toc.seekp(lastPos);

        writeU16(fs, 0);
    }
#endif
}

std::optional<std::filesystem::path> remove_prefix_if_present(const std::filesystem::path &p, const std::filesystem::path &prefix)
{
    auto pit = p.begin();
    auto kit = prefix.begin();

    for (; kit != prefix.end(); ++kit, ++pit)
    {
        if (pit == p.end())
            return std::nullopt;

        if (*pit != *kit)
            return std::nullopt;
    }

    std::filesystem::path result;

    for (; pit != p.end(); ++pit)
        result /= *pit;

    return result;
}

std::string getBIOFileName(const std::filesystem::path &name)
{
    // FIXME: This is all trash.

    auto tmp = name;
    const std::string &s = tmp.make_preferred().string();

    const auto &bioPos = s.find("BIOGame/");
    if (bioPos == std::string::npos)
        throw std::runtime_error("Oh noes, that's a fucky-wucky");

    auto bioPath = s.substr(bioPos);

#ifndef _WIN32
    std::replace(bioPath.begin(), bioPath.end(), '/', '\\');
#endif

    return bioPath;
}

inline STOCEntry fsToEntry(const std::filesystem::path &file)
{
    const auto &filename = getBIOFileName(file);

    return {
        .size = static_cast<int32_t>(std::filesystem::file_size(file)), // NOTE: Cast to silence conversion warning
        .hash = tocfix::hashString(filename),
        .name = filename,
    };
}

void listBaseFiles(const std::filesystem::path &root, std::vector<STOCEntry> &entries)
{
    const std::unordered_set<std::string> &vSkipPaths{
        "DLC",
        "Patches",
        "Splash",
#if LEGENDARY_EDITION
        "Config",
#endif
    };

    const auto &tmpTxt = root / "PCConsoleTOC.txt";
    if (std::filesystem::exists(tmpTxt))
    {
        entries.emplace_back(fsToEntry(tmpTxt));
    }

    for (const auto &entry: std::filesystem::directory_iterator(root))
    {
        if (!entry.is_directory())
        {
            continue;
        }

        std::string name = entry.path().filename().string();

        if (vSkipPaths.contains(name))
        {
            std::println(stderr, "Skipping directory {}", name);

            continue;
        }

        for (const auto &it: getAllFiles(entry))
        {
            std::println("+ {}", it.string());

            entries.emplace_back(fsToEntry(it));
        }
    }
}

void listDLCFiles(const std::filesystem::path &root, std::vector<STOCEntry> &entries)
{
    for (const auto &entry: std::filesystem::directory_iterator(root))
    {
        if (!entry.is_directory())
            continue;

        std::string name = entry.path().filename().string();
        if (name.rfind("DLC_", 0) != 0)
            continue;

        std::println("DLC: {}", entry.path().c_str());

        for (const auto &it: getAllFiles(entry))
        {
            std::println("+ {}", it.string());

            entries.emplace_back(fsToEntry(it));
        }
    }
}

int main(int argc, char *argv[])
{
    // Registry path: HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\BioWare\Mass Effect 3
    // Registry key: Install Dir

    if (argc < 2) {
        printHelp(argc, argv);

        return EXIT_FAILURE;
    }

    const std::string &sPath = argv[1];

    std::filesystem::path pTOCPath = sPath;

    if (pTOCPath.filename() != "PCConsoleTOC.bin") {
        std::println(stderr, "Input argument must be the root path to PCConsoleTOC.bin");

        return EXIT_FAILURE;
    }

    std::error_code ec;
    std::filesystem::path pBasePath, pBIOPath, pTOCPathBackup;

    pBasePath = pTOCPath.parent_path().parent_path();
    pBIOPath = pBasePath / "BIOGame";

    std::filesystem::path pDLCPath = pBIOPath / "DLC";

    std::println(stderr, "Base path: {}", pBasePath.string());
    std::println(stderr, "BIOGame path: {}", pBIOPath.string());
    std::println(stderr, "DLC path: {}", pDLCPath.string());

    pTOCPathBackup = pTOCPath;
    pTOCPathBackup += std::filesystem::path(".bak").stem();

    std::filesystem::rename(pTOCPath, pTOCPathBackup, ec);
    if (ec) {
        std::println(stderr, "Failed to rename original TOC file");

        return EXIT_FAILURE;
    }

    std::fstream fs;

    fs.open(pTOCPath, std::fstream::out | std::fstream::binary | std::fstream::trunc);

    // NOTE: Legendary Edition 1 stores DLC TOC separately
#ifdef LEGENDARY_EDITION
    std::vector<STOCEntry> vBaseEntries;
    std::vector<STOCEntry> vDLCEntries;

    vBaseEntries.reserve(3958); // NOTE: Reserve enough for a base game install with all DLC

    std::println(stderr, "Listing base game files:");
    listBaseFiles(pBIOPath, vBaseEntries);

    writeEntries(fs, vBaseEntries);

    std::println(stderr, "Listing DLC files:");
    listDLCFiles(pDLCPath, vDLCEntries);

    writeEntries(fs, vDLCEntries);
#else
    std::vector<STOCEntry> vEntries;

    vEntries.reserve(3958); // NOTE: Reserve enough for a base game install with all DLC

    std::println(stderr, "Listing base game files:");
    listBaseFiles(pBIOPath, vEntries);

    std::println(stderr, "Listing DLC files:");
    listDLCFiles(pDLCPath, vEntries);

    writeEntries(fs, vEntries);
#endif

    fs.close();

    return EXIT_SUCCESS;
}
