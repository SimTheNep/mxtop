// Just serves as debug once main.cpp has this implemented
// Actually I might just make it permanent in the end and have it be invoked by main lol
// I'd probabaly have to make a prompt instead of arguments for that though
#include "parser.hpp"
#include <fstream>
#include <iostream>

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <path/to/json>\n";
        return 1;
    }

    std::string path = argv[1];

    std::ifstream f(path);
    if (!f)
    {
        std::cerr << "Couldn't open " << path << "\n";
        return 1;
    }

    nlohmann::json j;
    f >> j;

    // Pick parser based on filename
    std::string filename = path.substr(path.find_last_of('/') + 1);

    if (filename == "layouts.json")
    {
        debugLayouts(parseLayouts(j));
    }
    else if (filename == "module.json")
    {
        debugModule(parseModule(j));
    }
    else if (filename == "dictionary.json") // THIS STUPID SHIT BROKE THE FUCK OUT OF MY CODE FOR HOURS I HATE YOU SO MUCH
    {
        debugDictionary(parseDictionary(j));
    }
    else
    {
        std::cerr << "unrecognized file " << filename << "\n";
        return 1;
    }

    return 0;
}