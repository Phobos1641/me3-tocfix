// ini_parser.hpp -- a tiny case-insensitive INI reader.

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace auto_toc {

class IniFile {
public:
    [[nodiscard]] static IniFile load(const std::filesystem::path& path);
    [[nodiscard]] static IniFile parse(std::string_view content);

    [[nodiscard]] std::optional<std::string>
    get_value(std::string_view section, std::string_view key) const;

private:
    struct CaseInsensitiveLess {
        using is_transparent = void;
        [[nodiscard]] bool operator()(std::string_view a,
                                      std::string_view b) const noexcept;
    };

    using KeyValueMap = std::map<std::string, std::string, CaseInsensitiveLess>;
    using SectionMap  = std::map<std::string, KeyValueMap, CaseInsensitiveLess>;

    SectionMap sections_;
};

} // namespace auto_toc
