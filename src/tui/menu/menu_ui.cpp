#include "menu_ui.hpp"

#include <ftxui/component/event.hpp>

using namespace ftxui;

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
        if (activePane_ == Pane::Queue)    body = text("queue pane goes here");
        if (activePane_ == Pane::Settings) body = text("settings pane goes here");
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
        // Keybindings
        if (ev == Event::Character('q') || ev == Event::Escape) {
            screen_.Exit();
            return true;
        }

        if (ev == Event::Tab) {
            activePane_ = (activePane_ == Pane::Queue) ? Pane::Settings
                : (activePane_ == Pane::Settings) ? Pane::Help : Pane::Queue;
            return true;
        }

        return false;
    });

    screen_.Loop(eventHandler);

    // q/escape kills this
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
        lines.push_back(text(row) | color(palette.panelBorder) | bold);

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
        return (activePane_ == pane) ? (t | color(palette.panelBorder) | bold) : (t | color(palette.textDim));
    };

    return hbox({
        filler(),
        tab("Queue", Pane::Queue),
        tab("Settings", Pane::Settings),
        tab("Help", Pane::Help),
        filler()
    });
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
        text("Menu") | color(palette.textDim) | bold,
        row("Tab", "switch between Queue, Settings, Help"),
        row("j / k / ↑ ↓", "move selection up/down"),
        row("a", "add a MIDI file to the queue"),
        row("0..9 / A..F", "assign ports 1..16 to the selected queue entry"),
        row("d", "remove the selected queue entry"),
        row("Enter", "start playback"),
        row("q / Esc", "quit"),
        text(""),
        text("During playback") | color(palette.textDim) | bold,
        row("h / l / ← →", "cycle port pages"),
        row("j / k / ↑ ↓", "cycle channels"),
        row("Shift + j / k / ↑ ↓", "cycle through the queue"),
        row("x", "stop playback"),
        row("p", "panic"),
        row("r", "restart the current file"),
        row("s", "solo channel"),
        row("m", "mute channel"),
        row("q / Esc", "quit")
    };

    return window(text(" HELP ") | bold, vbox(std::move(lines))) | color(palette.panelBorder) | flex;
}

// FOOTER
//
//

Element menuUi::renderFooter() const {
    const auto palette = settings_.palette();

    return hbox({
        text(" [Tab] Change tabs [q] Quit ") | color(palette.footerText),
        filler()
    });
}