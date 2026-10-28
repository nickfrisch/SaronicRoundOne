#include "simulation.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

//follow-on 1: cargo and dock operations
//everything follow-on 1 adds to Engine lives here, the base code only calls in at its "follow-on 1" hooks
//
//cargo mode is on only if init supplies dock_throughput, otherwise cargo commands and cargo_manifest are rejected
//like set_destination, commands are validated against committed state and only edit planned state:
//  dock needs committed idle, undock/load/unload/swap need committed docked
//  dock after dock and undock after undock are no-ops, dock after undock and undock after dock are rejected
//  load/unload/swap after undock in the same tick is fine (cargo moves, then the vessel leaves)
//throughput counts the minimum ops needed to reach the planned state, not commands issued:
//  load then unload of the same cargo is 0 ops, a separate load + unload by one vessel is 1 (could be one swap)

namespace {

const std::int64_t MAX_QUANTITY = std::numeric_limits<int>::max();

//Cargo never stores a zero quantity, so count() tells us if any unit exists
void Take(Cargo& c, int destination)
{
    if (--c[destination] == 0)
        c.erase(destination);
}

json CargoToJson(const Cargo& c)
{
    json out = json::array();

    for (const auto& [destination, quantity] : c)
        out.push_back({{"destination", destination}, {"quantity", quantity}});

    return out;
}

//minimum dock ops to turn one vessel inventory into another
//each unit gained needs a load, each unit lost needs an unload, and a swap covers one of each
std::int64_t OpsBetween(const Cargo& from, const Cargo& to)
{
    auto quantityOf = [](const Cargo& c, int d) -> std::int64_t
    {
        auto it = c.find(d);
        return it == c.end() ? 0 : it->second;
    };

    std::int64_t gained = 0;
    std::int64_t lost = 0;

    for (const auto& [d, q] : to)
        gained += std::max<std::int64_t>(0, q - quantityOf(from, d));

    for (const auto& [d, q] : from)
        lost += std::max<std::int64_t>(0, q - quantityOf(to, d));

    return std::max(gained, lost);
}

//reads v as an integer in [1, MAX_QUANTITY], name is only used in the error message
bool ReadPositive(const json& v, const std::string& name, std::int64_t& out, std::string& err)
{
    if (!v.is_number_integer())
    {
        err = name + " must be an integer";
        return false;
    }

    //nlohmann library parses non-negative integers as unsigned, so if signed we know number is negative
    if (!v.is_number_unsigned() || v.get<std::uint64_t>() < 1 || v.get<std::uint64_t>() > MAX_QUANTITY)
    {
        err = name + " must be between 1 and " + std::to_string(MAX_QUANTITY) + ", got " + v.dump();
        return false;
    }

    out = static_cast<std::int64_t>(v.get<std::uint64_t>());
    return true;
}

} //namespace

//hook in Handle, returns true (and fills out) if cmd is a cargo command
bool Engine::HandleCargoCommand(const std::string& cmd, const json& params, Result& out)
{
    if (cmd != "dock" && cmd != "undock" && cmd != "load" && cmd != "unload" && cmd != "swap")
        return false;

    if (!cargo_enabled_)
        out = Result::Error(cmd + " requires cargo mode (init with dock_throughput)");

    else if (cmd == "dock" || cmd == "undock")
        out = SetDockedCommand(params, cmd == "dock");

    else if (cmd == "load")
        out = TransferCommand(params, "destination", nullptr);

    else if (cmd == "unload")
        out = TransferCommand(params, nullptr, "destination");

    else
        out = TransferCommand(params, "load_destination", "unload_destination");

    return true;
}

//hook in InitCommand, validates dock_throughput + optional cargo_manifest and only commits if all valid
//must stay the last validation in InitCommand so a failure changes nothing
bool Engine::InitCargo(const json& params, int n, std::string& err)
{
    if (!params.contains("dock_throughput"))
    {
        if (!params.contains("cargo_manifest"))
            return true; //base mode

        err = "cargo_manifest requires dock_throughput";
        return false;
    }

    const json& throughputObject = params["dock_throughput"];
    if (!throughputObject.is_array() || static_cast<int>(throughputObject.size()) != n)
    {
        err = "dock_throughput must be an array of " + std::to_string(n) + " integers";
        return false;
    }

    std::vector<std::int64_t> throughput(n);
    for (int i = 0; i < n; i++)
    {
        if (!ReadPositive(throughputObject[i], "dock_throughput[" + std::to_string(i) + "]", throughput[i], err))
            return false;
    }

    std::vector<Cargo> docks(n);
    if (!ReadManifest(params, n, docks, err))
        return false;

    //all valid, commit
    cargo_enabled_ = true;
    dock_throughput_ = std::move(throughput);
    docks_ = std::move(docks);

    return true;
}

//hook in TickCommand, adds the tick's cargo_manifest to the docks
//a bad manifest changes nothing
bool Engine::TickCargo(const json& params, std::string& err)
{
    if (!params.contains("cargo_manifest"))
        return true;

    if (!cargo_enabled_)
    {
        err = "cargo_manifest requires cargo mode (init with dock_throughput)";
        return false;
    }

    //work on a copy so a bad entry halfway through doesn't leave partial cargo behind
    std::vector<Cargo> docks = docks_;
    if (!ReadManifest(params, GetN(), docks, err))
        return false;

    docks_ = std::move(docks);
    return true;
}

