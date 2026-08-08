#include "menu_ui.hpp"

#include <algorithm>
#include <array>

using namespace ftxui;

// MENU_SETTINGS.CPP
//
// Settings pane layout, configuration selection, persistent saving and tooltips

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