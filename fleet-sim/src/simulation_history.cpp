#include "simulation.hpp"

//follow-on 2: historical state queries
//everything follow-on 2 adds to Engine lives here, the base code only calls in at its "follow-on 2" hooks
//
//at the end of init and every tick planned == committed, so the world state json at that moment
//IS the committed state for that tick. we store it as-is in history_ (indexed by tick) so
//get_state_at(T) is just a lookup

//hook in InitCommand and TickCommand
void Engine::RecordHistory()
{
    //history_[t] is the committed state at tick t, ticks are consecutive from 0
    history_.push_back(GetWorldState().payload);
}

Result Engine::GetStateAtCommand(const json& params) {
    if (!params.contains("tick") || !params["tick"].is_number_integer())
        return Result::Error("tick must be an integer");

    const json& t = params["tick"];

    //nlohmann library parses non-negative integers as unsigned, so if signed we know number is negative
    if (!t.is_number_unsigned())
        return Result::Error("tick " + t.dump() + " is negative");

    if (t.get<std::uint64_t>() > static_cast<std::uint64_t>(tick_))
        return Result::Error("tick " + t.dump() + " is in the future (current tick: " + std::to_string(tick_) + ")");

    return Result::Ok(history_[t.get<std::uint64_t>()]);
}
