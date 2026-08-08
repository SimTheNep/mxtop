#pragma once

#include "../ui_model.hpp"
#include <ftxui/dom/elements.hpp>

namespace TuiWidgets {
    // System metric parser
    struct SystemLoadMetrics {
        float cpuPercent = 0.0f;
        float ramPercent = 0.0f;
        size_t ramUsedMb = 0;
    };

    SystemLoadMetrics getProcessLoad();
    ftxui::Element drawSystemLoad(const UiModel& model);
}