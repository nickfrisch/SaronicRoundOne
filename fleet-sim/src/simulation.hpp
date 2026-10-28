#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

using Tick = std::int64_t; //ai suggestion, makes it clear which variables follow properties of "tick" (monotonically increasing)
                            //64-bit means tick_ + travel time won't overflow

//cargo is identified only by its destination: destination -> quantity, never storing 0
using Cargo = std::map<int, std::int64_t>;

//third-party
#include <nlohmann/json.hpp>

using json = nlohmann::json;



struct Result {
    bool ok;
    json payload;

    static Result Ok(json p) {return {true, std::move(p)}; }
    static Result Error(const std::string& msg) {return {false, json{{"message", msg}}}; }
};

enum class VesselState
{
    IDLE,
    TRANSIT,
    DOCKED //from follow-on 1
};

struct Vessel {
    int currLocation = 0;         //while in transit this is the "from" location
    int destination = -1;         //only meaningful in transit; -1 sentinel otherwise
    VesselState state = VesselState::IDLE;
    Tick arrivesAtTick = 0;

    Cargo cargo; //follow-on 1 addition

    json ToJson() const; //ai suggestion, member function reads better
};

class Engine {
    public: 
        //handle requests and output Result objects
        //orchestrate incoming requests to their proper operations
        Result Handle(const json& request);

    private:
        Result InitCommand(const json& params);
        Result TickCommand(const json& params);
        Result SetDestinationCommand(const json& params);
        Result GetStateCommand(const json& params);

        //helper to validate and parse locations
        static bool ReadLocation(const json& params, const char* key, int n, int& out, std::string& err);

        //serialize the planned state
        Result GetWorldState() const;

        int GetN() const { return static_cast<int>(travel_times_.size()); }

        bool initialized_ = false;
        Tick tick_ = 0;

        //travel_times is NxN where:
        //travel_times[i][j] represents time to go from loc_i to loc_j => diags are 0
        //loc_i in [0, N-1] where N is dim of travel_times
        std::vector<std::vector<int>> travel_times_;

        //store planned and committed states using two unordered_maps
        //committed_: world as of last tick
        //planned_: committed_ + effects of all commands since last ticks
        std::unordered_map<std::string, Vessel> committed_;
        std::unordered_map<std::string, Vessel> planned_;

        //follow-on 1
        //implementations live in simulation_cargo.cpp to make it clear what additions were made
        //cargo mode is only enabled when init supplies dock_throughput

        //hooks, each validates first and changes nothing on failure
        bool HandleCargoCommand(const std::string& cmd, const json& params, Result& out); //true if cmd is a cargo command
        bool InitCargo(const json& params, int n, std::string& err); //dock_throughput + optional cargo_manifest
        bool TickCargo(const json& params, std::string& err); //adds the tick's cargo_manifest to the docks
        void AddCargoToWorldState(json& world) const;

        Result SetDockedCommand(const json& params, bool dock); //dock / undock
        Result TransferCommand(const json& params, const char* loadKey, const char* unloadKey); //load / unload / swap

        bool ReadManifest(const json& params, int n, std::vector<Cargo>& docks, std::string& err) const;
        const Vessel* FindVessel(const json& params, std::string& id, std::string& err) const;
        std::int64_t PendingOps(int location) const;

        bool cargo_enabled_ = false;
        std::vector<std::int64_t> dock_throughput_; //static config, indexed by location

        //dock inventories, no committed copy needed since dock changes are never rolled back
        //and validation only needs the planned inventory
        std::vector<Cargo> docks_;

        //follow-on 2
        //implementations live in simulation_history.cpp

        Result GetStateAtCommand(const json& params);

        //hook in init/tick, snapshot the just-committed state into history_
        void RecordHistory();

        std::vector<json> history_; //history_[t] = committed world state at tick t
};