#include "parser.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>

// PARSER_LAYOUTS.CPP
//
// Parses layout definitionss

using json = nlohmann::json;

// Helper to parse views and widgets
static std::unordered_map<std::string, layoutSect> parseGroups(const json& j) {
    std::unordered_map<std::string, layoutSect> out;

    for (const auto& [key, value] : j.items()) {
        out[key] = layoutSect{
            value.get<std::vector<std::string>>()
        };
    }

    return out;
}

// Parses the display variants
layoutDef parseLayouts(const json& j) {
    layoutDef def;

    for (const auto& [variantName, variantJson] : j.items()) {
        layoutType variant;

        if (variantJson.contains("views"))   { variant.views   = parseGroups(variantJson.at("views")); }
        if (variantJson.contains("widgets")) { variant.widgets = parseGroups(variantJson.at("widgets")); } // Only Full has widgets

        def.variants[variantName] = std::move(variant);
    }

    return def;
}