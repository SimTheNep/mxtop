#include "menu_ui.hpp"

#include <ftxui/component/event.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <set>

using namespace ftxui;

// HELPERS
//
//

namespace {
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

    // Lower string, has been done beforfe but I forgot where
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
        return -1;
    }
}

// RUNTIME
//
//

menuUi::menuUi(const std::vector<std::string>& availablePorts, Settings settings)
    : availablePorts_(availablePorts), settings_(std::move(settings)) {}

menuReturn menuUi::run() {
    menuReturn result;

    // Queue/Settings/Help
    auto component = Renderer([this] {
        Element body = text("");
        if (activePane_ == Pane::Queue)    body = renderQueue();
        if (activePane_ == Pane::Settings) body = renderSettings();
        if (activePane_ == Pane::Help)     body = renderHelp();

        return vbox({
            renderLogo(),
            renderTabs(),
            body | flex,
            filler(),
            renderFooter()
        }) | bgcolor(settings_.palette().background) | color(settings_.palette().textPrimary);
    });

    auto eventHandler = CatchEvent(component, [&](Event ev) {
        // Locked when typikng in the queue prompt
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

            // Ctrl + W, lear Previous Word
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
            screen_.Exit();
            return true;
        }

        if (ev == Event::Tab) {
            activePane_ = (activePane_ == Pane::Queue) ? Pane::Settings
                : (activePane_ == Pane::Settings) ? Pane::Help : Pane::Queue;
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
                    screen_.Exit();
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

    result.settings = settings_;
    return result;
}

// LOGO & TABS
//
//

Element menuUi::renderLogo() const {
    const auto palette = settings_.palette();

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

    Elements lines;
    for (const char* row : kLogo)
        lines.push_back(text(row) | color(palette.headerTitle) | bold);

    return vbox({
        vbox(std::move(lines)) | hcenter,
        text("A MIDI/SysEx visualizer, by SimTheNep") | color(palette.textDim) | hcenter,
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

// RENDER QUEUE
//
//

// Toggles the port selection in the selcted file
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
    Elements queueRows;

    if (queue_.empty() && addingFile_){
        queueRows.push_back(text(" Queue is empty. Press 'a' to add a file.") | color (palette.textDim));

    } else {
        for (int i = 0; i < static_cast<int>(queue_.size()); i++){
            bool selected = (i == queueCursor_);
            const auto& item = queue_[i];

            Element prefix = selected
                ? (text(" 󰐊 ") | color(palette.headerTitle) | bold) 
                : text("   ");
            
            Element num = text("[" + std:: to_string(i+1) + "]") | color(palette.textDim);

            Element name = text(item.file) | size(WIDTH, EQUAL, 32);

            if (selected){
                name = name | color(palette.textPrimary) | bold;
            } else {
                name = name | color(palette.textDim);
            }

            // Ports
            Elements portBadges;
            for (unsigned int p : item.ports) {
                portBadges.push_back(
                    text(" P" + std::to_string(p) + " ") | color(palette.fxValue) | bold
                );
            }

            queueRows.push_back(hbox({prefix, num, name, hbox(std::move(portBadges))}));
        }
    }

    // Bottom tooltip
    Element bottomDrawer;

    if (addingFile_) {
        std::string ghost = autoComplete(fileInputBuf_);
        bottomDrawer = vbox({
            hbox({
                text(" file path > ") | color(palette.headerBpm) | bold,
                text(fileInputBuf_) | color(palette.headerClock) | bold,
                text(ghost) | color(palette.textDim),
                text("█") | blink | color(palette.headerClock)
            }),
            text(" [Enter] Confirm  [Tab/→] Complete  [Ctrl+W] Clear Word  [Ctrl+D] Clear Line  [Esc] Cancel ") | color(palette.textDim)
        });
    } else if (queue_.empty()) {
        bottomDrawer = hbox({
            text(" Tip: ") | color(palette.masterLabel) | bold,
            text("Press 'a' to add a MIDI file to the queue.") | color(palette.textPrimary)
        });
    } else {
        const auto& sel = queue_[queueCursor_];
        std::string portsStr = "Assigned Ports: ";
        for (size_t i = 0; i < sel.ports.size(); ++i) {
            portsStr += "P" + std::to_string(sel.ports[i]) + (i + 1 < sel.ports.size() ? ", " : "");
        }

        bottomDrawer = vbox({
            hbox({
                text(" Selected: ") | color(palette.masterLabel) | bold,
                text(sel.file) | color(palette.textPrimary) | bold,
                text("  (" + portsStr + ")") | color(palette.fxLabel)
            }),
            hbox({
                text(" [0..9] [A..F] Toggle Port  [a] Add  [d] Delete  [Enter] Start Playback ") | color(palette.footerText)
            })
        });
    }

    return window(
        text(" QUEUE ") | bold,
        vbox({
            vbox(std::move(queueRows)) | flex,
            separator() | color(palette.tableHeader),
            bottomDrawer
        })
    ) | color(palette.tableHeader) | flex;
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

    return window(
        text(" SETTINGS ") | bold,
        vbox({
            vbox(std::move(listRows)),
            filler(),
            hintBox
        })
    ) | color(palette.tableHeader) | flex;
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
        text("Queue") | color(palette.headerTitle) | bold,
        row("j / k / ↑ ↓", "move selection up/down"),
        row("a", "add a MIDI file to the queue"),
        row("ctrl + d", "clear prompt text"),
        row("ctrl + w", "clear to last /"),
        row("0..9 / A..F", "assign ports 1..16 to the selected queue entry"),
        row("d", "remove the selected queue entry"),
        row("Enter", "start playback"),
        text(""),
        text("Settings") | color(palette.headerTitle) | bold,
        row("j / k / ↑ ↓", "move selection up/down"),
        row("h / l / ← →", "change selected setting value"),
        row("Space / Enter", "cycle selected setting forward"),
        text(""),
        text("Playback") | color(palette.headerTitle) | bold,
        row("h / l / ← →", "cycle port pages"),
        row("j / k / ↑ ↓", "cycle through the queue"),
        row("x", "stop playback"),
        row("p", "panic"),
        row("r", "restart the current file"),
        row("s + 0..9 / A..F", "solo channel toggle"),
        row("m + 0..9/ A..F", "mute channel toggle")
    };

    return window(text(" HELP ") | bold, vbox(std::move(lines))) | color(palette.tableHeader) | flex;
}

// FOOTER
//
//

Element menuUi::renderFooter() const {
    const auto palette = settings_.palette();

    Element tabHint = hbox({ text(" [Tab] ") | color(palette.headerClock) | bold, text("Change tabs ") | color(palette.footerText) });
    Element settingsHint = hbox({ text("| [j/k or ↑/↓] ") | color(palette.headerClock) | bold, text("Move ") | color(palette.footerText),
                                 text("| [h/l or ←/→] ") | color(palette.headerClock) | bold, text("Change ") | color(palette.footerText) });
    Element quitHint = hbox({ text("| [q] ") | color(palette.headerClock) | bold, text("Quit ") | color(palette.footerText) });

    Elements footerItems = { tabHint };
    if (activePane_ == Pane::Settings) {
        footerItems.push_back(settingsHint);
    }
    footerItems.push_back(quitHint);

    return hbox({
        hbox(std::move(footerItems)),
        filler()
    });
}