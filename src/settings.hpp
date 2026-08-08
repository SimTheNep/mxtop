#pragma once

#include <ftxui/screen/color.hpp>

#include <optional>
#include <string>

enum class NumeralFormat {
    Decimal,
    Hex
};

// Which module to firce
enum class ModuleOverride {
    Auto,
    SD90,
    GS,
    XG,
    GM2
};

// Component-specific colors
struct ColorPalette {
    // Global & Structural
    ftxui::Color background      = ftxui::Color::Default;
    ftxui::Color panelBorder     = ftxui::Color::GrayDark;
    ftxui::Color textPrimary     = ftxui::Color::White;
    ftxui::Color textDim         = ftxui::Color::GrayDark;
    ftxui::Color footerText      = ftxui::Color::GrayDark;

    // Header & Titles
    ftxui::Color headerTitle     = ftxui::Color::Green;
    ftxui::Color headerBpm       = ftxui::Color::Yellow;
    ftxui::Color headerTimeSig   = ftxui::Color::Cyan;
    ftxui::Color headerClock     = ftxui::Color::Cyan;
    ftxui::Color headerStatus    = ftxui::Color::Green;

    // Channel Table
    ftxui::Color tableHeader     = ftxui::Color::Cyan;
    ftxui::Color channelActive   = ftxui::Color::Green;
    ftxui::Color channelInactive = ftxui::Color::GrayDark;

    // VU Meters
    ftxui::Color vuLow           = ftxui::Color::Green;
    ftxui::Color vuMid           = ftxui::Color::Yellow;
    ftxui::Color vuHigh          = ftxui::Color::Red;
    ftxui::Color vuPeakMarker    = ftxui::Color::White;

    // System FX & Master Out
    ftxui::Color fxLabel         = ftxui::Color::Cyan;
    ftxui::Color fxValue         = ftxui::Color::Green;
    ftxui::Color masterLabel     = ftxui::Color::Yellow;
    ftxui::Color masterValue     = ftxui::Color::White;

    // Event Log
    ftxui::Color logSysEx        = ftxui::Color::Red;
    ftxui::Color logPc           = ftxui::Color::Green;
    ftxui::Color logCc           = ftxui::Color::Yellow;
    ftxui::Color logTempo        = ftxui::Color::Yellow;
    ftxui::Color logTimeSig      = ftxui::Color::Cyan;

    // Activity Grid
    ftxui::Color activityBadge   = ftxui::Color::Green;
    ftxui::Color activityChord   = ftxui::Color::Yellow;

    // System Load
    ftxui::Color loadPoly        = ftxui::Color::Green;
    ftxui::Color loadRam         = ftxui::Color::Yellow;
    ftxui::Color loadCpu         = ftxui::Color::Red;
};

struct Settings {
    // Display
    std::string theme = "base"; // Name of CSS file in src/themes/
    NumeralFormat numeralFormat = NumeralFormat::Decimal;
    ModuleOverride moduleOverride = ModuleOverride::Auto;

    bool showEventLog = true;
    bool showActivityGrid = true;
    int logMaxVisible = 14;

    // VU-meter
    double highlightTimeoutMs = 100.0;
    double noteOffGraceMs = 50.0;
    int vuBarWidth = 20;
    bool showPeakMarker = true;

    ColorPalette palette() const;
};

std::string toString(NumeralFormat format);
std::string toString(ModuleOverride mod);

std::optional<NumeralFormat> parseNumeralFormat(const std::string& s);
std::optional<ModuleOverride> parseModuleOverride(const std::string& s);

Settings loadSettings(const std::string& path = "settings.toml");
bool saveSettings(const Settings& settings, const std::string& path = "settings.toml");