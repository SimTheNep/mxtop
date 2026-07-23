#include "parser.hpp"
#include <cstdio>
#include <fstream>

using json = nlohmann::json;

static std::unordered_map<std::string, layoutSect>

parseGroups(const json& j)
{
    std::unordered_map<std::string, layoutSect> out;

    for (const auto& [key, value] : j.items())
    {
        out[key] = layoutSect{
            value.get<std::vector<std::string>>()
        };
    }

    return out;
}

// LAYOUTS.JSON
// 
//

layoutDef parseLayouts(const json& j)
{
    layoutDef def;

    for (const auto& [variantName, variantJson] : j.items())
    {
        layoutVary variant;

        if (variantJson.contains("views")){variant.views = parseGroups(variantJson.at("views"));}
        if (variantJson.contains("widgets")){variant.widgets = parseGroups(variantJson.at("widgets"));} // Only Full has widgets

        def.variants[variantName] = std::move(variant);
    }

    return def;
}

// DEBUG
//
//

void debugLayouts(const layoutDef& layouts)
{
    for (const auto& [variantName, variant] : layouts.variants)
    {
    printf("Display: %s\n", variantName.c_str());

        for (const auto& [sectionName, section] : variant.views)
        {
            printf("    Parameter: %s\n", sectionName.c_str());
            for (const auto& id : section.items) { printf("        %s\n", id.c_str()); }
        }

        for (const auto& [sectionName, section] : variant.widgets)
        {
            printf("    Widget: %s\n", sectionName.c_str());
            for (const auto& id : section.items) { printf("        %s\n", id.c_str()); }
        }
    }
}