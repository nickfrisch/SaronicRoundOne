#include "simulation.hpp"

//use pass-by-reference rather than std::optional
bool Engine::ReadLocation(const json& params, const char* key, int n, int& out, std::string& err)
{
    if (!params.contains(key) || !params.at(key).is_number_integer())
    {
        err = std::string(key) + " must be an integer";
        return false;
    }

    std::int64_t l = params.at(key).get<std::int64_t>(); //json library is in 64-bits

    if (l < 0 || l >= n)
    {
        //dump() echoes the original JSON number; v itself is wrong for values above LLONG_MAX
        err = std::string(key) + " " + params.at(key).dump() + " is out of range (0-" + std::to_string(n - 1) + ")";
        return false;
    }

    out = static_cast<int>(l);
    return true;
}

json Vessel::ToJson() const
{
    if (state == VesselState::TRANSIT)
        return {{"state", "transit"}, {"from", currLocation}, {"to", destination}, {"arrives_at_tick", arrivesAtTick}};

    //from follow-on 1
    if (state == VesselState::DOCKED) 
        return {{"state", "docked"}, {"location", currLocation}};

    return {{"state", "idle"}, {"location", currLocation}};
}

Result Engine::Handle(const json& request) {
    if (!request.is_object() || !request.contains("command") || !request["command"].is_string())
        return Result::Error("request must be an object with a string \"command\"");

    const std::string cmd = request["command"];
    const json params = request.contains("parameters") ? request["parameters"] : json::object();

    if (!params.is_object())
        return Result::Error("\"parameters\" must be an object");

    if (cmd == "init") {
        if (initialized_) return Result::Error("already initialized");
        return InitCommand(params);
    }

    if (!initialized_)
        return Result::Error("simulation not initialized; send init first");

    if (cmd == "tick")
        return TickCommand(params);

    else if (cmd == "set_destination")
        return SetDestinationCommand(params);

    else if (cmd == "get_state")
        return GetStateCommand(params);

    //follow-on 2
    else if (cmd == "get_state_at")
        return GetStateAtCommand(params);

    //follow-on 1
    Result cargoResult; 
    if (HandleCargoCommand(cmd, params, cargoResult))
        return cargoResult;

    //fallback return
    return Result::Error("unknown command: " + cmd);
}

Result Engine::GetWorldState() const
{
    json vessels = json::object();

    for (const auto& [id, v] : planned_)
        vessels[id] = v.ToJson();

    json world = {{"tick", tick_}, {"vessels", vessels}};

    //follow-on 1
    AddCargoToWorldState(world);
    
    return Result::Ok(world);
}

