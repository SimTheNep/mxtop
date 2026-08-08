#include "menu_ui.hpp"

#include <ftxui/component/event.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <thread>

using namespace ftxui;

// MENU_HELPERS.CPP
//
// Utils, file scanners, auto-complete, and string formatting

// Pick a random Minecraft-style splash text
std::string getRandomSplashText() {
    static const std::vector<std::string> kSplashes = {
        // Shoutouts
        "Also try btop!",
        "Also try TMIDI Player!",
        "Also try FluidSynth for soundfonts!",

        // Release notes
        "Now with 100% more SysEx!",
        "Removed MS-DOS!",

        // Protocol technical stuff
        "0xF0 ... 0xF7!",
        "16 channels and beyond!",
        "Channel 10 is always percussion!",
        "Note On with 0 velocity is just Note Off!",
        "Hexadecimal is my favorite format!",

        // Hardware & standards
        "XG, GS, and GM2 approved!",
        "SysEx included, synths sold separately!",
        "Don't feed CC04 after midnight!",
        "ALSA is typing...",

        // Memes
        "Black MIDI compatible! (Mostly...)",
        "Polyphony count goes brrr!",
        "Have you changed your theme today?",
        "Powered by FTXUI & RtMidi!"
    };

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, kSplashes.size() - 1);
    return kSplashes[dist(gen)];
}

// Scan for .css files in theme directory
std::vector<std::string> getThemes() {
    std::set<std::string> themesSet;
    
    std::vector<std::string> searchPaths = {
        "src/themes",
        "themes",
        "../src/themes",
        "../themes",
        "../../src/themes"
    };

    for (const auto& path : searchPaths) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && std::filesystem::is_directory(path, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
                if (entry.is_regular_file(ec) && entry.path().extension() == ".css") {
                    themesSet.insert(entry.path().stem().string());
                }
            }
        }
    }

    if (themesSet.empty()) {
        return { "default", "base" };
    }

    return std::vector<std::string>(themesSet.begin(), themesSet.end());
}

// Scan for module folders in modules directory
std::vector<ModuleOverride> getModules() {
    std::vector<ModuleOverride> result = { ModuleOverride::Auto };
    std::set<ModuleOverride> foundMods;

    std::vector<std::string> searchPaths = {
        "modules",
        "../modules",
        "../../modules",
        "src/modules",
        "../src/modules"
    };

    for (const auto& path : searchPaths) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && std::filesystem::is_directory(path, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
                if (entry.is_directory(ec) || std::filesystem::exists(entry.path() / "module.json", ec)) {
                    std::string folderName = entry.path().filename().string();
                    if (auto mod = parseModuleOverride(folderName)) {
                        foundMods.insert(*mod);
                    }
                }
            }
        }
    }

    // Fallback if directory is not found
    if (foundMods.empty()) {
        return { ModuleOverride::Auto, ModuleOverride::SD90, ModuleOverride::GS, ModuleOverride::XG, ModuleOverride::GM2 };
    }

    for (auto mod : foundMods) {
        if (mod != ModuleOverride::Auto) {
            result.push_back(mod);
        }
    }

    return result;
}

// Reads the first /* comment block */ at the top of a .css theme file as a tooltip description
std::string getThemeDesc(const std::string& themeName) {
    std::vector<std::string> searchPaths = {
        "src/themes/" + themeName + ".css",
        "themes/" + themeName + ".css",
        "../src/themes/" + themeName + ".css",
        "../themes/" + themeName + ".css",
        "../../src/themes/" + themeName + ".css"
    };

    for (const auto& path : searchPaths) {
        std::ifstream file(path);
        if (!file.is_open()) continue;

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        size_t start = content.find("/*");
        if (start != std::string::npos) {
            size_t end = content.find("*/", start + 2);
            if (end != std::string::npos) {
                std::string desc = content.substr(start + 2, end - (start + 2));
                
                // Trim leading/trailing comment indicators
                size_t first = desc.find_first_not_of(" \t\r\n*");
                size_t last = desc.find_last_not_of(" \t\r\n*");
                if (first != std::string::npos && last != std::string::npos) {
                    desc = desc.substr(first, last - first + 1);

                    if (!desc.empty()) return desc;
                }
            }
        }
    }

    return "CSS visual palette preset"; // Fallback default
}

// Lower string
std::string lowerStr(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Autocomplete + ghost
std::string autoComplete(const std::string& input) {
    if (input.empty()) return "";

    std::filesystem::path p(input);
    std::filesystem::path dir;
    std::string prefix;

    // Handle trailing slashes when entering directories
    if (input.back() == '/' || input.back() == '\\') {
        dir = p;
        prefix = "";
    } else {
        dir = p.has_parent_path() ? p.parent_path() : ".";
        prefix = p.filename().string();
    }

    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) return "";

    std::string lowerPrefix = lowerStr(prefix);

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        std::string name = entry.path().filename().string();
        std::string lowerName = lowerStr(name);

        // Case-insensitive
        if (lowerName.rfind(lowerPrefix, 0) == 0 && name.size() >= prefix.size()) {
            std::string suffix = name.substr(prefix.size());
            if (entry.is_directory(ec) && (suffix.empty() || suffix.back() != '/')) {
                suffix += "/";
            }
            if (!suffix.empty()) return suffix;
        }
    }
    return "";
}

// Clears the word back to the previous /
void clearWord(std::string& str) {
    if (str.empty()) return;

    // Trim slashes/spaces
    while (!str.empty() && (str.back() == '/' || str.back() == '\\' || str.back() == ' ')) {
        str.pop_back();
    }

    size_t pos = str.find_last_of("/\\ ");
    if (pos == std::string::npos) {
        str.clear();
    } else {
        str = str.substr(0, pos + 1);
    }
}

// Parses HEX into port index
int charToPort(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

// Formats port lists into clean ranges (e.g. "P1–P16 (All)" or "P1–P6, P9–P12")
std::string formatPorts(const std::vector<unsigned int>& ports) {
    if (ports.empty()) return "None";
    if (ports.size() == 16) return "P1–P16 (All)";

    std::vector<std::string> ranges;
    size_t i = 0;
    while (i < ports.size()) {
        size_t start = i;
        while (i + 1 < ports.size() && ports[i + 1] == ports[i] + 1) {
            i++;
        }
        if (i > start) {
            ranges.push_back("P" + std::to_string(ports[start] + 1) + "–P" + std::to_string(ports[i] + 1));
        } else {
            ranges.push_back("P" + std::to_string(ports[start] + 1));
        }
        i++;
    }

    std::string result;
    for (size_t r = 0; r < ranges.size(); ++r) {
        result += ranges[r] + (r + 1 < ranges.size() ? ", " : "");
    }
    return result;
}

// Get double timestamp in seconds for animations
double getTimeSec() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now.time_since_epoch()).count();
}

// Applies typewriter CRT scanline reveal transition to list of rows
Element scanlineTransition(Elements rows, double switchTime) {
    double elapsed = getTimeSec() - switchTime;
    double duration = 0.22; // 220 ms typewriter sweep

    if (elapsed >= duration || rows.empty()) {
        return vbox(std::move(rows));
    }

    double progress = std::clamp(elapsed / duration, 0.0, 1.0);
    size_t visibleCount = static_cast<size_t>(std::ceil(progress * rows.size()));

    Elements visibleRows;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i < visibleCount) {
            visibleRows.push_back(std::move(rows[i]));
        } else {
            visibleRows.push_back(text("") | size(HEIGHT, EQUAL, 1));
        }
    }

    return vbox(std::move(visibleRows));
}