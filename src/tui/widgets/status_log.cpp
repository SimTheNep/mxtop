#include "status_log.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <string>

// STATUS LOG WIDGET
//
// Renders the CC/PC/pitch-bend/SysEx/Meta log

using namespace ftxui;

Element TuiWidgets::drawEventLog(const UiModel& model) {
    const auto palette = model.palette();
    auto entries = model.getLogEntries();
    Elements list;

    constexpr int maxVisible = 14;
    int count = 0;

    for (const auto& e : entries) {
        if (count >= maxVisible) break;

        std::string icon;
        Color baseColor = palette.textPrimary;

        // Assigned standard terminal colors
        if (e.type == "sysex") {
            icon = "󰅩 "; 
            baseColor = palette.logSysEx;
        } else if (e.type == "pc") {
            icon = "󰓹 "; 
            baseColor = palette.logPc;
        } else if (e.type == "cc") {
            icon = "󰡁 "; 
            baseColor = palette.logCc;
        } else if (e.type == "tempo") {
            icon = "󰎇 "; 
            baseColor = palette.logTempo;
        } else if (e.type == "timesig") {
            icon = "󰃬 "; 
            baseColor = palette.logTimeSig;
        } else {
            icon = "󰃬 "; 
            baseColor = palette.logTimeSig;
        }

        std::string fullText = icon + e.timecode + "  " + e.text;
        if (fullText.length() > 52) {
            fullText = fullText.substr(0, 49) + "...";
        }

        Element txt = text(fullText) | color(baseColor);
        
        // Simulates a fade-out using the text properties
        if (count == 0) {
            txt = txt | bold; // Highlight newest entry
        } else if (count >= 4) {
            txt = txt | color(palette.textDim); // Fade out older entries
        }

        list.push_back(txt);
        count++;
    }

    if (list.empty()) {
        list.push_back(text("Awaiting MIDI events...") | color(palette.textDim));
    }

    return window(text(" EVENT LOG ") | bold | color(palette.tableHeader), vbox(std::move(list))) | flex;
}