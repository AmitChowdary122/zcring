#!/usr/bin/env bash
# Fan-out sweep: payload x consumer count -> results/fanout.csv
#
# The question this answers is the scalability one: what happens to one-way
# latency as one producer feeds N consumers. zcring publishes once and all N
# read the same bytes (zero copies, O(1) in N); pipe/unix need N separate
# connections and the producer writes the payload N times (N copies, O(N)).
# The gap should therefore widen with N.
#
# Runs in --touch mode for the same reason sweep.sh does: without it zcring
# moves only the 8-byte header and the result is an artifact. See README.md.
#
# Runs in --yield mode, which sweep.sh does not. Rationale: zcring's waiters
# busy-wait while pipe/unix block in read(). At N=1 that costs little, but
# this is a dual-core machine, and at N=4 there are more spinners than
# hardware threads — pure spin then measures scheduler thrash rather than
# fan-out, inflating zcring p50 by ~40x. --yield gives the waiters a bounded
# spin and then hands the CPU back, which is both closer to what the
# comparators already do and closer to what Layer 2 notification will do.
# The flag is a no-op for pipe/unix. Measured cost of --yield at N=1 is ~8%
# on zcring, i.e. it is the conservative choice, not a flattering one.
#
# Offered rate is per-size, not a flat --gap-us -- see scripts/rates.sh.
# RATE_FRACTION (default 25, a quarter of saturating throughput) is a
# percentage of each size's measured saturation rate; the SAME table is
# used here as in sweep.sh, deliberately,
# so the baseline and fan-out results are one coherent dataset rather than
# two sweeps calibrated differently. The table itself was derived from N=1,
# not N=4 -- see scripts/rates.sh for why N=4 cannot supply a meaningful
# saturation point on this hardware (CPU contention alone dominates it).
# GAP, if set, overrides the table with a flat value for ad hoc exploration.
#
# Env overrides: COUNT, RATE_FRACTION, GAP, SIZES, REPS, NCONS, OUT_SUFFIX
set -euo pipefail

cd "$(dirname "$0")/.."
BENCH=build/bench
[ -x "$BENCH" ] || { echo "build first: make"; exit 1; }
. scripts/lib.sh
. scripts/rates.sh

mkdir -p results
OUT=results/fanout${OUT_SUFFIX:-}.csv
COUNT=${COUNT:-50000}
RATE_FRACTION=${RATE_FRACTION:-25}
REPS=${REPS:-5}
SIZES=${SIZES:-"64 256 1024 4096 16384 65536 262144 1048576"}
NCONS=${NCONS:-"1 2 4"}

# --- topology ---------------------------------------------------------------
# The producer gets one physical core to itself and every consumer is kept off
# that core's SMT siblings. This is not fussiness: putting a spinning consumer
# on the producer's sibling thread leaves p50 unchanged but inflated p99 by
# ~185x in testing, because the two then share one core's execution
# resources. On a 2-physical-core box that means one hyperthread is
# deliberately left idle. Stated because it affects how the numbers should be
# read, not hidden.
PROD=${PROD:-2}
SIBS=$(cat /sys/devices/system/cpu/cpu$PROD/topology/thread_siblings_list 2>/dev/null || echo "$PROD")
CONS=""
for c in $(seq 0 $(($(nproc) - 1))); do
    case ",$SIBS," in *",$c,"*) continue ;; esac
    CONS="${CONS:+$CONS,}$c"
done
[ -n "$CONS" ] || { echo "no consumer CPUs outside the producer's core"; exit 1; }

echo "producer cpu:      $PROD (siblings: $SIBS)" >&2
echo "consumer cpu list: $CONS" >&2
echo "consumers per run: $NCONS  (wrapping over the list above)" >&2
echo >&2

check_cstates

echo "rate fraction: ${GAP:+GAP override=$GAP us}${GAP:-${RATE_FRACTION}% of saturation}" >&2

echo "transport,consumers,size,mean_ns,p50_ns,p99_ns,p999_ns,p9999_ns,max_ns" > "$OUT"
for n in $NCONS; do
  for size in $SIZES; do
    # Large payloads across N connections need proportionally more work per
    # message; scale the sample count down so the sweep terminates.
    c=$COUNT
    [ "$size" -ge 262144 ] && c=$((COUNT / 10))
    [ "$size" -ge 1048576 ] && c=$((COUNT / 40))
    c=$((c / n))
    [ "$c" -lt 500 ] && c=500

    gap=${GAP:-$(gap_for_fraction "$size" "$RATE_FRACTION")}

    for rep in $(seq 1 "$REPS"); do
      for t in zcring pipe unix; do
        printf 'N=%s %-7s size=%-8s n=%-7s gap=%-6s rep=%s\n' "$n" "$t" "$size" "$c" "$gap" "$rep" >&2
        $BENCH --transport="$t" --consumers="$n" --size="$size" --count="$c" \
               --gap-us="$gap" --touch --yield \
               --cpu-prod="$PROD" --cpu-cons="$CONS" --csv >> "$OUT"
      done
    done
  done
done

echo >&2
echo "wrote $OUT" >&2
