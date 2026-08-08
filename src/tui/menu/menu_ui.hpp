#pragma once

#include "../../settings.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <string>
#include <vector>

// Entry in the MIDI queue
struct queueSlot {
    std::string file;
    unsigned int port = 0;
};

// What main should do once MenuUi returns
enum class menuAction {
    Play,
    Quit
};

// Send settings and all to main
struct menuReturn {
    menuAction action = menuAction::Quit;
    std::vector<queueSlot> queue;
    Settings settings;
};

class menuUi {
public:
    explicit menuUi(const std::vector<std::string>& availablePorts, Settings settings = loadSettings());

    // Blocks until the user starts playback or quits
    menuReturn run();

private:
    enum class Pane { Queue, Settings, Help };

    // Render
    ftxui::Element renderLogo() const;
    ftxui::Element renderTabs() const;
    ftxui::Element renderQueue() const;
    ftxui::Element renderSettings() const;
    ftxui::Element renderHelp() const;
    ftxui::Element renderFooter() const;

    // Queue
    void addFile();
    void removeSelFile();
    void moveQueueUp();
    void moveQueueDown();

    // Settings
    void cycleSettingLeft();
    void cycleSettingRight();

    std::vector<std::string> availablePorts_;
    std::vector<queueSlot> queue_;

    Settings settings_;

    Pane activePane_ = Pane::Queue;
    int queueCursor_ = 0;
    int settingsCursor_ = 0;

    bool addingFile_ = false;
    std::string fileInputBuf_;

    ftxui::ScreenInteractive screen_ = ftxui::ScreenInteractive::Fullscreen();
};