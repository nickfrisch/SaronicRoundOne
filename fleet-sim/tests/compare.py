#!/usr/bin/env python3
"""compare.py EXPECTED ACTUAL -- compare two NDJSON files semantically.

- Key order and whitespace are ignored (lines are compared as parsed JSON).
- Blank lines and lines starting with '#' are ignored, so expected files can be annotated.
- An expected error message ending in "..." matches any actual message with that prefix
  (used for parse errors, whose exact text comes from the JSON library).
"""
import json, sys

MAX_SHOWN = 5      # mismatches to print before giving up
MAX_CHARS = 300    # truncate huge lines (large tests) in the diff output


def load(path):
    with open(path) as f:
        return [json.loads(l) for l in f if l.strip() and not l.lstrip().startswith("#")]


def matches(e, a):
    if e == a:
        return True
    try:
        msg = e["payload"]["message"]
        return (e["status"] == a["status"] == "error" and msg.endswith("...")
                and a["payload"]["message"].startswith(msg[:-3]))
    except (TypeError, KeyError):
        return False


def short(x):
    s = json.dumps(x) if x != "<missing>" else x
    return s if len(s) <= MAX_CHARS else s[:MAX_CHARS] + f"... ({len(s)} chars)"


exp, act = load(sys.argv[1]), load(sys.argv[2])
bad = 0
for i in range(max(len(exp), len(act))):
    e = exp[i] if i < len(exp) else "<missing>"
    a = act[i] if i < len(act) else "<missing>"
    if not matches(e, a):
        bad += 1
        if bad <= MAX_SHOWN:
            print(f"    response {i+1}:\n      expected: {short(e)}\n      actual:   {short(a)}")
if bad > MAX_SHOWN:
    print(f"    ... and {bad - MAX_SHOWN} more mismatched responses")
sys.exit(0 if bad == 0 else 1)
