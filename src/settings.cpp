#include "settings.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <vector>

using namespace ftxui;

// CSS THEME PARSER WITH 16 ANSI COLOR SUPPORT
//
//

namespace {
    std::string trimStr(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n;{}");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n;{}");
        return s.substr(start, end - start + 1);
    }

    std::string lowerStr(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    // Full ANSI 16 + RGB Hex Parser
    Color parseCssColor(std::string str) {
        str = lowerStr(trimStr(str));
        if (str.empty()) return Color::Default;

        // ANSI Standard 16 Colors
        if (str == "default" || str == "none" || str == "transparent") return Color::Default;
        if (str == "black") return Color::Black;
        if (str == "red") return Color::Red;
        if (str == "green") return Color::Green;
        if (str == "yellow") return Color::Yellow;
        if (str == "blue") return Color::Blue;
        if (str == "magenta") return Color::Magenta;
        if (str == "cyan") return Color::Cyan;
        if (str == "white") return Color::White;
        if (str == "gray" || str == "grey" || str == "graydark" || str == "darkgray") return Color::GrayDark;
        if (str == "lightgray" || str == "graylight") return Color::GrayLight;

        // ANSI Bright Colors
        if (str == "brightred" || str == "redbright") return Color::RedLight;
        if (str == "brightgreen" || str == "greenbright") return Color::GreenLight;
        if (str == "brightyellow" || str == "yellowbright") return Color::YellowLight;
        if (str == "brightblue" || str == "bluebright") return Color::BlueLight;
        if (str == "brightmagenta" || str == "magentabright") return Color::MagentaLight;
        if (str == "brightcyan" || str == "cyanbright") return Color::CyanLight;
        if (str == "brightwhite" || str == "whitebright") return Color::White;

        // Hex Colors (#RGB and #RRGGBB)
        if (str[0] == '#') {
            std::string hex = str.substr(1);
            try {
                if (hex.size() == 6) {
                    int r = std::stoi(hex.substr(0, 2), nullptr, 16);
                    int g = std::stoi(hex.substr(2, 2), nullptr, 16);
                    int b = std::stoi(hex.substr(4, 2), nullptr, 16);
                    return Color::RGB(r, g, b);
                } else if (hex.size() == 3) {
                    int r = std::stoi(std::string(2, hex[0]), nullptr, 16);
                    int g = std::stoi(std::string(2, hex[1]), nullptr, 16);
                    int b = std::stoi(std::string(2, hex[2]), nullptr, 16);
                    return Color::RGB(r, g, b);
                }
            } catch (...) {}
        } else if (str.find("rgb(") == 0 && str.back() == ')') {
            std::string inner = str.substr(4, str.size() - 5);
            std::stringstream ss(inner);
            int r = 0, g = 0, b = 0;
            char c1, c2;
            if (ss >> r >> c1 >> g >> c2 >> b) {
                return Color::RGB(r, g, b);
            }
        }
        return Color::Default;
    }

    std::optional<ColorPalette> loadCssThemeFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) return std::nullopt;

        ColorPalette palette;
        std::string line;

        while (std::getline(file, line)) {
            line = trimStr(line);
            if (line.empty() || line.find("/*") == 0 || line.find("//") == 0 || line == ":root") continue;

            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;

            std::string key = lowerStr(trimStr(line.substr(0, colon)));
            std::string val = trimStr(line.substr(colon + 1));

            if (key.rfind("--", 0) == 0) {
                key = key.substr(2);
            }

            Color c = parseCssColor(val);

            // Global & Structural
            if (key == "background" || key == "bg") palette.background = c;
            else if (key == "panel-border" || key == "border") palette.panelBorder = c;
            else if (key == "text-primary" || key == "text") palette.textPrimary = c;
            else if (key == "text-dim" || key == "dim") palette.textDim = c;
            else if (key == "footer-text" || key == "footer") palette.footerText = c;

            // Header
            else if (key == "header-title") palette.headerTitle = c;
            else if (key == "header-bpm") palette.headerBpm = c;
            else if (key == "header-timesig") palette.headerTimeSig = c;
            else if (key == "header-clock") palette.headerClock = c;
            else if (key == "header-status") palette.headerStatus = c;

            // Channel Table
            else if (key == "table-header") palette.tableHeader = c;
            else if (key == "channel-active" || key == "active") palette.channelActive = c;
            else if (key == "channel-inactive" || key == "inactive") palette.channelInactive = c;

            // VU Meter
            else if (key == "vu-low") palette.vuLow = c;
            else if (key == "vu-mid") palette.vuMid = c;
            else if (key == "vu-high") palette.vuHigh = c;
            else if (key == "vu-peak") palette.vuPeakMarker = c;

            // FX & Master
            else if (key == "fx-label") palette.fxLabel = c;
            else if (key == "fx-value") palette.fxValue = c;
            else if (key == "master-label") palette.masterLabel = c;
            else if (key == "master-value") palette.masterValue = c;

            // Event Log
            else if (key == "log-sysex") palette.logSysEx = c;
            else if (key == "log-pc") palette.logPc = c;
            else if (key == "log-cc") palette.logCc = c;
            else if (key == "log-tempo") palette.logTempo = c;
            else if (key == "log-timesig") palette.logTimeSig = c;

            // Activity Grid
            else if (key == "activity-badge") palette.activityBadge = c;
            else if (key == "activity-chord") palette.activityChord = c;

            // System Load
            else if (key == "load-poly") palette.loadPoly = c;
            else if (key == "load-ram") palette.loadRam = c;
            else if (key == "load-cpu") palette.loadCpu = c;
        }

        return palette;
    }
}

