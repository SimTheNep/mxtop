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

// HELPERS
//
//

namespace {
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
}

// RUNTIME
//
//

menuUi::menuUi(const std::vector<std::string>& availablePorts, Settings settings)
    : availablePorts_(availablePorts), settings_(std::move(settings)), splashText_(getRandomSplashText()) {}

menuReturn menuUi::run() {
    menuReturn result;

    // Background refresh thread (~30 FPS animation driver)
    std::atomic<bool> animating(true);
    std::thread animThread([this, &animating] {
        while (animating) {
            std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 FPS
            screen_.PostEvent(Event::Custom);
        }
    });

    auto exitMenu = [&] {
        animating = false;
        if (animThread.joinable()) animThread.join();
        screen_.Exit();
    };

    // Queue/Settings/Help
    auto component = Renderer([this] {
        Element body = text("");
        if (activePane_ == Pane::Queue)    body = renderQueue();
        if (activePane_ == Pane::Settings) body = renderSettings();
        if (activePane_ == Pane::Help)     body = renderHelp();

        return vbox({
            renderLogo(),
            renderTabs(),
            text(""),
            body | hcenter | flex, // Centered pane body
            filler(),
            renderFooter()
        }) | bgcolor(settings_.palette().background) | color(settings_.palette().textPrimary);
    });

    auto eventHandler = CatchEvent(component, [&](Event ev) {
        // Redraw frame on Custom Animation Tick
        if (ev == Event::Custom) {
            return false;
        }

        // Locked when typing in the queue prompt
        if (activePane_ == Pane::Queue && addingFile_) {
            if (ev == Event::Escape) {
                addingFile_ = false;
                fileInputBuf_.clear();
                return true;
            }

            if (ev == Event::Return) {
                if (!fileInputBuf_.empty()) {
                    queue_.push_back({ fileInputBuf_, { 0 } });
                    fileInputBuf_.clear();
                    addingFile_ = false;
                    queueCursor_ = static_cast<int>(queue_.size()) - 1;
                }
                return true;
            }

            // Ctrl + D, clear entire path
            if (ev == Event::Character("\x04")) {
                fileInputBuf_.clear();
                return true;
            }

            // Ctrl + W, clear previous Word
            if (ev == Event::Character("\x17")) {
                clearWord(fileInputBuf_);
                return true;
            }

            if (ev == Event::Tab || ev == Event::ArrowRight) {
                std::string ghost = autoComplete(fileInputBuf_);
                if (!ghost.empty()) fileInputBuf_ += ghost;
                return true;
            }

            if (ev == Event::Backspace) {
                if (!fileInputBuf_.empty()) fileInputBuf_.pop_back();
                return true;
            }

            if (ev.is_character()) {
                fileInputBuf_ += ev.character();
                return true;
            }

            return true;
        }

        // Global Keybindings (when not typing)
        if (ev == Event::Character('q') || ev == Event::Escape) {
            exitMenu();
            return true;
        }

        if (ev == Event::Tab) {
            activePane_ = (activePane_ == Pane::Queue) ? Pane::Settings
                : (activePane_ == Pane::Settings) ? Pane::Help : Pane::Queue;
            lastTabSwitchTime_ = getTimeSec(); // Record tab switch timestamp for animation
            return true;
        }

        // Queue Pane Keybindings
        if (activePane_ == Pane::Queue) {
            if (ev == Event::Character('a')) {
                addingFile_ = true;
                fileInputBuf_.clear();
                return true;
            }
            if (ev == Event::Character('d')) {
                if (!queue_.empty()) {
                    queue_.erase(queue_.begin() + queueCursor_);
                    if (queueCursor_ >= static_cast<int>(queue_.size()) && queueCursor_ > 0)
                        queueCursor_--;
                }
                return true;
            }
            if (ev == Event::ArrowUp || ev == Event::Character('k')) {
                if (queueCursor_ > 0) queueCursor_--;
                return true;
            }
            if (ev == Event::ArrowDown || ev == Event::Character('j')) {
                if (queueCursor_ < static_cast<int>(queue_.size()) - 1) queueCursor_++;
                return true;
            }

            // Press Enter to start playback
            if (ev == Event::Return) {
                if (!queue_.empty()) {
                    result.action = menuAction::Play;
                    result.queue = queue_;
                    exitMenu();
                }
                return true;
            }

            // Port Assignment (0..9..A..F)
            if (ev.is_character()) {
                char c = ev.character()[0];
                int port = charToPort(c);
                if (port >= 0 && port < 16) {
                    togglePort(static_cast<unsigned int>(port));
                    return true;
                }
            }
        }

        // Settings keybindings
        if (activePane_ == Pane::Settings) {
            if (ev == Event::ArrowUp || ev == Event::Character('k')) {
                if (settingsCursor_ > 0) settingsCursor_--;
                return true;
            }
            if (ev == Event::ArrowDown || ev == Event::Character('j')) {
                if (settingsCursor_ < kNumSettings - 1) settingsCursor_++;
                return true;
            }
            if (ev == Event::ArrowLeft || ev == Event::Character('h')) {
                cycleSettingLeft();
                return true;
            }
            if (ev == Event::ArrowRight || ev == Event::Character('l') || ev == Event::Return || ev == Event::Character(' ')) {
                cycleSettingRight();
                return true;
            }
        }

        return false;
    });

    screen_.Loop(eventHandler);

    // Stop animation thread on exit
    animating = false;
    if (animThread.joinable()) animThread.join();

    result.settings = settings_;
    return result;
}

