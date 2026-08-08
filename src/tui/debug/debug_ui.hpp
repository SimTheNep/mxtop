#ifndef DEBUG_UI_HPP
#define DEBUG_UI_HPP

#include "../../midi_reader/types.hpp"
#include "../../state_layer/state.hpp"

#include <ncurses.h>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <array>
#undef border

class DebugUi {
public:
    DebugUi(const std::vector<std::string>& files, const std::vector<std::string>& ports);
    ~DebugUi();

    void addEvent(const RawEvent& ev, bool debug = false);
    void addSnap(int channel, const takeSnapshot& snap, double timestampMs);

private:
    void drawInfo();
    void drawLabels();
    void drawTable();

    void drawStateLabels();
    void drawStateTable();

    void checkScrollInput();
    std::string formatMessage(const RawEvent& ev);

    WINDOW* info_ = nullptr;
    WINDOW* labels_ = nullptr;
    WINDOW* log_ = nullptr; // Note: Used as an ncurses pad (newpad)

    int rows_ = 0;
    int cols_ = 0;

    int infoHeight_ = 0;
    int labelsHeight_ = 0;
    int scrollOffset_ = 0;
    int logPadRows_ = 500;

    std::vector<std::string> files_;
    std::vector<std::string> ports_;
    double lastTimestamp_ = 0.0;

    std::array<std::optional<RawEvent>, 10> latest_;
    std::map<int, takeSnapshot> stateSnapshots_;
};

#endif