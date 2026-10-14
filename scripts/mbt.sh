#!/usr/bin/env bash
# One iteration of the spec -> traces -> C++ replay loop.
#
# Environment:
#   N_TRACES   number of traces to generate        (default 50)
#   MAX_STEPS  steps per trace                     (default 60)
#   SEED       Quint seed, for reproducible traces (default: random)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SPEC="$ROOT/spec/elevator.qnt"
TRACES="$ROOT/traces"
BUILD="$ROOT/build"
LOG="$BUILD/mbt_last.log"

N_TRACES="${N_TRACES:-50}"
MAX_STEPS="${MAX_STEPS:-60}"
SEED_ARGS=()
if [[ -n "${SEED:-}" ]]; then SEED_ARGS=(--seed="$SEED"); fi

step() { printf '\n==> %s\n' "$*"; }

# Prints only the verdict line on success, the whole counterexample on a violation.
quint_run() {
  local out
  if ! out=$(quint run "$SPEC" "$@" ${SEED_ARGS[@]+"${SEED_ARGS[@]}"} 2>&1); then
    echo "$out"
    exit 1
  fi
  echo "$out" | grep -E '^\[(ok|violation)\]'
}

step "Quint: typecheck and tests"
quint typecheck "$SPEC"
quint test "$SPEC"

step "Quint: simulate 1000 executions, checking invariant 'safety'"
quint_run --invariant=safety --max-samples=1000 --max-steps="$MAX_STEPS"

step "Quint: generate $N_TRACES ITF traces, checking invariant 'safety' in every state"
rm -rf "$TRACES"
mkdir -p "$TRACES"
quint_run --mbt --invariant=safety \
  --max-samples="$N_TRACES" --n-traces="$N_TRACES" --max-steps="$MAX_STEPS" \
  --out-itf="$TRACES/trace_{seq}.itf.json"

step "C++: build"
cmake -S "$ROOT" -B "$BUILD" >/dev/null
cmake --build "$BUILD"

set +e

step "C++: unit tests"
"$BUILD/elevator_tests" --gtest_brief=1
unit_status=$?

step "C++: replay Quint traces (MBT driver)"
MBT_TRACES_DIR="$TRACES" "$BUILD/mbt_driver" --gtest_brief=1 2>&1 | tee "$LOG"
mbt_status=${PIPESTATUS[0]}

set -e

echo
[[ $mbt_status -eq 0 ]] && echo "MBT:  PASS" || echo "MBT:  FAIL (log: $LOG)"
[[ $unit_status -eq 0 ]] && echo "UNIT: PASS" || echo "UNIT: FAIL"
[[ $mbt_status -eq 0 && $unit_status -eq 0 ]]
