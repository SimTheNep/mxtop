#pragma once

#include "ui_model.hpp"
#include "widgets/system_resources.hpp"
#include "widgets/status_log.hpp"
#include "widgets/activity_log.hpp"
#include "../midi_reader/midi_load.hpp"

#include <ftxui/dom/elements.hpp>
#include <string>

namespace LayoutCommon {

    // Main layout profile router (Full, Compact, Tiny)
    ftxui::Element drawLayout(const UiModel& model, const MidiMeta& meta, double elapsedMs, const std::string& currentTimeStr);

    // Section component builders
    ftxui::Element drawHeader(const UiModel& model, const MidiMeta& meta, double elapsedMs);
    ftxui::Element drawChTable(const UiModel& model);
    ftxui::Element drawPageSelector(const UiModel& model);
    ftxui::Element drawSystemFx(const UiModel& model);
    ftxui::Element drawMasterOutput(const UiModel& model);
    ftxui::Element drawFooter(const UiModel& model, const std::string& currentTimeStr);

} // namespace LayoutCommon