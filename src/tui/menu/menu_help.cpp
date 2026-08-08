#include "menu_ui.hpp"

using namespace ftxui;

// MENU_HELP.CPP
//
// Help instructions and footer shortcuts

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