ColorPalette Settings::palette() const {
    std::vector<std::string> searchPaths = {
        "src/themes/" + theme + ".css",
        "themes/" + theme + ".css",
        "../src/themes/" + theme + ".css",
        "../themes/" + theme + ".css",
        theme + ".css"
    };

    for (const auto& path : searchPaths) {
        if (auto cssPalette = loadCssThemeFile(path)) {
            return *cssPalette;
        }
    }

    return ColorPalette{};
}

std::string toString(NumeralFormat format) {
    return format == NumeralFormat::Hex ? "hex" : "decimal";
}

std::string toString(ModuleOverride mod) {
    switch (mod) {
        case ModuleOverride::SD90: return "sd-90";
        case ModuleOverride::GS:   return "gs";
        case ModuleOverride::XG:   return "xg";
        case ModuleOverride::GM2:  return "gm2";
        case ModuleOverride::Auto:
        default:                  return "auto";
    }
}

std::optional<NumeralFormat> parseNumeralFormat(const std::string& s) {
    std::string v = lowerStr(s);
    if (v == "decimal" || v == "dec") return NumeralFormat::Decimal;
    if (v == "hex" || v == "hexadecimal") return NumeralFormat::Hex;
    return std::nullopt;
}

std::optional<ModuleOverride> parseModuleOverride(const std::string& s) {
    std::string v = lowerStr(s);
    if (v == "auto")                      return ModuleOverride::Auto;
    if (v == "sd-90" || v == "sd90")       return ModuleOverride::SD90;
    if (v == "gs")                         return ModuleOverride::GS;
    if (v == "xg")                         return ModuleOverride::XG;
    if (v == "gm2")                        return ModuleOverride::GM2;
    return std::nullopt;
}

// TOML PARSER
//
//

