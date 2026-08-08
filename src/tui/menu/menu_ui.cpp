#include "menu_ui.hpp"

#include <ftxui/component/event.hpp>

#include <atomic>
#include <chrono>
#include <thread>

using namespace ftxui;

// MENU_UI.CPP
//
// Event loop and animations.

menuUi::menuUi(const std::vector<std::string>& availablePorts, Settings settings)
    : availablePorts_(availablePorts), settings_(std::move(settings)), splashText_(getRandomSplashText()) {}

menuReturn menuUi::run() {
    menuReturn result;

    // Background refresh thread (~30 FPS animation driver)
    std::atomic<bool> animating(true);
    std::thread animThread([this, &animating] {
        while (animating) {
            std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 FPS
            screen_.PostEvent(Event::Custom);
        }
    });

    auto exitMenu = [&] {
        animating = false;
        if (animThread.joinable()) animThread.join();
        screen_.Exit();
    };

    // Queue/Settings/Help
    auto component = Renderer([this] {
        Element body = text("");
        if (activePane_ == Pane::Queue)    body = renderQueue();
        if (activePane_ == Pane::Settings) body = renderSettings();
        if (activePane_ == Pane::Help)     body = renderHelp();

        return vbox({
            renderLogo(),
            renderTabs(),
            text(""),
            body | hcenter | flex, // Centered pane body
            filler(),
            renderFooter()
        }) | bgcolor(settings_.palette().background) | color(settings_.palette().textPrimary);
    });

    auto eventHandler = CatchEvent(component, [&](Event ev) {
        // Redraw frame on Custom Animation Tick
        if (ev == Event::Custom) {
            return false;
        }

        // Locked when typing in the queue prompt
        if (activePane_ == Pane::Queue && addingFile_) {
            if (ev == Event::Escape) {
                addingFile_ = false;
                fileInputBuf_.clear();
                return true;
            }

            if (ev == Event::Return) {
                if (!fileInputBuf_.empty()) {
                    queue_.push_back({ fileInputBuf_, { 0 } });
                    fileInputBuf_.clear();
                    addingFile_ = false;
                    queueCursor_ = static_cast<int>(queue_.size()) - 1;
                }
                return true;
            }

            // Ctrl + D, clear entire path
            if (ev == Event::Character("\x04")) {
                fileInputBuf_.clear();
                return true;
            }

            // Ctrl + W, clear previous Word
            if (ev == Event::Character("\x17")) {
                clearWord(fileInputBuf_);
                return true;
            }

            if (ev == Event::Tab || ev == Event::ArrowRight) {
                std::string ghost = autoComplete(fileInputBuf_);
                if (!ghost.empty()) fileInputBuf_ += ghost;
                return true;
            }

            if (ev == Event::Backspace) {
                if (!fileInputBuf_.empty()) fileInputBuf_.pop_back();
                return true;
            }

            if (ev.is_character()) {
                fileInputBuf_ += ev.character();
                return true;
            }

            return true;
        }

        // Global Keybindings (when not typing)
        if (ev == Event::Character('q') || ev == Event::Escape) {
            exitMenu();
            return true;
        }

        if (ev == Event::Tab) {
            activePane_ = (activePane_ == Pane::Queue) ? Pane::Settings
                : (activePane_ == Pane::Settings) ? Pane::Help : Pane::Queue;
            lastTabSwitchTime_ = getTimeSec(); // Record tab switch timestamp for animation
            return true;
        }

        // Queue Pane Keybindings
        if (activePane_ == Pane::Queue) {
            if (ev == Event::Character('a')) {
                addingFile_ = true;
                fileInputBuf_.clear();
                return true;
            }
            if (ev == Event::Character('d')) {
                if (!queue_.empty()) {
                    queue_.erase(queue_.begin() + queueCursor_);
                    if (queueCursor_ >= static_cast<int>(queue_.size()) && queueCursor_ > 0)
                        queueCursor_--;
                }
                return true;
            }
            if (ev == Event::ArrowUp || ev == Event::Character('k')) {
                if (queueCursor_ > 0) queueCursor_--;
                return true;
            }
            if (ev == Event::ArrowDown || ev == Event::Character('j')) {
                if (queueCursor_ < static_cast<int>(queue_.size()) - 1) queueCursor_++;
                return true;
            }

            // Press Enter to start playback
            if (ev == Event::Return) {
                if (!queue_.empty()) {
                    result.action = menuAction::Play;
                    result.queue = queue_;
                    exitMenu();
                }
                return true;
            }

            // Port Assignment (0..9..A..F)
            if (ev.is_character()) {
                char c = ev.character()[0];
                int port = charToPort(c);
                if (port >= 0 && port < 16) {
                    togglePort(static_cast<unsigned int>(port));
                    return true;
                }
            }
        }

        // Settings keybindings
        if (activePane_ == Pane::Settings) {
            if (ev == Event::ArrowUp || ev == Event::Character('k')) {
                if (settingsCursor_ > 0) settingsCursor_--;
                return true;
            }
            if (ev == Event::ArrowDown || ev == Event::Character('j')) {
                if (settingsCursor_ < kNumSettings - 1) settingsCursor_++;
                return true;
            }
            if (ev == Event::ArrowLeft || ev == Event::Character('h')) {
                cycleSettingLeft();
                return true;
            }
            if (ev == Event::ArrowRight || ev == Event::Character('l') || ev == Event::Return || ev == Event::Character(' ')) {
                cycleSettingRight();
                return true;
            }
        }

        return false;
    });

    screen_.Loop(eventHandler);

    // Stop animation thread on exit
    animating = false;
    if (animThread.joinable()) animThread.join();

    result.settings = settings_;
    return result;
}