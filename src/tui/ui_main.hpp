#pragma once

#include "ui_model.hpp"
#include "layouts.hpp"
#include "../midi_reader/midi_load.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class MidiUi {
public:
    MidiUi(const layoutDef& layouts, const moduleDef& module, const MidiMeta& meta);
    ~MidiUi();

    void start();
    void stop();

    void addSnap(int channel, const takeSnapshot& snap, double elapsedMs);
    void addEvent(const RawEvent& ev);

private:
    void renderLoop();
    std::string getCurrentTimeStr() const;

    UiModel model_;
    MidiMeta meta_;

    ftxui::ScreenInteractive screen_ = ftxui::ScreenInteractive::Fullscreen();
    std::thread uiThread_;
    std::atomic<bool> running_{false};
    std::mutex dataMutex_;

    double lastElapsedMs_ = 0.0;
};