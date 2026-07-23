// Just serves as debug once main.cpp has this implemented
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
    if (path.find("layout") != std::string::npos)
    {
        debugLayouts(parseLayouts(j));
    }
    else if (path.find("module") != std::string::npos)
    {
        debugModule(parseModule(j));
    }
    else
    {
        std::cerr << "Invalid JSON file\n";
    }

    return 0;
}