namespace {
    std::string stripQuotes(std::string s) {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
            return s.substr(1, s.size() - 2);
        }
        return s;
    }

    std::unordered_map<std::string, std::string> parseFlatToml(std::istream& in) {
        std::unordered_map<std::string, std::string> out;
        std::string line;
        std::string section;

        while (std::getline(in, line)) {
            std::string trimmed = trimStr(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (trimmed.front() == '[' && trimmed.back() == ']') {
                section = trimmed.substr(1, trimmed.size() - 2);
                continue;
            }

            size_t eq = trimmed.find('=');
            if (eq == std::string::npos) continue;

            std::string key = trimStr(trimmed.substr(0, eq));
            std::string val = stripQuotes(trimStr(trimmed.substr(eq + 1)));

            std::string fullKey = section.empty() ? key : (section + "." + key);
            out[fullKey] = val;
        }

        return out;
    }

    bool parseBool(const std::string& s, bool fallback) {
        std::string v = lowerStr(s);
        if (v == "true" || v == "1" || v == "yes") return true;
        if (v == "false" || v == "0" || v == "no") return false;
        return fallback;
    }

    template <typename T>
    T parseNumber(const std::string& s, T fallback) {
        try {
            if constexpr (std::is_integral_v<T>) {
                return static_cast<T>(std::stol(s));
            } else {
                return static_cast<T>(std::stod(s));
            }
        } catch (...) {
            return fallback;
        }
    }
}

Settings loadSettings(const std::string& path) {
    Settings settings;
    
    std::vector<std::string> configPaths = {
        path,
        "src/" + path,
        "../" + path,
        "../src/" + path
    };

    std::ifstream file;
    for (const auto& p : configPaths) {
        file.open(p);
        if (file.is_open()) break;
    }

    if (!file.is_open()) return settings;

    auto kv = parseFlatToml(file);

    auto get = [&](const std::string& key) -> std::optional<std::string> {
        auto it = kv.find(key);
        if (it == kv.end()) return std::nullopt;
        return it->second;
    };

    if (auto v = get("display.theme")) {
        settings.theme = *v;
    }
    if (auto v = get("display.numeral_format")) {
        if (auto parsed = parseNumeralFormat(*v)) settings.numeralFormat = *parsed;
    }
    if (auto v = get("display.module_override")) {
        if (auto parsed = parseModuleOverride(*v)) settings.moduleOverride = *parsed;
    }
    if (auto v = get("display.show_event_log")) settings.showEventLog = parseBool(*v, settings.showEventLog);
    if (auto v = get("display.show_activity_grid")) settings.showActivityGrid = parseBool(*v, settings.showActivityGrid);
    if (auto v = get("display.log_max_visible")) settings.logMaxVisible = parseNumber<int>(*v, settings.logMaxVisible);

    if (auto v = get("meter.highlight_timeout_ms")) settings.highlightTimeoutMs = parseNumber<double>(*v, settings.highlightTimeoutMs);
    if (auto v = get("meter.note_off_grace_ms")) settings.noteOffGraceMs = parseNumber<double>(*v, settings.noteOffGraceMs);
    if (auto v = get("meter.vu_bar_width")) settings.vuBarWidth = parseNumber<int>(*v, settings.vuBarWidth);
    if (auto v = get("meter.show_peak_marker")) settings.showPeakMarker = parseBool(*v, settings.showPeakMarker);

    return settings;
}

// TOML WRITER
//
//

bool saveSettings(const Settings& settings, const std::string& path) {
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return false;

    file << "# MIDI Visualizer Configuration\n\n";

    file << "[display]\n";
    file << "theme = \"" << settings.theme << "\"\n";
    file << "numeral_format = \"" << toString(settings.numeralFormat) << "\"\n";
    file << "module_override = \"" << toString(settings.moduleOverride) << "\"\n";
    file << "show_event_log = " << (settings.showEventLog ? "true" : "false") << "\n";
    file << "show_activity_grid = " << (settings.showActivityGrid ? "true" : "false") << "\n";
    file << "log_max_visible = " << settings.logMaxVisible << "\n\n";

    file << "[meter]\n";
    file << "highlight_timeout_ms = " << settings.highlightTimeoutMs << "\n";
    file << "note_off_grace_ms = " << settings.noteOffGraceMs << "\n";
    file << "vu_bar_width = " << settings.vuBarWidth << "\n";
    file << "show_peak_marker = " << (settings.showPeakMarker ? "true" : "false") << "\n";

    return true;
}