// LOGO & TABS
//
//

Element menuUi::renderLogo() const {
    // clang-format off
    static const char* kLogo[] = {
        "███╗   ███╗██╗  ██╗████████╗ ██████╗ ██████╗ ",
        "████╗ ████║╚██╗██╔╝╚══██╔══╝██╔═══██╗██╔══██╗",
        "██╔████╔██║ ╚███╔╝    ██║   ██║   ██║██████╔╝",
        "██║╚██╔╝██║ ██╔██╗    ██║   ██║   ██║██╔═══╝ ",
        "██║ ╚═╝ ██║██╔╝ ██╗   ██║   ╚██████╔╝██║     ",
        "╚═╝     ╚═╝╚═╝  ╚═╝   ╚═╝    ╚═════╝ ╚═╝     "
    };
    // clang-format on

    const auto palette = settings_.palette();
    double t = getTimeSec();

    // Theme palette colors for logo wave animation
    const std::array<Color, 5> themeColors = {
        palette.vuHigh,
        palette.headerBpm,
        palette.headerTitle,
        palette.headerClock,
        palette.headerTimeSig
    };

    Elements lines;
    size_t colorCount = themeColors.size();

    for (size_t rowIdx = 0; rowIdx < 6; ++rowIdx) {
        size_t colorIdx = static_cast<size_t>(t * 4.0 + rowIdx) % colorCount;
        lines.push_back(text(kLogo[rowIdx]) | color(themeColors[colorIdx]) | bold);
    }

    // Splash text with pulsating bold
    bool isBold = std::sin(t * 6.0) > 0.0;
    Element splashElem = text("★ " + splashText_ + " ★") | color(palette.headerBpm);
    if (isBold) {
        splashElem = splashElem | bold;
    }

    return vbox({
        text(""), // Top margin
        vbox(std::move(lines)) | hcenter,
        hbox({splashElem}) | hcenter,
        hbox({
            text("A MIDI/SysEx visualizer, by SimTheNep ") | color(palette.textDim)
        }) | hcenter,
        text("")
    });
}

Element menuUi::renderTabs() const {
    const auto palette = settings_.palette();

    auto tab = [&](const std::string& label, Pane pane) {
        Element t = text(" " + label + " ") | border;
        return (activePane_ == pane) 
            ? (t | color(palette.fxValue) | bold) 
            : (t | color(palette.textDim));
    };

    return hbox({
        filler(),
        tab("Queue", Pane::Queue),
        tab("Settings", Pane::Settings),
        tab("Help", Pane::Help),
        filler()
    });
}

// QUEUE PANE
//
//

void menuUi::togglePort(unsigned int portIdx) {
    if (queue_.empty() || queueCursor_ < 0 || queueCursor_ >= static_cast<int>(queue_.size())) return;

    auto& ports = queue_[queueCursor_].ports;
    auto it = std::find(ports.begin(), ports.end(), portIdx);

    if (it != ports.end()) {
        ports.erase(it); // Remove if already assigned
    } else {
        ports.push_back(portIdx); // Add if not assigned
        std::sort(ports.begin(), ports.end());
    }
}

