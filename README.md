# Fleet Simulation Engine (C++17)

Reads newline-delimited JSON commands on stdin, writes one JSON response line per command to stdout.

## Requirements
    g++ (or clang++) with C++17, make; bash + python3 for the test runner. No other dependencies.

## Build / run / test
    make            # builds build/fleet_sim
    make run        # interactive: type/pipe JSON lines
    make test       # runs every tests/cases/*/*.in against its .out
    make debug      # rebuild with ASanitizer + UBSanitizer (then `make clean && make` for a normal build)

    # run one test case by hand ('#' lines are comments for the test runner, so strip them)
    grep -v '^#' tests/cases/spec/base_example.in | ./build/fleet_sim

## Layout
    src/main.cpp          stdin/stdout loop, JSON (de)serialization of the envelope
    src/simulation.*      Engine class: command dispatch + base simulation logic
    src/simulation_cargo.cpp  [Follow-on 1] dock/undock/load/unload/swap, manifests, throughput
    src/simulation_history.cpp  [Follow-on 2] get_state_at + per-tick committed-state snapshots
                          (the base code calls into each only at hooks commented //follow-on 1 / 2)
    tests/cases/CATEGORY/ NAME.in (commands, '#' comments allowed) + NAME.out (expected responses)
                          f1_* / f2_* categories cover Follow-on 1 / 2; the rest cover the base problem
    tests/run_tests.sh    test runner (compares JSON semantically via compare.py)
    third_party/nlohmann  vendored single-header JSON library (MIT)
    docs/DESIGN.md        design doc