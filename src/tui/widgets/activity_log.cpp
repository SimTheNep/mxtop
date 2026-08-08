#include "activity_log.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

// ACTIVITY LOG WIDGET
//
// Draws the 4x4 per-channel activity grid with chord name identification

using namespace ftxui;

namespace {
    static const char* kNoteNames[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    std::string formatNoteName(int midiNote) {
        if (midiNote < 0 || midiNote > 127) return "---";
        int octave = (midiNote / 12) - 1;
        int nameIdx = midiNote % 12;
        return std::string(kNoteNames[nameIdx]) + std::to_string(octave);
    }

    // CHORD IDENTIFIER
    //
    //

    std::string identifyChord(const std::vector<int>& rawNotes) {
        if (rawNotes.empty()) return "---";

        // Extract unique pitch classes to eliminate octave sensitivity
        std::set<int> pitchClasses;
        int lowestNote = 127;
        
        for (int n : rawNotes) {
            if (n < 0) continue; // Ignore invalid pitch numbers
            int pc = (n % 12 + 12) % 12;
            pitchClasses.insert(pc);
            if (n < lowestNote) lowestNote = n;
        }

        if (pitchClasses.empty()) return "---";

        // Single note, no octave sensitivity (C3 + C4 + C5 combined)
        if (pitchClasses.size() == 1) {
            return std::string(kNoteNames[*pitchClasses.begin()]);
        }

        // Two notes (check for power chords, including inverted 4ths)
        if (pitchClasses.size() == 2) {
            auto it = pitchClasses.begin();
            int pc1 = *it;
            int pc2 = *(++it);
            
            int interval = (pc2 - pc1 + 12) % 12;
            if (interval == 7) {
                return std::string(kNoteNames[pc1]) + "5";
            } else if (interval == 5) { // Inversion of perfect 5th (perfect 4th)
                return std::string(kNoteNames[pc2]) + "5";
            }
            return std::string(kNoteNames[pc1]) + " + " + std::string(kNoteNames[pc2]);
        }

        // 3+ notes (est against all possible roots, also handles inversions)
        for (int root = 0; root < 12; ++root) {
            std::set<int> intervals;
            for (int pc : pitchClasses) {
                intervals.insert((pc - root + 12) % 12);
            }

            // Chord formulas from semitone intervals from root
            if (intervals == std::set<int>{0, 4, 7})       return std::string(kNoteNames[root]) + " Maj";
            if (intervals == std::set<int>{0, 3, 7})       return std::string(kNoteNames[root]) + " min";
            if (intervals == std::set<int>{0, 3, 6})       return std::string(kNoteNames[root]) + " dim";
            if (intervals == std::set<int>{0, 4, 8})       return std::string(kNoteNames[root]) + " aug";
            if (intervals == std::set<int>{0, 2, 7})       return std::string(kNoteNames[root]) + " sus2";
            if (intervals == std::set<int>{0, 5, 7})       return std::string(kNoteNames[root]) + " sus4";
            if (intervals == std::set<int>{0, 4, 7, 11})   return std::string(kNoteNames[root]) + " Maj7";
            if (intervals == std::set<int>{0, 4, 7, 10})   return std::string(kNoteNames[root]) + " 7";
            if (intervals == std::set<int>{0, 3, 7, 10})   return std::string(kNoteNames[root]) + " m7";
            if (intervals == std::set<int>{0, 3, 6, 10})   return std::string(kNoteNames[root]) + " m7b5";
            if (intervals == std::set<int>{0, 3, 6, 9})    return std::string(kNoteNames[root]) + " dim7";
            if (intervals == std::set<int>{0, 4, 7, 2})    return std::string(kNoteNames[root]) + " add9";
            if (intervals == std::set<int>{0, 4, 7, 5})    return std::string(kNoteNames[root]) + " add11";
        }

        // Fallback for complex voicings
        int rootIdx = (lowestNote % 12 + 12) % 12;
        return std::string(kNoteNames[rootIdx]) + " Poly (" + std::to_string(pitchClasses.size()) + "n)";
    }
}

Element TuiWidgets::drawActivity(const UiModel& model) {
    const auto palette = model.palette();
    auto pageRows = model.getChRows();
    
    // Fetch pages from ui_model.cpp
    int page = model.currentPage();
    
    // Ensure it always has elements up to the full 16 channels
    while (pageRows.size() < 16) {
        ChRow dummy{};
        dummy.channelId = static_cast<int>(page * 16 + pageRows.size());
        dummy.isActive = false;
        dummy.hasData = false;
        dummy.cells.resize(1, {"---", 3});
        pageRows.push_back(dummy);
    }

    Elements gridRows;
    constexpr int kCols = 4; // 4x4 grid layout
    constexpr int kChordWidth = 14; // Fixed width for chord labels
    
    for (size_t rIdx = 0; rIdx < pageRows.size(); rIdx += kCols) {
        Elements currentHBox;
        for (int c = 0; c < kCols; ++c) {
            if (rIdx + c >= pageRows.size()) break;
            const auto& r = pageRows[rIdx + c];
            char chTag[16];
            char portChar = 'A' + static_cast<char>(r.channelId / 16);
            std::snprintf(chTag, sizeof(chTag), "%c%02d", portChar, (r.channelId % 16) + 1);

            // Feed exact held notes to the chord identifier
            std::string chordLabel = r.isActive ? identifyChord(r.heldNotes) : "---";

            Element chBadge = text(std::string(chTag)) | bold;
            Element noteElem = text(chordLabel);

            if (r.isActive) {
                chBadge = chBadge | color(palette.activityBadge);
                noteElem = noteElem | color(palette.activityChord) | bold;
            } else {
                chBadge = chBadge | color(palette.channelInactive);
                noteElem = noteElem | color(palette.channelInactive);
            }

            // Fixed-width formatting for less jitter
            Element cell = hbox({ 
                text(" "),
                chBadge | size(WIDTH, EQUAL, 4),
                text(" | "),
                noteElem | size(WIDTH, EQUAL, kChordWidth),
                text(" ")
            }) | flex;

            currentHBox.push_back(cell);
        }
        gridRows.push_back(hbox(std::move(currentHBox)) | flex);
    }

    std::string windowTitle = " ACTIVITY GRID ";

    return window(text(windowTitle) | bold | color(palette.activityChord), vbox(std::move(gridRows))) | flex;
}