Element menuUi::renderQueue() const {
    const auto palette = settings_.palette();
    double t = getTimeSec();

    Elements queueRows;

    if (queue_.empty() && !addingFile_) {
        queueRows.push_back(text(" Queue is empty. Press 'a' to add a file.") | color(palette.textDim));
    } else {
        for (int i = 0; i < static_cast<int>(queue_.size()); i++) {
            bool selected = (i == queueCursor_);
            const auto& item = queue_[i];

            Element prefix = selected
                ? (text(" 󰐊 ") | color(palette.headerTitle) | bold) 
                : text("   ");
            
            Element num = text("[" + std::to_string(i + 1) + "] ") | color(palette.textDim);
            Element name = text(item.file) | size(WIDTH, EQUAL, 24);

            if (selected) {
                name = name | color(palette.textPrimary) | bold;
            } else {
                name = name | color(palette.textDim);
            }

            // Auto-scrolling for port badges
            size_t numPorts = item.ports.size();
            size_t startIdx = 0;
            if (selected && numPorts > 6) {
                startIdx = static_cast<size_t>(t / 1.2) % numPorts;
            }

            Elements portBadges;
            size_t maxVisibleBadges = std::min(numPorts, size_t(7));
            for (size_t k = 0; k < maxVisibleBadges; ++k) {
                size_t pIdx = (startIdx + k) % numPorts;
                unsigned int p = item.ports[pIdx];
                portBadges.push_back(
                    text(" P" + std::to_string(p + 1) + " ") | color(palette.fxValue) | bold
                );
            }

            if (numPorts > 7) {
                portBadges.push_back(text("…") | color(palette.headerBpm));
            }

            Element portsContainer = hbox(std::move(portBadges));

            queueRows.push_back(hbox({ prefix, num, name, text(" "), portsContainer }));
        }
    }

    // Bottom tooltip drawer
    Element bottomDrawer;

    if (addingFile_) {
        std::string ghost = autoComplete(fileInputBuf_);
        bottomDrawer = vbox({
            hbox({
                text(" file path > ") | color(palette.headerBpm) | bold,
                text(fileInputBuf_) | color(palette.headerClock) | bold,
                text(ghost) | color(palette.textDim),
                text("_") | blink | color(palette.headerClock)
            }) | hscroll_indicator | flex,
            text(" [Enter] Confirm  [Tab/→] Complete  [Ctrl+W] Clear Word  [Ctrl+D] Clear Line  [Esc] Cancel ") | color(palette.textDim)
        });
    } else if (queue_.empty()) {
        bottomDrawer = hbox({
            text(" Tip: ") | color(palette.masterLabel) | bold,
            text("Press 'a' to add a MIDI file to the queue.") | color(palette.textPrimary)
        });
    } else {
        const auto& sel = queue_[queueCursor_];
        std::string formattedRanges = formatPorts(sel.ports);

        bottomDrawer = vbox({
            hbox({
                text(" Selected: ") | color(palette.masterLabel) | bold,
                text(sel.file + " ") | color(palette.textPrimary) | bold,
                text("(" + formattedRanges + ")") | color(palette.fxLabel)
            }) | hscroll_indicator | flex,
            hbox({
                text(" [0..9] [A..F] Toggle Port  [a] Add  [d] Delete  [Enter] Start Playback ") | color(palette.footerText)
            })
        });
    }

    // Apply transition
    Element bodyContent = scanlineTransition(std::move(queueRows), lastTabSwitchTime_);

    return window(
        text(" QUEUE ") | bold,
        vbox({
            bodyContent | flex,
            separator() | color(palette.tableHeader),
            bottomDrawer
        })
    ) | color(palette.tableHeader) | size(WIDTH, EQUAL, 74);
}

// SETTINGS PANE
//
//

