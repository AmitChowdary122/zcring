#!/usr/bin/env bash
# Camera -> {edge-count, jitter, checksum} fan-out demo.
#
# Reuses this repo's pinning and quieting conventions (scripts/lib.sh)
# rather than reinventing them: check_cstates warns if deep C-states are
# enabled (they cost tail latency, see RUNNING.md #4a), and pinning keeps
# the producer and consumers off the same SMT sibling wherever the CPU
# count allows it.
#
# Env overrides: TRANSPORT (zcring|unix, default zcring), DURATION (seconds,
# default 60 for a quick look -- pass DURATION=600 for the ten-minute
# soak-test claim in README/STATUS.md).
set -euo pipefail

cd "$(dirname "$0")/.."
BIN=build/pipeline
[ -x "$BIN" ] || { echo "build first: make demo"; exit 1; }
. scripts/lib.sh

TRANSPORT=${TRANSPORT:-zcring}
DURATION=${DURATION:-60}

# --- CPU assignment ---------------------------------------------------------
# Producer + 3 consumers is 4 roles. On this 2-physical-core/4-thread part
# that is every logical CPU, so unlike the latency sweeps (which can always
# find a spare non-sibling core) one consumer necessarily shares a physical
# core with someone. Assignment: producer on cpu2 alone were possible, but
# with exactly 4 logical CPUs for 4 roles, checksum draws cpu0 (producer's
# own SMT sibling) -- documented rather than hidden, and it matters little
# at a 33ms period (see STATUS.md Open problem #3 for the general limit).
if [ "$(nproc)" -ge 4 ]; then
    PIN="--cpu-prod=2 --cpu-cons=3,1,0"  # edge-count=3, jitter=1, checksum=0
else
    PIN=""
    echo "warning: fewer than 4 CPUs; running unpinned" >&2
fi
[ -n "$PIN" ] && echo "pinning: $PIN" >&2 || echo "pinning: none" >&2

check_cstates

echo "transport: $TRANSPORT   duration: ${DURATION}s" >&2
sleep 1

exec $BIN --transport="$TRANSPORT" --duration="$DURATION" $PIN
