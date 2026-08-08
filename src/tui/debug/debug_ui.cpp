#include "debug_ui.hpp"
#include <ncurses.h>
#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>
#include <optional>
#include <array>
#include <algorithm>

// DATA FUNCTS
const char* kind(MsgKind k) {
    switch (k) {
        case MsgKind::NoteOff:           return "NoteOff";
        case MsgKind::NoteOn:            return "NoteOn";
        case MsgKind::PolyAftertouch:    return "PolyAT";
        case MsgKind::CC:                return "CC";
        case MsgKind::ProgramChange:     return "PC";
        case MsgKind::ChannelAftertouch: return "ChanAT";
        case MsgKind::PitchBend:         return "PitchBend";
        case MsgKind::SysEx:             return "SysEx";
        default:                         return "Unknown";
    }
}

const char* sysexCmd(const RawEvent& ev) {
    if (ev.data.size() < 6)
        return "SysEx";

    switch (ev.data[5]) {
        case 0x12: return "DT1";
        case 0x11: return "RQ1";
        default:   return "???";
    }
}

std::string sysexAddr(const RawEvent& ev) {
    if (ev.data.size() < 10)
        return "";

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02X %02X %02X %02X",
        ev.data[6], ev.data[7], ev.data[8], ev.data[9]
    );
    return buf;
}

std::string hexBytes(const RawEvent& ev) {
    std::string s;
    char buf[4];

    if (ev.kind == MsgKind::SysEx) {
        return std::to_string(ev.data.size()) + " bytes";
    }

    for (size_t i = 0; i < ev.data.size(); ++i) {
        std::snprintf(buf, sizeof(buf), "%02X ", ev.data[i]);
        s += buf;
    }
    return s;
}

// Map MsgKind to a safe index within the array size of 10
inline size_t kindToIndex(MsgKind k) {
    switch (k) {
        case MsgKind::NoteOff:           return 0;
        case MsgKind::NoteOn:            return 1;
        case MsgKind::PolyAftertouch:    return 2;
        case MsgKind::CC:                return 3;
        case MsgKind::ProgramChange:     return 4;
        case MsgKind::ChannelAftertouch: return 5;
        case MsgKind::PitchBend:         return 6;
        case MsgKind::SysEx:             return 7;
        default:                         return 8;
    }
}

// UI FUNCTS
DebugUi::DebugUi(const std::vector<std::string>& files, const std::vector<std::string>& ports ) : files_(files), ports_(ports) {
    initscr();
    noecho();
    cbreak();
    curs_set(0);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

    getmaxyx(stdscr, rows_, cols_);

    labelsHeight_ = 2;

    infoHeight_ = 3;

    if (files_.size() == 1)
        infoHeight_ += 1 + ports_.size();
    else
        infoHeight_ += files_.size() * 3;

    info_ = newwin(infoHeight_, cols_, 0, 0);
    labels_ = newwin(labelsHeight_, cols_, infoHeight_, 0);

    // Initial pad size - allocating 500 rows to hold all channels easily
    logPadRows_ = 500;
    log_ = newpad(logPadRows_, cols_);

    if (!info_ || !labels_ || !log_) {
        std::fprintf(stderr, "[debug_ui] failed to create window/pad rows=%d cols=%d\n", rows_, cols_);
    }

    drawInfo();
    drawLabels();
    drawTable();
}

DebugUi::~DebugUi() {
    delwin(log_);
    delwin(labels_);
    delwin(info_);
    endwin();
}

std::string DebugUi::formatMessage(const RawEvent& ev) {
    switch (ev.kind) {
        case MsgKind::CC:
            return "CC " + std::to_string(ev.data.size() > 1 ? ev.data[1] : 0) +
                   " = " + std::to_string(ev.data.size() > 2 ? ev.data[2] : 0);

        case MsgKind::ProgramChange:
            return "program " + std::to_string(ev.data.size() > 1 ? ev.data[1] : 0);

        case MsgKind::NoteOn:
        case MsgKind::NoteOff: {
            std::string msg = "note ";
            msg += std::to_string(ev.data.size() > 1 ? ev.data[1] : 0);
            msg += " vel ";
            msg += std::to_string(ev.velocity);
            return msg;
        }

        case MsgKind::PitchBend:         return "pitch bend";
        case MsgKind::ChannelAftertouch: return "aftertouch";
        case MsgKind::PolyAftertouch:    return "poly aftertouch";

        case MsgKind::SysEx: {
            char buf[16];
            if (ev.data.size() >= 8) {
                std::snprintf(buf, sizeof(buf), "%02X %02X %02X %02X",
                    ev.data[6], ev.data[7], ev.data[8], ev.data[9]);
                return std::string(sysexCmd(ev)) + " (" + buf + ")";
            }
            return "SysEx (Roland)";
        }
        default: return "";
    }
}

void DebugUi::checkScrollInput() {
    int ch = getch();
    if (ch == KEY_UP) {
        scrollOffset_ = std::max(0, scrollOffset_ - 1);
    } else if (ch == KEY_DOWN) {
        scrollOffset_++;
    } else if (ch == KEY_PPAGE) { // Page Up
        scrollOffset_ = std::max(0, scrollOffset_ - 10);
    } else if (ch == KEY_NPAGE) { // Page Down
        scrollOffset_ += 10;
    }
}

