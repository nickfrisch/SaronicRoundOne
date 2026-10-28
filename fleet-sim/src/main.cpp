#include <iostream>
#include <string>
#include "simulation.hpp"

int main() {
    //speed up iostream operations
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    Engine sim;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) 
            continue;  // skip blank lines

        Result r;
        try 
        {
            r = sim.Handle(json::parse(line));
        }
        catch (const json::parse_error& e) 
        {
            r = Result::Error(std::string("invalid JSON: ") + e.what());
        } 
        catch (const json::exception& e) 
        {
            r = Result::Error(std::string("malformed command: ") + e.what());
        }

        json out = {{"status", r.ok ? "ok" : "error"}, {"payload", r.payload}};
        std::cout << out.dump() << std::endl; //make sure to flush the stream, not just insert a newline
    }
    return 0;
}
