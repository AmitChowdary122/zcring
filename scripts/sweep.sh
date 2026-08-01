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
# Env overrides: COUNT, RATE_FRACTION, GAP, SIZES, REPS, OUT_SUFFIX
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
      printf 'running %-7s size=%-8s n=%-7s gap=%-6s rep=%s\n' "$t" "$size" "$n" "$gap" "$rep" >&2
      $BENCH --transport="$t" --size="$size" --count="$n" \
             --gap-us="$gap" --touch $PIN --csv >> "$OUT"
    done
  done
done

echo >&2
echo "wrote $OUT" >&2
column -t -s, "$OUT"
