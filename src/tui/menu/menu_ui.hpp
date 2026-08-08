#pragma once

#include "../../settings.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <string>
#include <vector>

// Helper functions
std::string getRandomSplashText();
std::vector<std::string> getThemes();
std::vector<ModuleOverride> getModules();
std::string getThemeDesc(const std::string& themeName);
std::string lowerStr(std::string s);
std::string autoComplete(const std::string& input);
void clearWord(std::string& str);
int charToPort(char c);
std::string formatPorts(const std::vector<unsigned int>& ports);
double getTimeSec();
ftxui::Element scanlineTransition(ftxui::Elements rows, double switchTime);

// Entry in the MIDI queue
struct queueSlot {
    std::string file;
    std::vector<unsigned int> ports;
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

    // RENDER
    ftxui::Element renderLogo() const;
    ftxui::Element renderTabs() const;
    ftxui::Element renderQueue() const;
    ftxui::Element renderSettings() const;
    ftxui::Element renderHelp() const;
    ftxui::Element renderFooter() const;

    // QUEUE HANDLING
    void addFilePrompt();
    void removeSelFile();
    void moveQueueUp();
    void moveQueueDown();
    void togglePort(unsigned int portIdx);

    // SETTINGS HANDLING
    void cycleSettingLeft();
    void cycleSettingRight();
    void cycleSetting(int delta); // -1 for left, +1 for right

    static constexpr int kNumSettings = 10;

    std::vector<std::string> availablePorts_;
    std::vector<queueSlot> queue_;

    Settings settings_;

    Pane activePane_ = Pane::Queue;
    int queueCursor_ = 0;
    int settingsCursor_ = 0;
    int portCursor_ = 0;

    bool addingFile_ = false;
    std::string fileInputBuf_;

    ftxui::ScreenInteractive screen_ = ftxui::ScreenInteractive::Fullscreen();

    std::string splashText_;
    double lastTabSwitchTime_ = 0.0;
};