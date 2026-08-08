#pragma once

#include "../../json_parser/parser.hpp"
#include "../../midi_reader/midi_load.hpp"
#include "../../midi_reader/types.hpp"
#include "../../state_layer/state.hpp"
#include "../settings.hpp"

#include <deque>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

// Formatted channel view items
struct ChCell {
    std::string text;
    int width = 0;
};

struct ChRow {
    int channelId = 0;
    bool isActive = false;
    bool isSelected = false;
    bool hasData = false;
    int activeNotes = 0;
    std::vector<int> heldNotes;
    std::vector<ChCell> cells;
};

// Formatted log entry
struct LogEntry {
    std::string timecode;
    std::string text;
    std::string type; // "note", "sysex", "cc", "pc"
};

class UiModel {
public:
    UiModel() = default;

    void init(const layoutDef& layouts, const moduleDef& module);
    void updtSize(int width, int height);

    // Terminal dimension getters
    int termWidth() const { return termWidth_; }
    int termHeight() const { return termHeight_; }

    // Channel selection navigation (Up / Down)
    // void selNextCh();
    // void selPrevCh();
    // void giveSelChannel(int ch);
    // int selChannel() const { return selChannel_; }
    void setTotalChannels(int totalChannels);

    // Multi-port page navigation (Left / Right)
    void selNextPage();
    void selPrevPage();
    int currentPage() const { return currentPage_; }
    int totalPages() const;

    void pushSnap(int channel, const takeSnapshot& snap, double elapsedMs); // Snapshot
    void pushEvent(const RawEvent& ev, double elapsedMs); // MIDI events

    const layoutType& activeProfile() const;
    const std::string& activeProfileName() const { return activeProfileName_; }

    std::vector<std::string> getColHeader() const;
    std::vector<ChRow> getChRows() const;

    std::vector<std::pair<std::string, std::string>> getSystemFx() const;
    std::vector<std::pair<std::string, std::string>> getMasterOut() const;

    const std::deque<LogEntry>& getLogEntries() const { return logBuffer_; }
    const std::vector<int>& getHistory() const { return activityBins_; }

    int polyCount() const { return totalPoly_; }
    const moduleDef& module() const { return module_; }

    // VU-meter settings
    float meterLevel(int channel) const;
    float meterPeak(int channel) const;

    // Applies loaded settings
    void applySettings(const Settings& settings) { settings_ = settings; }
    const Settings& settings() const { return settings_; }
    ColorPalette palette() const { return settings_.palette(); }

private:
    std::string selProfile(int width, int height) const;
    std::string formatField(int ch, const std::string& fieldId, const takeSnapshot& snap) const;
    std::string formatDefaultField(const std::string& fieldId) const;
    std::string formatNote(int note) const;

    // True if channel had a held note within the last settings_.noteOffGraceMs
    bool channelRinging(int channel, double nowMs) const;

    // Per-channel VU meter parameters
    struct MeterState {
        float level = 0.f;
        float peak = 0.f;
        double peakHeldMs = 0.0;
        double lastUpdateMs = -1.0;
    };

    int totalChannels_ = 0;

    Settings settings_; // Defaults match the old hardcoded constants until a menu overrides them

    layoutDef layouts_;
    moduleDef module_;
    std::unordered_map<std::string, const ModuleObject*> objectMap_;

    std::string activeProfileName_ = "full";
    int termWidth_ = 160;
    int termHeight_ = 42;

    int selChannel_ = 0;
    int currentPage_ = 0;

    int totalPoly_ = 0;
    std::unordered_map<int, takeSnapshot> snapshots_;
    takeSnapshot sysSnapshot_;

    std::unordered_map<int, MeterState> meters_;
    std::unordered_map<int, double> lastActiveMs_; // Channel -> elapsedMs when it last had polyCount > 0
    double lastElapsedMs_ = 0.0;

    std::deque<LogEntry> logBuffer_;
    std::vector<int> activityBins_{std::vector<int>(32, 0)};

    static constexpr size_t kMaxLogEntries = 15;
};