void menuUi::cycleSetting(int delta) {
    switch (settingsCursor_) {
        case 0: { // Theme
            auto themes = getThemes();
            auto it = std::find(themes.begin(), themes.end(), settings_.theme);
            int idx = (it != themes.end()) ? static_cast<int>(std::distance(themes.begin(), it)) : 0;
            idx = (idx + delta + static_cast<int>(themes.size())) % static_cast<int>(themes.size());
            settings_.theme = themes[idx];
            break;
        }
        case 1: // Numeral Format
            settings_.numeralFormat = (settings_.numeralFormat == NumeralFormat::Decimal) 
                ? NumeralFormat::Hex : NumeralFormat::Decimal;
            break;
        case 2: { // Module Override
            auto modules = getModules();
            auto it = std::find(modules.begin(), modules.end(), settings_.moduleOverride);
            int idx = (it != modules.end()) ? static_cast<int>(std::distance(modules.begin(), it)) : 0;
            idx = (idx + delta + static_cast<int>(modules.size())) % static_cast<int>(modules.size());
            settings_.moduleOverride = modules[idx];
            break;
        }
        case 3: // Show Event Log
            settings_.showEventLog = !settings_.showEventLog;
            break;
        case 4: // Show Activity Grid
            settings_.showActivityGrid = !settings_.showActivityGrid;
            break;
        case 5: // Log Max Visible
            settings_.logMaxVisible = std::clamp(settings_.logMaxVisible + delta, 5, 30);
            break;
        case 6: // VU Bar Width
            settings_.vuBarWidth = std::clamp(settings_.vuBarWidth + (delta * 2), 10, 40);
            break;
        case 7: // Show Peak Marker
            settings_.showPeakMarker = !settings_.showPeakMarker;
            break;
        case 8: // Highlight Timeout
            settings_.highlightTimeoutMs = std::clamp(settings_.highlightTimeoutMs + (delta * 10.0), 10.0, 500.0);
            break;
        case 9: // Note Off Grace
            settings_.noteOffGraceMs = std::clamp(settings_.noteOffGraceMs + (delta * 10.0), 0.0, 300.0);
            break;
    }
    saveSettings(settings_);
}

void menuUi::cycleSettingLeft()  { cycleSetting(-1); }
void menuUi::cycleSettingRight() { cycleSetting(+1); }

Element menuUi::renderSettings() const {
    const auto palette = settings_.palette();

    struct ItemDef {
        std::string section;
        std::string label;
        std::string value;
        std::string description;
    };

    auto fmtBool = [](bool val) { return val ? "Enabled" : "Disabled"; };

    std::array<ItemDef, kNumSettings> items = {{
        { "Display", "Theme", settings_.theme, getThemeDesc(settings_.theme) },
        { "Display", "Numeral Format", toString(settings_.numeralFormat), "Display values in Decimal or Hexadecimal" },
        { "Display", "Module Override", toString(settings_.moduleOverride), "Select module profile" },
        { "Display", "Show Event Log", fmtBool(settings_.showEventLog), "Display MIDI event log" },
        { "Display", "Show Activity Grid", fmtBool(settings_.showActivityGrid), "Display channel activity grid" },
        { "Display", "Log Max Visible Lines", std::to_string(settings_.logMaxVisible), "Maximum visible rows in event log" },

        { "Meter & Level", "VU Bar Width", std::to_string(settings_.vuBarWidth), "Width of channel VU level meters in characters" },
        { "Meter & Level", "Show Peak Marker", fmtBool(settings_.showPeakMarker), "Show peak hold indicator on VU meters" },
        { "Meter & Level", "Highlight Timeout", std::to_string(static_cast<int>(settings_.highlightTimeoutMs)) + " ms", "Duration for channel highlight flash on events" },
        { "Meter & Level", "Note Off Grace", std::to_string(static_cast<int>(settings_.noteOffGraceMs)) + " ms", "Decay grace period after note off" }
    }};

    Elements listRows;
    std::string currentSection = "";

    for (int i = 0; i < kNumSettings; ++i) {
        const auto& item = items[i];

        if (item.section != currentSection) {
            if (!currentSection.empty()) listRows.push_back(text(""));
            currentSection = item.section;
            listRows.push_back(text("[ " + currentSection + " ]") | color(palette.tableHeader) | bold);
        }

        bool selected = (i == settingsCursor_);

        Element prefixElem = selected 
            ? (text(" ▸ ") | color(palette.headerTitle) | bold) 
            : text("   ");

        Element labelElem = text(item.label) | size(WIDTH, EQUAL, 28);
        Element valElem = text("< " + item.value + " >") | bold;

        if (selected) {
            labelElem = labelElem | color(palette.textPrimary) | bold;
            valElem = valElem | color(palette.headerBpm) | bold;
        } else {
            labelElem = labelElem | color(palette.textPrimary);
            valElem = valElem | color(palette.fxLabel);
        }

        listRows.push_back(hbox({ prefixElem, labelElem, valElem }));
    }

    std::string desc = items[settingsCursor_].description;
    Element hintBox = vbox({
        text(""),
        separator() | color(palette.panelBorder),
        hbox({
            text(" Tip: ") | color(palette.masterLabel) | bold,
            text(desc) | color(palette.textPrimary)
        })
    });

    listRows.push_back(filler());
    listRows.push_back(hintBox);

    // Apply transition
    Element bodyContent = scanlineTransition(std::move(listRows), lastTabSwitchTime_);

    return window(
        text(" SETTINGS ") | bold,
        bodyContent
    ) | color(palette.tableHeader) | size(WIDTH, EQUAL, 74);
}

