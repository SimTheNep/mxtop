#pragma once

#include "../../midi_reader/types.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <ncurses.h>

// ncurses view of what's currently being read
// Three windows: Info (port, file, timestamp), Labels (table header), Table (data)
struct MidiUi {
public:

    MidiUi(
        const std::vector<std::string>& files,
        const std::vector<std::string>& ports
    );

    ~MidiUi();

    // debug is currently unused, will be used for this UI when final UI is implemented
    void addEvent(const RawEvent& ev, bool debug);

private: // internal UI drawing stuff, don't touch
    void drawInfo();
    void drawLabels();
    void drawTable();

    std::string formatMessage(const RawEvent& ev);

    WINDOW* info_ = nullptr;
    WINDOW* labels_ = nullptr;
    WINDOW* log_ = nullptr;

    int rows_ = 0;
    int cols_ = 0;

    double lastTimestamp_ = 0.0;

    std::vector<std::string> ports_;
    std::vector<std::string> files_;

    // Only holds the most recent event of its kind
    std::array<std::optional<RawEvent>, 8> latest_;
};