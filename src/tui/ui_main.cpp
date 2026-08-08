#include "ui_main.hpp"

#include <ftxui/component/event.hpp>
#include <ftxui/screen/terminal.hpp>

#include <chrono>
#include <ctime>
#include <iomanip>

using namespace ftxui;

// UI_MAIN.CPP
//
// Constructs the UI and feeds it the snapshots



// LIFECYCLE
//
//

MidiUi::MidiUi(const layoutDef& layouts, const moduleDef& module, const MidiMeta& meta)
    : meta_(meta) {
    model_.init(layouts, module);
}

MidiUi::~MidiUi() {
    stop();
}

void MidiUi::start() {
    if (running_) return;
    running_ = true;

    uiThread_ = std::thread(&MidiUi::renderLoop, this);
}

void MidiUi::stop() {
    if (!running_) return;

    running_ = false;
    screen_.Exit();

    if (uiThread_.joinable()) {
        uiThread_.join();
    }
}

// DATA FEED
//
//

void MidiUi::addSnap(int channel, const takeSnapshot& snap, double elapsedMs) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    lastElapsedMs_ = elapsedMs;
    model_.pushSnap(channel, snap, elapsedMs);
    // ftxui only redraws on an event, so every incoming snapshot has to nudge it for page changes
    screen_.PostEvent(Event::Custom);
}

void MidiUi::addEvent(const RawEvent& ev) {
    std::lock_guard<std::mutex> lock(dataMutex_);

    if (ev.kind == MsgKind::Meta || (ev.data.size() >= 3 && ev.data[0] == 0xFF)) {
        //  BPM tracking
        if (ev.data.size() >= 6 && ev.data[0] == 0xFF && ev.data[1] == 0x51) {
            uint32_t mpqn = (static_cast<uint32_t>(ev.data[3]) << 16) |
                            (static_cast<uint32_t>(ev.data[4]) << 8)  |
                             static_cast<uint32_t>(ev.data[5]);
            if (mpqn > 0) {
                meta_.bpm = 60000000.0 / static_cast<double>(mpqn);
            }
        }
        //  Time signature tracking
        else if (ev.data.size() >= 5 && ev.data[0] == 0xFF && ev.data[1] == 0x58) {
            meta_.timeSigNum = ev.data[3];
            meta_.timeSigDenom = 1 << ev.data[4];
        }
    }

    model_.pushEvent(ev, ev.timestamp);
    screen_.PostEvent(Event::Custom);
}

std::string MidiUi::getCurrentTimeStr() const {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};

    // localtime_r isn't available on Windows, parity reasons
#if defined(_WIN32)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return std::string(buf);
}

// RENDER LOOP & INPUT
//
// 

void MidiUi::renderLoop() {
    auto component = Renderer([this] {
        std::lock_guard<std::mutex> lock(dataMutex_);

        auto dims = Terminal::Size();
        model_.updtSize(dims.dimx, dims.dimy);

        std::string clockStr = getCurrentTimeStr();

        // Render layout via layouts.cpp
        return LayoutCommon::drawLayout(model_, meta_, lastElapsedMs_, clockStr);
    });

    auto eventHandler = CatchEvent(component, [this](Event ev) {
        if (ev == Event::Character('q') || ev == Event::Escape) {
            stop();
            return true;
        }

        // Up/Down channel selection
        // if (ev == Event::ArrowUp || ev == Event::Character('k')) {
        //     std::lock_guard<std::mutex> lock(dataMutex_);
        //     // model_.selPrevCh();
        //     return true;
        // }

        // if (ev == Event::ArrowDown || ev == Event::Character('j')) {
        //     std::lock_guard<std::mutex> lock(dataMutex_);
        //     // model_.selNextCh();
        //     return true;
        // }

        // Left/Right Part A, Part B, Part C... page navigation
        if (ev == Event::ArrowLeft || ev == Event::Character('h')) {
            {
                std::lock_guard<std::mutex> lock(dataMutex_);
                model_.selPrevPage();
            }
            screen_.PostEvent(Event::Custom); // Force redraw
            return true;
        }

        if (ev == Event::ArrowRight || ev == Event::Character('l')) {
            {
                std::lock_guard<std::mutex> lock(dataMutex_);
                model_.selNextPage();
            }
            screen_.PostEvent(Event::Custom); // Force redraw
            return true;
        }

        return false;
    });

    screen_.Loop(eventHandler);
}