Result Engine::InitCommand(const json& params) {
    //Only commit once EVERYTHING is validated
    //project description reads like proper input can be assumed for MOST params,
    //but I will add validation to all just in case

    if (!params.contains("travel_times") || !params["travel_times"].is_array() || params["travel_times"].empty())
        return Result::Error("init requires a non-empty travel_times array");

    if (!params.contains("vessels") || !params["vessels"].is_array())
        return Result::Error("init requires a vessels array");

    const json& timesObject = params["travel_times"];
    const int N = static_cast<int>(timesObject.size());

    //travel_times must be NxN, diagonal 0, off-diagonal positive integers
    std::vector<std::vector<int>> travel_times(N, std::vector<int>(N, 0));
    for (int i = 0; i < N; i++)
    {
        if (!timesObject[i].is_array() || static_cast<int>(timesObject[i].size()) != N)
            return Result::Error("travel_times must be an NxN matrix");

        for (int j = 0; j < N; j++)
        {
            if (!timesObject[i][j].is_number_integer())
                return Result::Error("travel_times entries must be integers");

            //location validation
            //need to handle overflow, negatives, 0s
            if (i == j)
            {
                if (timesObject[i][j] != 0)
                    return Result::Error("travel_times diagonal must be 0");
                continue; //idx initialized to 0 already
            }
            
            //nlohmann library parses non-negative integers as unsigned, so if signed we know number is negative
            if (!timesObject[i][j].is_number_unsigned() || timesObject[i][j].get<std::uint64_t>() == 0) //cannot be 0 off-diagonal
                return Result::Error("travel_times off-diagonal entries must be non-zero positive");

            if (timesObject[i][j].get<std::uint64_t>() > std::numeric_limits<int>::max())
                return Result::Error("travel_times entries must be at most " + std::to_string(std::numeric_limits<int>::max()));

            travel_times[i][j] = static_cast<int>(timesObject[i][j].get<std::uint64_t>());
        }
    }

    std::unordered_map<std::string, Vessel> vessels;
    for (const json& v : params["vessels"])
    {
        if (!v.is_object() || !v.contains("id") || !v["id"].is_string())
            return Result::Error("each vessel requires a string id");

        const std::string id = v["id"].get<std::string>();
        if (vessels.count(id))
            return Result::Error("duplicate vessel id: " + id);

        Vessel newVessel;
        std::string err;
        if (!ReadLocation(v, "location", N, newVessel.currLocation, err))
            return Result::Error("vessel " + id + ": " + err);

        vessels[id] = newVessel;
    }

    //follow-on 1
    std::string cargoErr;
    if (!InitCargo(params, N, cargoErr))
        return Result::Error(cargoErr);

    //all valid, commit
    travel_times_ = std::move(travel_times);
    committed_ = std::move(vessels);
    planned_ = committed_;
    tick_ = 0;
    initialized_ = true;

    //follow-on 2
    RecordHistory();

    return GetWorldState();
}

Result Engine::TickCommand(const json& params) {
    //follow-on 1
    //add the tick's cargo_manifest first so a bad manifest rejects the whole tick
    std::string err;
    if (!TickCargo(params, err))
        return Result::Error(err);

    //planned state becomes the committed state
    committed_ = planned_;

    //advance tick count
    tick_++;

    //process arrivals (arriving at tick T reports IDLE at tick t)
    for (auto& [id, v] : committed_)
    {
        if (v.state == VesselState::TRANSIT && v.arrivesAtTick <= tick_)
        {
            v.currLocation = v.destination;
            v.destination = -1;
            v.state = VesselState::IDLE;
            v.arrivesAtTick = 0;
        }
    }

    //new planned starts from last committed
    planned_ = committed_;

    //follow-on 2
    RecordHistory(); //after all of this tick's transitions

    return GetWorldState();
}

Result Engine::SetDestinationCommand(const json& params) {
    //Valid only if committed state is idle; updates planned state
    //Rejected if vessel is in transit, vessel_id is unknown, or destination is out of range

    if (!params.contains("vessel_id") || !params["vessel_id"].is_string())
        return Result::Error("vessel_id must be a string");

    const std::string id = params["vessel_id"].get<std::string>();

    auto it = committed_.find(id);
    if (it == committed_.end())
        return Result::Error("unknown vessel: " + id);

    if (it->second.state != VesselState::IDLE)
        return Result::Error("vessel " + id + " is not idle");

    int nextDest = 0;
    std::string err;
    if (!ReadLocation(params, "destination", GetN(), nextDest, err))
        return Result::Error(err);

    //Start from the committed vessel so this overrides any earlier
    //set_destination issued since the last tick
    Vessel v = it->second;

    //treating destination == currLocation as "stay"
    //keep the vessel idle and cancel any pending set_destination command in planned
    if (nextDest != v.currLocation)
    {
        v.state = VesselState::TRANSIT;
        v.destination = nextDest;
        v.arrivesAtTick = tick_ + travel_times_[v.currLocation][nextDest];
    }

    planned_[id] = v;
    return GetWorldState();
}

Result Engine::GetStateCommand(const json& /*params*/) {
    return GetWorldState();
}