// HELP PANE
//
//

Element menuUi::renderHelp() const {
    const auto palette = settings_.palette();

    auto row = [&](const std::string& keys, const std::string& desc) {
        return hbox({
            text(keys) | color(palette.headerBpm) | bold | size(WIDTH, EQUAL, 22),
            text(desc) | color(palette.textPrimary)
        });
    };

    Elements lines = {
        text("[ Queue ]") | color(palette.headerTitle) | bold,
        row("   j / k / ↑ ↓", "move selection up/down"),
        row("   a", "add a MIDI file to the queue"),
        row("   ctrl + d", "clear prompt text"),
        row("   ctrl + w", "clear to last /"),
        row("   0..9 / A..F", "assign ports 1..16 to the selected queue entry"),
        row("   d", "remove the selected queue entry"),
        row("   Enter", "start playback"),
        text(""),
        text("[ Settings ]") | color(palette.headerTitle) | bold,
        row("   j / k / ↑ ↓", "move selection up/down"),
        row("   h / l / ← →", "change selected setting value"),
        row("   Space / Enter", "cycle selected setting forward"),
        text(""),
        text("[ Playback ]") | color(palette.headerTitle) | bold,
        row("   h / l / ← →", "cycle port pages"),
        row("   j / k / ↑ ↓", "cycle through the queue"),
        row("   x", "stop playback"),
        row("   p", "panic"),
        row("   r", "restart the current file"),
        row("   s + 0..9 / A..F", "solo channel toggle"),
        row("   m + 0..9/ A..F", "mute channel toggle")
    };

    // Apply transition
    Element bodyContent = scanlineTransition(std::move(lines), lastTabSwitchTime_);

    return window(text(" HELP ") | bold, bodyContent) | color(palette.tableHeader) | size(WIDTH, EQUAL, 74);
}

// FOOTER
//
//

Element menuUi::renderFooter() const {
    const auto palette = settings_.palette();

    Element tabHint = hbox({ text(" [Tab] ") | color(palette.headerClock) | bold, text("Change tabs ") | color(palette.footerText) });
    
    Element queueHint = hbox({
        text("| [j/k or ↑/↓] ") | color(palette.headerClock) | bold, text("Move ") | color(palette.footerText),
        text("| [a] ") | color(palette.headerClock) | bold, text("Add ") | color(palette.footerText)
    });

    Element settingsHint = hbox({
        text("| [j/k or ↑/↓] ") | color(palette.headerClock) | bold, text("Move ") | color(palette.footerText),
        text("| [h/l or ←/→] ") | color(palette.headerClock) | bold, text("Change ") | color(palette.footerText)
    });

    Element quitHint = hbox({ text("| [q] ") | color(palette.headerClock) | bold, text("Quit ") | color(palette.footerText) });

    Elements footerItems = { tabHint };
    if (activePane_ == Pane::Queue) {
        footerItems.push_back(queueHint);
    } else if (activePane_ == Pane::Settings) {
        footerItems.push_back(settingsHint);
    }
    footerItems.push_back(quitHint);

    return hbox({
        hbox(std::move(footerItems)),
        filler()
    });
}