#!/usr/bin/env bash
# Payload sweep -> results/sweep.csv (or $OUT_SUFFIX variant, see below)
#
# Runs in --touch mode: producer and consumer actually write and read every
# payload cache line. Without that, zcring moves only the 8-byte header and
# the resulting flat curve is an artifact of the harness, not a property of
# the design. See README.md.
#
# Offered rate is per-size, not a flat --gap-us: see scripts/rates.sh for why
# a single gap-us was producing inconsistent numbers across payload sizes.
# RATE_FRACTION selects a percentage of each size's measured saturation rate
# (25 = primary rate, a quarter of the saturating throughput -- 4x headroom
# above the confirmed knee; see README.md for the justification). GAP, if
# set, overrides the table entirely with a flat value for ad hoc single-rate
# exploration -- not used for committed data.
#
# WAITER selects how zcring consumers wait, and it is deliberately defaulted
# to `spin`: every dataset committed to results/ was measured that way, and
# changing the default would silently make the committed baseline
# irreproducible from this tree. `WAITER=notify` uses the adaptive
# spin-then-futex waiter instead, which is the like-for-like comparison
# against pipe/unix (both sides then block when idle) at the cost of a wake on
# most messages. The two are different questions, not better and worse
# versions of one: spin answers "lowest achievable latency given a dedicated
# core", notify answers "latency at an idle CPU cost comparable to a socket".
# Report both, never silently mix them in one table.
#
# Env overrides: COUNT, RATE_FRACTION, GAP, SIZES, REPS, WAITER, OUT_SUFFIX
set -euo pipefail

cd "$(dirname "$0")/.."
BENCH=build/bench
[ -x "$BENCH" ] || { echo "build first: make"; exit 1; }
. scripts/lib.sh
. scripts/rates.sh

mkdir -p results
OUT=results/sweep${OUT_SUFFIX:-}.csv
COUNT=${COUNT:-20000}
RATE_FRACTION=${RATE_FRACTION:-25}
REPS=${REPS:-1}
SIZES=${SIZES:-"64 256 1024 4096 16384 65536 262144 1048576"}
WAITER=${WAITER:-spin}

case "$WAITER" in
    spin)   WAIT_FLAG="" ;;
    notify) WAIT_FLAG="--notify" ;;
    *) echo "WAITER must be 'spin' or 'notify', got '$WAITER'" >&2; exit 1 ;;
esac
echo "zcring waiter: $WAITER" >&2

# --- pick two cores that are NOT SMT siblings -------------------------------
# Sibling threads share L1/L2, which flatters shared-memory IPC and makes the
# comparison dishonest. Cross-physical-core is the realistic case.
pick_cores() {
    local base=2 sibs cand
    [ -d /sys/devices/system/cpu/cpu$base ] || { echo ""; return; }
    sibs=$(cat /sys/devices/system/cpu/cpu$base/topology/thread_siblings_list 2>/dev/null || echo "$base")
    for cand in $(seq 3 $(($(nproc) - 1))); do
        case ",$sibs," in
            *",$cand,"*) continue ;;
        esac
        # also reject if cand's sibling list contains base
        local cs
        cs=$(cat /sys/devices/system/cpu/cpu$cand/topology/thread_siblings_list 2>/dev/null || echo "$cand")
        case ",$cs," in
            *",$base,"*) continue ;;
        esac
        echo "--cpu-prod=$base --cpu-cons=$cand"
        return
    done
    echo ""
}

PIN=""
if [ "$(nproc)" -ge 4 ]; then PIN=$(pick_cores); fi
[ -n "$PIN" ] && echo "pinning: $PIN" >&2 || echo "pinning: none" >&2

check_machine "$OUT"
check_cstates

echo "rate fraction: ${GAP:+GAP override=$GAP us}${GAP:-${RATE_FRACTION}% of saturation}" >&2

echo "transport,size,mean_ns,p50_ns,p99_ns,p999_ns,p9999_ns,max_ns" > "$OUT"
for size in $SIZES; do
  # Large payloads through a 64 KiB pipe need many blocking round-trips per
  # message; scale the sample count down so the sweep finishes this decade.
  n=$COUNT
  [ "$size" -ge 262144 ] && n=$((COUNT / 10))
  [ "$size" -ge 1048576 ] && n=$((COUNT / 40))
  [ "$n" -lt 500 ] && n=500

  gap=${GAP:-$(gap_for_fraction "$size" "$RATE_FRACTION")}

  for rep in $(seq 1 "$REPS"); do
    for t in zcring pipe unix; do
      wait_for_cool
      printf 'running %-7s size=%-8s n=%-7s gap=%-6s rep=%s\n' "$t" "$size" "$n" "$gap" "$rep" >&2
      # --pages=4k pins the arena's page size, so this dataset does not
      # silently change meaning depending on whether vm.nr_hugepages happens
      # to be reserved on the machine at the time (see RUNNING.md §5a).
      # Huge pages are measured in scripts/hugepage_ab.sh, not here.
      $BENCH --transport="$t" --size="$size" --count="$n" \
             --gap-us="$gap" --touch --pages=4k $WAIT_FLAG $PIN --csv >> "$OUT"
    done
  done
done

echo >&2
echo "wrote $OUT" >&2
column -t -s, "$OUT"
