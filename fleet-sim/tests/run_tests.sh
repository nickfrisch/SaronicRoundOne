#!/usr/bin/env bash
# Usage: tests/run_tests.sh [path/to/binary]
# Runs every tests/cases/CATEGORY/NAME.in through the binary and compares stdout
# to NAME.out as JSON (key order / whitespace ignored; see compare.py).
# Lines starting with '#' in .in files are comments and are stripped first.
set -u
DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="${1:-$DIR/../build/fleet_sim}"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
pass=0; fail=0

for cat_dir in "$DIR"/cases/*/; do
  category="$(basename "$cat_dir")"
  echo "== $category"
  for in in "$cat_dir"*.in; do
    name="$(basename "$in" .in)"
    actual="$TMP/$category.$name.out"
    if ! grep -v '^[[:space:]]*#' "$in" | "$BIN" > "$actual"; then
      echo "  FAIL $name (binary exited non-zero)"; fail=$((fail+1)); continue
    fi
    if python3 "$DIR/compare.py" "$cat_dir$name.out" "$actual"; then
      echo "  PASS $name"; pass=$((pass+1))
    else
      echo "  FAIL $name"; fail=$((fail+1))
    fi
  done
done

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
