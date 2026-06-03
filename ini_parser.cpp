#include "ini_parser.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace auto_toc {

namespace {

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

} // anonymous namespace

bool IniFile::CaseInsensitiveLess::operator()(std::string_view a,
                                              std::string_view b) const noexcept {
    const auto n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const auto ca = std::tolower(static_cast<unsigned char>(a[i]));
        const auto cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return ca < cb;
    }
    return a.size() < b.size();
}

IniFile IniFile::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Failed to open INI file: " + path.string());
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return parse(ss.str());
}

IniFile IniFile::parse(std::string_view content) {
    IniFile ini;
    std::string current_section;
    bool in_section = false;

    std::size_t pos = 0;
    while (pos <= content.size()) {
        auto eol = content.find('\n', pos);
        if (eol == std::string_view::npos) eol = content.size();
        const auto line = trim(content.substr(pos, eol - pos));
        if (eol == content.size()) {
            // Process this last line, then break.
            if (!line.empty()) {
                if (line.front() == '[' && line.back() == ']') {
                    current_section = std::string{line.substr(1, line.size() - 2)};
                    in_section = true;
                    ini.sections_.try_emplace(current_section);
                } else if (in_section) {
                    if (const auto eq = line.find('='); eq != std::string_view::npos) {
                        auto key   = std::string{trim(line.substr(0, eq))};
                        auto value = std::string{trim(line.substr(eq + 1))};
                        ini.sections_[current_section][std::move(key)] = std::move(value);
                    }
                }
            }
            break;
        }
        pos = eol + 1;

        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']') {
            current_section = std::string{line.substr(1, line.size() - 2)};
            in_section = true;
            ini.sections_.try_emplace(current_section);
            continue;
        }

        if (!in_section) continue; // Lines outside any section are ignored.

        const auto eq = line.find('=');
        if (eq == std::string_view::npos) continue;

        auto key   = std::string{trim(line.substr(0, eq))};
        auto value = std::string{trim(line.substr(eq + 1))};
        ini.sections_[current_section][std::move(key)] = std::move(value);
    }
    return ini;
}

std::optional<std::string>
IniFile::get_value(std::string_view section, std::string_view key) const {
    const auto sit = sections_.find(section);
    if (sit == sections_.end()) return std::nullopt;
    const auto kit = sit->second.find(key);
    if (kit == sit->second.end()) return std::nullopt;
    return kit->second;
}

} // namespace auto_toc
