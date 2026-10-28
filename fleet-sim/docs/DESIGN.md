# Design Doc

<!-- Written by hand (AI may not be used for the design doc). -->

## Overview
For this project round I chose to develop the Fleet Simulation Engine as I believe it more closely aligns with the work I would be doing in my role at Saronic and it allowed me to use C++ which I am much more comfortable with compared to Rust.  I was able to complete the base project as well as the additional follow-ons, choosing to separate them in the codebase to show the progression of the project.  I looked at upgrading to C++20/23 from C++17 but after my initial implementation, I did not find enough potential improvements to warrant the switch.

## Run/Test
README.md defines how to build, run, and test the engine.  It also gives an overview of the project structure.

## Architecture
This project follows the REPL architecture pattern where the engine loops while reading input and giving out single output.  Once a command is sent through std::cin, the Engine's Handle function validates and evaluates the given command.  Evaluation is only started once validation is complete and individual function blocks handle the command specific logic.  simulation.cpp holds the base engine logic plus some additional hooks and functions for the follow-ons.  I tried to separate the follow-on logic from the base logic as best I could to show the progress of the engine as more features were added.

The primary design of the engine is the idea of "committed" and "planned" states.  A planned state is seen as a mutable "future" that changes with every command until a "tick" commits the planned state into the engine.  To achieve this in the code, I chose to make two copies of the vessel map, committed_ and planned_ which are only edited by respective engine commands (tick for committed_, all else for planned_).  I could store the history of what commands are actually done to the committed states and then "replay" to find the most recent state, but I feel that having the duplicate datastructs is much more convenient to work with.  

## Tick Strategy
The most important command is "tick" as this handles all of the various operations that could be done to the vessels and must keep track that many different operations happen in the correct order.  Firstly, the planned state becomes the committed state.  Then the tick count is incremented by one to allow for vessels to arrive at a tick and report "idle" in the same tick.  Arrivals are processed next which sets a vessel's location and makes it become idle.  Finally, the planned state is made to be the same as the committed state so that operations during the next tick are applied on top of the correct state.

The cargo manifest is applied first, so a bad manifest will reject a whole tick.  Additionally, the history snapshot is recorded last so that we only commit valid ticks to the history.

## Transit
Vessels store arrivesAtTick rather than a counter of their time traveling.  Since vessels cannot have their destination updated after a set_destination command is committed, there is no need to track progress as there is no scenario where we need to know how far along the vessel is on its journey.  Once a valid set_destination command is committed, a vessel is stuck on that journey until it finishes.

## Open-ended decisions
Most of these decisions rely on the fact that a command is valid depending on the committed state during the tick they are sent during.  Duplicate operations or inverse operations don't actually clash even if they may seem like they do.
- set_destination: I decided that a set_destination to the vessel's current location will be treated as a "stay put" command.  It will cancel any pending commands to move the vessel (previous set_destination calls).  I don't treat it as an error as exact vessel location may not be known in this simulation if a user blindly starts commanding an already running simulation.  This mimics real life operations where an operator would rather be sure that a vessel stays in its location rather than deal with an error.
- duplicate dock and undock: The second command acts exactly the same.  The committed state still hasn't changed so the second command is just as valid as the first.  It will still set the same exact properties except in this case those properties will already be correct.
- undock after dock/dock after undock: These commands need to be rejected since undock and dock require that the committed state is valid, not the pending state.
- dock and set_destination: These two commands should override each other.  Both start from the same committed vessel, so whichever is last in order determines the planned state.
- undock and cargo operations in same tick: These two commands can coexist in the same tick in either order since the committed state hasn't changed.  The vessel is docked for the whole tick, so any cargo operations are valid.  
- throughput counting: Throughput must count the fewest operations to reach the pending state, not the number of commands.  The spec says a command is rejected "if the new pending state cannot be achieved within the dock_throughput limit".  This doesn't reference the number of commands, just the end pending state.  Thus, in the case of a load followed by an unload, the throughput cost is 0 since nothing changed.  I used the following formula following this observation: sum max(units gained, units lost) per vessel for each dock
- cargo "mode": Cargo output is only turned on when init includes dock_throughput.  This may go against the spec which says that init "now requires" dock_throughput. However, keeping with my design that the follow-ons being additions to the base project, I want the functionality to be separate such that all of the output for the base project can be observed independently from follow-on 1 or 2 functionality.
- docks output format: The full format is cut off in the PDF that I received so I had to assume what the correct output format was.  I chose an array of {location, cargo}.  

## Validation and Error Handling
Some portions of the project description are slightly unclear with what should be assumed or validated against for incoming data.  This uncertainty led to me implementing validation at all stages in case my assumptions were incorrect.  I made sure to check all values within an operation before committing the changes to the current planned state.  This made sure no modifications were halfway complete when being thrown out.  Malformed input cannot cause crashes so bad input still gets one response line.  The biggest issues I found were potential overflows caused by parsing of the json and invalid values for formatted integer input like init.  Putting guards in place for these meant that the engine cannot get into an unknown/dangerous state.

## Testing
The test suite that I created utilized AI to create many edge cases and generic cases to run through my engine.  After recreating the spec tests and identifying critical tests to run (invariants, edge cases, specific scenarios mentioned in the project spec), I guided an AI tool through generating more tests that followed those same patterns.  AI helped me find testing scenarios that I didn't consider before such as arbitrary vessel IDs with random unicode characters.  Tests were deemed right by line-by-line review for small tests and breakign down larger tests into smaller scenarios grouped together that could be analyzed individually.  Sanitizer builds also helped find bugs that may not directly appear in program output.  This testing sutie helped me catch overflow and zero quantity bugs that I had not considered.

## Other Considerations
I prioritized runtime over memory throughout the project.  I assume that a fleet simulator will be running on a powerful computer where memory is less of a concern than in an embedded system.  The performance metric that matters more is how long of a simulation that can be run (or how many ticks can be completed).  In a test with 1000 vessels over 2000 ticks, about 790 MB of memory was used.  In the future, I would work to reduce this memory overhead, starting with storing history more compactly as a series of operations rather than full world states.

## AI Usage
I believe that AI should be used as a tool to create boilerplate/repetitive code quickly and fill in knowledge/skill gaps.  I created and wrote the initial system design of the project and used AI to iterate upon my initial work to get the project where it is now.  I also had conversations with LLMs to help me understand confusing or vague portions of the project spec so that I could have a good initial try before polishing with AI.