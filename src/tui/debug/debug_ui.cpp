#include "debug_ui.hpp"
#include <ncurses.h>
#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>
#include <optional>
#include <array>

// DATA FUNCTS
//
//

// Taken from types.hpp
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

// Grabs whether it's a write or read request, Roland format
const char* sysexCmd(const RawEvent& ev) {
    if (ev.data.size() < 6)
        return "SysEx";

    switch (ev.data[5]) {
        case 0x12: return "DT1";
        case 0x11: return "RQ1";
        default:   return "???";
    }
}

// Get address by checking bytes 6-9 (NICE)
std::string sysexAddr(const RawEvent& ev) {
    if (ev.data.size() < 10)
        return "";

    char buf[32];

    std::snprintf(buf, sizeof(buf), "%02X %02X %02X %02X",
        ev.data[6],
        ev.data[7],
        ev.data[8],
        ev.data[9]
    );

    return buf;
}

// Self explanatory
std::string hexBytes(const RawEvent& ev) {
    std::string s;
    char buf[4];

    size_t start = 0;

    // Prints out the SysEx message size instead of the bytes, it's huge
    if (ev.kind == MsgKind::SysEx) {
        return std::to_string(ev.data.size()) + " bytes";
    }

    for (size_t i = start; i < ev.data.size(); ++i) {
        std::snprintf(buf, sizeof(buf), "%02X ", ev.data[i]);
        s += buf;
    }

    return s;
}

// UI FUNCTS
//
//

MidiUi::MidiUi(
    const std::vector<std::string>& files,
    const std::vector<std::string>& ports
)
    : files_(files),
      ports_(ports)
{
    initscr();
    noecho();
    curs_set(0);

    getmaxyx(stdscr, rows_, cols_);

    int infoHeight = 3 + files_.size() * 3;
    constexpr int labelsHeight = 2;

    info_ = newwin(
        infoHeight,
        cols_,
        0,
        0
    );

    labels_ = newwin(
        labelsHeight,
        cols_,
        infoHeight,
        0
    );

    log_ = newwin(
        rows_ - infoHeight - labelsHeight,
        cols_,
        infoHeight + labelsHeight,
        0
    );

    // If the terminal is too small it throws this too
    if (!info_ || !labels_ || !log_) {
        std::fprintf(stderr, "[debug_ui] failed to create window(s) rows=%d cols=%d\n", rows_, cols_);
    }

    drawInfo();
    drawLabels();
    drawTable();
}

// KILL
MidiUi::~MidiUi() {
    delwin(log_);
    delwin(labels_);
    delwin(info_);
    endwin();
}

std::string MidiUi::formatMessage(const RawEvent& ev) {
    switch (ev.kind) {

        case MsgKind::CC:
            return
                "CC " +
                std::to_string(ev.data.size() > 1 ? ev.data[1] : 0) +
                " = " +
                std::to_string(ev.data.size() > 2 ? ev.data[2] : 0);

        case MsgKind::ProgramChange:
            return
                "program " +
                std::to_string(ev.data.size() > 1 ? ev.data[1] : 0);

        case MsgKind::NoteOn:
        case MsgKind::NoteOff: {
            std::string msg = "note ";
            msg += std::to_string(ev.data.size() > 1 ? ev.data[1] : 0);
            msg += " vel ";
            msg += std::to_string(ev.velocity);
            return msg;
        }

        case MsgKind::PitchBend:
            return "pitch bend";

        case MsgKind::ChannelAftertouch:
            return "aftertouch";

        case MsgKind::PolyAftertouch:
            return "poly aftertouch";

        case MsgKind::SysEx: {
            char buf[16];

            if (ev.data.size() >= 8) {
                std::snprintf(
                    buf,
                    sizeof(buf),
                    "%02X %02X %02X %02X",
                    ev.data[6],
                    ev.data[7],
                    ev.data[8],
                    ev.data[9]
                );

                return std::string(sysexCmd(ev)) + " (" + buf + ")";
            }

            return "SysEx (Roland)";
        }

        default:
            return "";
    }
}

void MidiUi::addEvent(const RawEvent& ev, bool) {
    lastTimestamp_ = ev.timestamp;

    latest_[ static_cast<int>(ev.kind) ] = ev;

    drawInfo();
    drawTable();
}

void MidiUi::drawInfo() {
    werase(info_);

    mvwprintw(info_, 0, 0, "mxtop MIDI reader debug - Ctrl+C to stop");

    mvwprintw(info_, 1, 0, "Current Timestamp: %.1f ms", lastTimestamp_); // here it is, ms 

    int row = 3;

    for (size_t i = 0; i < ports_.size(); ++i) {

        // This converts the file path to just the file name
        mvwprintw(info_, row++, 0, "%s", std::filesystem::path(files_[i]).filename().string().c_str());

        mvwprintw(info_, row++, 2, "Port [%zu]: %s", i, ports_[i].c_str());

        row++; // space
    }

    wrefresh(info_);
}

void MidiUi::drawLabels() {
    werase(labels_);

    mvwprintw(labels_, 0, 0, "%-12s %-4s %-20s %s",
        "KIND",
        "CH",
        "MSG",
        "BYTES"
    );

    mvwhline(
        labels_,
        1,
        0,
        ACS_HLINE,
        cols_
    );

    wrefresh(labels_);
}

void MidiUi::drawTable() {
    werase(log_);

    int row = 0;

    for (const auto& evOpt : latest_) {

        if (!evOpt.has_value())
            continue;

        const RawEvent& ev = *evOpt;

        mvwprintw(log_, row++, 0, "%-12s %-4d %-20s %s",
            kind(ev.kind),
            ev.channel,
            formatMessage(ev).c_str(),
            hexBytes(ev).c_str()
        );
    }

    wrefresh(log_);
}