//hook in GetWorldState, adds vessel and dock cargo to the serialized world
void Engine::AddCargoToWorldState(json& world) const
{
    if (!cargo_enabled_)
        return;

    for (const auto& [id, v] : planned_)
        world["vessels"][id]["cargo"] = CargoToJson(v.cargo);

    json docks = json::array();

    for (int i = 0; i < GetN(); i++)
        docks.push_back({{"location", i}, {"cargo", CargoToJson(docks_[i])}});

    world["docks"] = docks;
}

//dock:   committed idle -> planned docked
//undock: committed docked -> planned idle (pending cargo ops still happen this tick)
Result Engine::SetDockedCommand(const json& params, bool dock) {
    std::string id, err;
    const Vessel* committed = FindVessel(params, id, err);
    if (!committed)
        return Result::Error(err);

    if (committed->state != (dock ? VesselState::IDLE : VesselState::DOCKED))
        return Result::Error("vessel " + id + (dock ? " is not idle" : " is not docked"));

    //dock starts from the committed vessel so it overrides a pending set_destination
    if (dock)
        planned_[id] = *committed;

    planned_[id].state = dock ? VesselState::DOCKED : VesselState::IDLE;
    return GetWorldState();
}

//load, unload and swap
//loadKey/unloadKey name the parameter holding each half's cargo destination, nullptr means that half isn't used
Result Engine::TransferCommand(const json& params, const char* loadKey, const char* unloadKey) {
    std::string id, err;
    const Vessel* committed = FindVessel(params, id, err);
    if (!committed)
        return Result::Error(err);

    if (committed->state != VesselState::DOCKED)
        return Result::Error("vessel " + id + " is not docked");

    int loadDest = -1;
    int unloadDest = -1;
    if (loadKey && !ReadLocation(params, loadKey, GetN(), loadDest, err))
        return Result::Error(err);

    if (unloadKey && !ReadLocation(params, unloadKey, GetN(), unloadDest, err))
        return Result::Error(err);

    const int loc = committed->currLocation;
    Cargo vessel = planned_[id].cargo;
    Cargo dock = docks_[loc];

    //both halves are checked before either is applied, so a swap can't load the unit it unloads
    if (loadKey && !dock.count(loadDest))
        return Result::Error("dock " + std::to_string(loc) + " has no cargo for destination " + std::to_string(loadDest));

    if (unloadKey && !vessel.count(unloadDest))
        return Result::Error("vessel " + id + " has no cargo for destination " + std::to_string(unloadDest));

    if (loadKey)
    {
        Take(dock, loadDest);
        ++vessel[loadDest];
    }

    if (unloadKey)
    {
        Take(vessel, unloadDest);

        //cargo unloaded at its destination is delivered and leaves the sim
        if (unloadDest != loc)
            ++dock[unloadDest];
    }

    //only this vessel's share of the dock's pending ops changes
    const std::int64_t ops = PendingOps(loc) - OpsBetween(committed->cargo, planned_[id].cargo)
                                             + OpsBetween(committed->cargo, vessel);
    if (ops > dock_throughput_[loc])
        return Result::Error("transfer of " + std::to_string(ops) + " operations exceeds dock throughput of " +
                             std::to_string(dock_throughput_[loc]) + " at location " + std::to_string(loc));

    planned_[id].cargo = std::move(vessel);
    docks_[loc] = std::move(dock);
    return GetWorldState();
}

//adds a cargo_manifest's entries to docks
//callers pass a copy and only keep it on success
bool Engine::ReadManifest(const json& params, int n, std::vector<Cargo>& docks, std::string& err) const
{
    if (!params.contains("cargo_manifest"))
        return true; //optional

    const json& manifestObject = params["cargo_manifest"];
    if (!manifestObject.is_array())
    {
        err = "cargo_manifest must be an array";
        return false;
    }

    for (std::size_t i = 0; i < manifestObject.size(); i++)
    {
        const json& entry = manifestObject[i];
        const std::string where = "cargo_manifest[" + std::to_string(i) + "]: ";

        if (!entry.is_object())
        {
            err = where + "entry must be an object";
            return false;
        }

        int location = 0;
        int destination = 0;
        std::int64_t quantity = 0;
        if (!ReadLocation(entry, "location", n, location, err) ||
            !ReadLocation(entry, "destination", n, destination, err) ||
            !ReadPositive(entry.value("quantity", json()), "quantity", quantity, err))
        {
            err = where + err;
            return false;
        }

        docks[location][destination] += quantity;
    }

    return true;
}

//same vessel_id checks as set_destination, returns the committed vessel or nullptr
const Vessel* Engine::FindVessel(const json& params, std::string& id, std::string& err) const
{
    if (!params.contains("vessel_id") || !params["vessel_id"].is_string())
    {
        err = "vessel_id must be a string";
        return nullptr;
    }

    id = params["vessel_id"].get<std::string>();

    auto it = committed_.find(id);
    if (it == committed_.end())
    {
        err = "unknown vessel: " + id;
        return nullptr;
    }

    return &it->second;
}

//ops needed this tick at a dock, summed over every vessel committed docked there
std::int64_t Engine::PendingOps(int location) const
{
    std::int64_t total = 0;

    for (const auto& [id, v] : committed_)
    {
        if (v.state == VesselState::DOCKED && v.currLocation == location)
            total += OpsBetween(v.cargo, planned_.at(id).cargo);
    }

    return total;
}
