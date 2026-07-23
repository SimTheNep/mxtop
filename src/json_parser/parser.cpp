#include "parser.hpp"
#include <iostream>
#include <fstream>


static std::map<std::string, std::vector<std::string>>

// Should give something along the lines of "patches": { "bank 1": ["msb","lsb"] }
parseGroups(const nlohmann::json& j) {
    std::map<std::string, std::vector<std::string>> out;

    for (auto& [key, val] : j.items())
        out[key] = val.get<std::vector<std::string>>();
    return out;
}

// LAYOUTS.JSON
// 
//

LayoutsDef parseLayouts(const nlohmann::json& j) {
    LayoutsDef def;

    for (auto& [variantName, variantJson] : j.items()) {
        LayoutVar v;

        if (variantJson.contains("views"))
            v.views = parseGroups(variantJson.at("views"));

        // Only full has widgets
        if (variantJson.contains("widgets"))
            v.widgets = parseGroups(variantJson.at("widgets"));

        def.variants[variantName] = std::move(v);
    }

    return def;
}


// Debug printer
void debugPrintLayouts(const LayoutsDef& layouts) {
    for (auto& [name, variant] : layouts.variants) {
        std::cout << "variant: " << name << "\n";

        for (auto& [section, ids] : variant.views)
            std::cout << "  views." << section
                      << " (" << ids.size() << " items)\n";

        for (auto& [section, ids] : variant.widgets)
            std::cout << "  widgets." << section
                      << " (" << ids.size() << " items)\n";
    }
}