#!/usr/bin/env bash
# Payload sweep -> results/sweep.csv
#
# Runs in --touch mode: producer and consumer actually write and read every
# payload cache line. Without that, zcring moves only the 8-byte header and
# the resulting flat curve is an artifact of the harness, not a property of
# the design. See README.md.
#
# Env overrides: COUNT, GAP, SIZES, REPS
set -euo pipefail

cd "$(dirname "$0")/.."
BENCH=build/bench
[ -x "$BENCH" ] || { echo "build first: make"; exit 1; }

mkdir -p results
OUT=results/sweep.csv
COUNT=${COUNT:-20000}
GAP=${GAP:-50}
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

echo "transport,size,mean_ns,p50_ns,p99_ns,p999_ns,p9999_ns,max_ns" > "$OUT"
for size in $SIZES; do
  # Large payloads through a 64 KiB pipe need many blocking round-trips per
  # message; scale the sample count down so the sweep finishes this decade.
  n=$COUNT
  [ "$size" -ge 262144 ] && n=$((COUNT / 10))
  [ "$size" -ge 1048576 ] && n=$((COUNT / 40))
  [ "$n" -lt 500 ] && n=500

  for rep in $(seq 1 "$REPS"); do
    for t in zcring pipe unix; do
      printf 'running %-7s size=%-8s n=%-7s rep=%s\n' "$t" "$size" "$n" "$rep" >&2
      $BENCH --transport="$t" --size="$size" --count="$n" \
             --gap-us="$GAP" --touch $PIN --csv >> "$OUT"
    done
  done
done

echo >&2
echo "wrote $OUT" >&2
column -t -s, "$OUT"
