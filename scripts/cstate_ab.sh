#!/usr/bin/env bash
# Controlled A/B: does the large-payload deep-C-state penalty depend on how
# the zcring consumer waits?  ->  results/cstate_waiter_ab.csv
#
# Background. STATUS.md Open problem #5 / reports.txt §22 established that
# disabling deep C-states costs zcring ~65% at 1 MiB, N=1 — reproducibly, and
# independently of thermal state (#4) and offered rate (#1). unix is not
# similarly penalised, which pointed at something specific to zcring's
# busy-spinning consumer rather than to a general cost of disabling idle
# states.
#
# Two hypotheses have been tested and both failed to explain it. reports.txt
# §24's --sleep diagnostic (nanosleep between polls) came out WORSE than spin,
# but recorded its own caveat: nanosleep is not a real block/wake cycle, and
# the cpuidle framework is equally disabled underneath it either way, so that
# test could not distinguish "busy-spin is the mechanism" from "nanosleep just
# adds its own overhead". It explicitly named a real futex block as the test
# that would settle it.
#
# This script is that test. The adaptive spin-then-futex waiter (--notify)
# genuinely blocks in the kernel and is genuinely woken by the producer, so
# the consumer's core enters and leaves idle the same way pipe/unix's blocked
# reader does. If the penalty is caused by busy-spin denying the core a real
# block/wake cycle, the disabled-vs-enabled gap must shrink under --notify.
#
# The 2x2 is waiter (spin, notify) x C-states (enabled, disabled), with unix
# as a control in each C-state condition, at the configuration reports.txt §22
# used: 1 MiB, N=1, gap=100us, cool package before every single invocation.
#
# Requires passwordless sudo for cpupower (see RUNNING.md §4a).
#
# Env overrides: SIZE, COUNT, GAP, REPS, WARMUP, OUT
set -euo pipefail

cd "$(dirname "$0")/.."
BENCH=build/bench
[ -x "$BENCH" ] || { echo "build first: make"; exit 1; }
. scripts/lib.sh

mkdir -p results
OUT=${OUT:-results/cstate_waiter_ab.csv}
SIZE=${SIZE:-1048576}
COUNT=${COUNT:-1250}
GAP=${GAP:-100}
REPS=${REPS:-3}
WARMUP=${WARMUP:-2000}

PROD=${PROD:-2}
CONS=${CONS:-3}

sudo -n cpupower idle-info >/dev/null 2>&1 || {
    echo "need passwordless sudo for cpupower; see RUNNING.md §4a" >&2; exit 1; }

# Both arms must run at a fixed frequency, or the comparison picks up governor
# behaviour instead of idle-state behaviour.
sudo -n cpupower frequency-set -g performance >/dev/null 2>&1 || true

set_cstates() {
    case "$1" in
        enabled)  sudo -n cpupower idle-set -E    >/dev/null 2>&1 ;;
        disabled) sudo -n cpupower idle-set -D 0  >/dev/null 2>&1 ;;
    esac
}

echo "waiter,cstates,transport,rep,size,mean_ns,p50_ns,p99_ns,p999_ns,p9999_ns,max_ns" > "$OUT"

run_one() {   # waiter cstates transport rep flag
    local waiter=$1 cs=$2 t=$3 rep=$4 flag=$5
    # Cool FIRST, then set the arm's condition: wait_for_cool() temporarily
    # re-enables C-states to let the package actually reach a low baseline
    # (see lib.sh), so setting the condition before it would be undone.
    wait_for_cool
    set_cstates "$cs"
    printf '%-6s cstates=%-8s %-6s rep=%s\n' "$waiter" "$cs" "$t" "$rep" >&2
    printf '%s,%s,' "$waiter" "$cs" >> "$OUT"
    $BENCH --transport="$t" --size="$SIZE" --count="$COUNT" --gap-us="$GAP" \
           --warmup="$WARMUP" --touch $flag --cpu-prod="$PROD" --cpu-cons="$CONS" --csv \
        | sed "s/^\([^,]*\),/\1,$rep,/" >> "$OUT"
}

for cs in enabled disabled; do
    for rep in $(seq 1 "$REPS"); do
        run_one spin   "$cs" zcring "$rep" ""
        run_one notify "$cs" zcring "$rep" "--notify"
        run_one spin   "$cs" unix   "$rep" ""      # control: no waiter to vary
    done
done

# Leave the machine in the state RUNNING.md says to leave it in.
set_cstates enabled

echo >&2
echo "wrote $OUT" >&2
column -t -s, "$OUT"
