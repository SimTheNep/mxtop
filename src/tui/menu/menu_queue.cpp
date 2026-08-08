#include "menu_ui.hpp"

#include <algorithm>

using namespace ftxui;

// MENU_QUEUE.CPP
//
// Queue pane layout, file path auto-completion and MIDI port assignment

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