void DebugUi::addEvent(const RawEvent& ev, bool) {
    lastTimestamp_ = ev.timestamp;
    latest_[kindToIndex(ev.kind)] = ev;

    checkScrollInput();
    drawInfo();
    drawLabels();
    drawTable();
}

void DebugUi::drawInfo() {
    werase(info_);
    mvwprintw(info_, 0, 0, "mxtop MIDI reader debug - [ARROWS/PgUp/PgDn] Scroll | Ctrl+C Stop");
    mvwprintw(info_, 1, 0, "Current Timestamp: %.1f ms", lastTimestamp_);

    int row = 3;

    if (files_.size() == 1) {
        mvwprintw(info_, row++, 0,
            "%s",
            std::filesystem::path(files_[0]).filename().string().c_str());

        for (size_t i = 0; i < ports_.size(); ++i) {
            mvwprintw(info_, row++, 2,
                "Port [%zu]: %s",
                i,
                ports_[i].c_str());
        }
    } else {
        for (size_t i = 0; i < files_.size(); ++i) {
            mvwprintw(info_, row++, 0,
                "%s",
                std::filesystem::path(files_[i]).filename().string().c_str());

            if (i < ports_.size()) {
                mvwprintw(info_, row++, 2,
                    "Port [%zu]: %s",
                    i,
                    ports_[i].c_str());
            }

            row++;
        }
    }
    wrefresh(info_);
}

void DebugUi::drawLabels() {
    werase(labels_);
    mvwprintw(labels_, 0, 0, "%-12s %-4s %-20s %s", "KIND", "CH", "MSG", "BYTES");
    mvwhline(labels_, 1, 0, ACS_HLINE, cols_);
    wrefresh(labels_);
}

void DebugUi::drawTable() {
    werase(log_);

    int row = 0;
    for (const auto& evOpt : latest_) {
        if (!evOpt.has_value()) continue;
        const RawEvent& ev = *evOpt;

        mvwprintw(log_, row++, 0, "%-12s %-4d %-20s %s",
            kind(ev.kind), ev.channel, formatMessage(ev).c_str(), hexBytes(ev).c_str()
        );
    }

    // Refresh pad section to screen viewport
    pnoutrefresh(
        log_,
        scrollOffset_, 0, // Pad top-left offset
        infoHeight_ + labelsHeight_, 0, // Screen viewport top-left
        rows_ - 1, cols_ - 1 // Screen viewport bottom-right
    );
    doupdate();
}

// STATE MODE
void DebugUi::addSnap(int channel, const takeSnapshot& snap, double timestampMs) {
    lastTimestamp_ = timestampMs;
    stateSnapshots_[channel] = snap;

    checkScrollInput();
    drawInfo();
    drawStateLabels();
    drawStateTable();
}

void DebugUi::drawStateLabels() {
    werase(labels_);
    mvwprintw(labels_, 0, 0, "%-8s %-24s %s", "TARGET", "OBJECT / FIELD", "STATE / DISPLAY VALUE");
    mvwhline(labels_, 1, 0, ACS_HLINE, cols_);
    wrefresh(labels_);
}

void DebugUi::drawStateTable() {
    // Count exactly how many rows this frame needs before touching the pad
    int neededRows = 0;
    for (const auto& [channel, snap] : stateSnapshots_) {
        neededRows += 1;                       // header
        neededRows += snap.values.size();      // values
        neededRows += snap.patchNames.size();  // patch names
        neededRows += 1;                       // polyphony
        neededRows += 1;                       // spacer
    }

    // Resize if what we need has outgrown what the pad currently has
    if (neededRows > logPadRows_) {
        logPadRows_ = neededRows + 64;
        wresize(log_, logPadRows_, cols_);
    }

    werase(log_);

    int row = 0;

    for (const auto& [channel, snap] : stateSnapshots_) {
        bool hasResolvedPatch = false;
        for (const auto& [id, name] : snap.patchNames) {
            if (name.has_value()) {
                hasResolvedPatch = true;
                break;
            }
        }

        // Header
        if (channel == -1) {
            mvwprintw(log_, row++, 0, "[=== SYSEX STATE ===]");
        } else {
            mvwprintw(log_, row++, 0, "[=== CHANNEL %d ===]", channel + 1);
        }

        // Values
        for (const auto& [id, display] : snap.values) {
            mvwprintw(log_, row++, 2, "%-24s : %s", id.c_str(), display.c_str());
        }

        // Patches
        for (const auto& [id, name] : snap.patchNames) {
            mvwprintw(log_, row++, 2, "%-24s : %s", id.c_str(), name ? name->c_str() : "(unresolved)");
        }

        mvwprintw(log_, row++, 2, "%-24s : poly=%d (last velocity: %d)",
            "Polyphony", snap.polyCount, snap.lastVelo);

        row++; // Space
    }

    // Clamp scroll bounds
    int viewportHeight = rows_ - infoHeight_ - labelsHeight_;
    int maxScroll = std::max(0, row - viewportHeight);
    scrollOffset_ = std::min(scrollOffset_, maxScroll);

    // Refresh Pad view
    pnoutrefresh(
        log_,
        scrollOffset_, 0,
        infoHeight_ + labelsHeight_, 0,
        rows_ - 1, cols_ - 1
    );
    doupdate();
}