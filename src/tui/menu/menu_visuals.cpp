#include "menu_ui.hpp"

#include <array>
#include <cmath>

using namespace ftxui;

// MENU_VISUALS.CPP
//
// Animated logo and navigation tabs

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