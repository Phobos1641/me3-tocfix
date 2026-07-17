# ME3 TOCFix

A modern C++ tool that regenerates the `PCConsoleTOC.bin` lookup tables shipped with the
original Mass Effect 3 and the three Mass Effect Legendary Edition games.
Recomputing these is what allows file-replacement mods (DLC mods, DLC_MOD_*, etc.) to
take effect -- the games won't load files they don't see in the TOC.

The tool supports four games:

| Argument | Game                                    |
| -------- | --------------------------------------- |
| `ME3`    | Mass Effect 3 (original 2012 release)   |
| `LE1`    | Mass Effect Legendary Edition — ME1     |
| `LE2`    | Mass Effect Legendary Edition — ME2     |
| `LE3`    | Mass Effect Legendary Edition — ME3     |

## Download

GitHub)
  Head over to [Releases](/releases) and download the latest release matching your platform.

GitLab)
  Head over to [Releases](https://gitgud.io/orochi/mods/mass-effect/me3-tocfix/-/releases) and download the latest release matching your platform.

## Building

Requires a C++20 compiler (GCC 11+, Clang 13+, or MSVC 2019 16.10+) and Meson **or** CMake 3.20+.

```bash
meson setup _build -Ddefault_library=static
meson compile -C _build
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

On Linux you can also build directly without CMake:

```bash
g++ -std=c++20 -O2 -pthread \
    src/main.cpp src/ini_parser.cpp src/toc_bin_file.cpp src/toc_creator.cpp \
    -o me3tocfix
```

## Usage

```shell
me3tocfix <path-to-game-exe>
me3tocfix -r <ME3|LE1|LE2|LE3> (Windows/Wine only registry lookup)
```

For example via providing the absolute path:

```shell
me3tocfix "D:\Origin Games\Mass Effect Legendary Edition\Game\ME3\Binaries\Win64\MassEffect3.exe"
```

Or via the Windows registry:

```shell
me3tocfix -r LE3
```

`me3tocfix` will write a fresh `PCConsoleTOC.bin` to the BioGame directory and
to every DLC subfolder (or, for LE1, only to BioGame, since LE1 folds all DLC
content into a single basegame TOC by mount priority).

## Notable changes vs. the C# project

- **CRC table built at compile time.** Original lazy-init had a non-atomic
  null-check race when called from `Task.WhenAll`. Now it's `constexpr`, baked
  into the binary, and trivially thread-safe.
- **`std::async` instead of `Task.WhenAll`.** Same fan-out parallelism for
  basegame + per-DLC jobs.
- **Prime-rounded hash-table sizes.** `hash % T` distributes badly when `T`
  shares factors with patterns in the input — powers of 2 are pathological,
  and round numbers like 100 cluster too. We snap the chosen `T` down to the
  nearest prime, which costs a few primality trials at build time and gives
  tighter chains for free. The C# project uses `T = N` directly and gets
  lucky or unlucky depending on `N`'s factorization.
- **Defensive name-length check.** Each TOC entry stores its serialized
  length in an int16, capping name length at ~32 KiB. Real ME / LE filenames
  are nowhere near that, but we now reject names that would overflow the
  field rather than silently producing a corrupt TOC.
- **2-GiB warning.** The C# version silently casts file sizes to `int`, which
  produces a corrupt TOC for files ≥ 2 GiB. We warn loudly when this happens.
- **Cross-platform core, including version detection.** The TOC algorithm
  itself runs on Linux/macOS, which is convenient for headless build
  pipelines. The C# project version's `MassEffect3.exe` → ME3 vs LE3
  disambiguation relied on the Win32 `GetFileVersionInfo` API, leaving
  non-Windows callers blind; we now parse the PE format directly to read
  the FileVersion resource, so detection works the same on every platform.
  Only the registry fallback (`-r` flag) remains Windows-only.
- **Trimmed-down INI parser.** AutoTOC only ever reads one value out of a DLC
  `autoload.ini` (the `ModMount` priority); we ship a minimal read-only parser
  instead of porting the full `DuplicatingIni` and its supporting types.
- **Correct file write semantics.** Original `DuplicatingIni.WriteToFile`
  never disposed/flushed its `StreamWriter`, which can produce truncated INI
  files. We don't write INIs at all here, but the TOC writer uses RAII to
  guarantee proper flush + close.
- **Cleaner serialiser.** No `MemoryStream.Seek` games — we build the TOC into
  a `std::vector<uint8_t>` and patch the hash-table slots in place once each
  bucket's offset is known, which makes the on-disk layout much easier to
  audit against the format.
- **Better error messages.** The C# project `CreateDLCTOC` swallows every
  exception as "may just be packed DLC". We still don't fail the whole run on
  one bad DLC, but we surface the actual error so genuine bugs aren't hidden.
- **Robust missing-directory handling.** `Directory.GetFiles` throws on
  missing input; `std::filesystem::directory_iterator` with `error_code`
  doesn't, so we degrade gracefully when, say, `BIOGame/Movies` doesn't exist.

## Credits

* AutoTOC from the ME3Tweaks project.

This project is not affiliated in any way with any of the above mentioned projects other than to acknowledge its source of inspiration.

## License

[GNU Affero General Public License v3.0 only](/LICENSE)
