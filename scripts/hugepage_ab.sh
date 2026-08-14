#!/usr/bin/env bash
# Controlled A/B: does huge-page backing of the arena move the 1 MiB number?
#   -> results/hugepage_ab.csv
#
# Background. reports.txt §22 and §27 leave part of the large-payload cost
# unexplained: at 1 MiB, N=1, zcring loses to a UNIX socket under the
# C-states-disabled configuration this project requires, and neither thermal
# state, offered rate, nor waiter style (spin vs a real futex block) accounts
# for it.
#
# The hypothesis here is address translation. With 4 KiB pages a 1 MiB message
# spans 256 PTEs, walked once by the producer and again by the consumer in a
# different address space; the L1 dTLB on this part holds ~64 entries, so every
# message misses it comprehensively, twice. A 2 MiB page makes the same message
# one PTE per side. If translation is a material part of the gap, forcing
# hugetlbfs backing has to show up here.
#
# The 2x2 is backing (4k, huge) x transport (zcring, unix). unix is the control
# and is expected not to move at all: it copies through the kernel and its
# arena is not zcring's, so a change in its number would mean the two arms
# differ in something other than the ring's page size -- thermal drift, most
# likely -- and would invalidate the comparison.
#
# The `pages` column records what the ring ACTUALLY got, parsed from bench's
# stderr, not what the arm asked for. The huge arm additionally uses
# --pages=huge, which fails rather than falling back, so a row that silently
# measured 4 KiB pages twice cannot exist.
#
# Requires: passwordless sudo for cpupower (RUNNING.md §4a), and a hugetlbfs
# pool. The pool is NOT reserved by this script -- that needs a root sysctl
# this repo's sudo policy does not grant -- so it checks and tells you the one
# command to run. See RUNNING.md §7.
#
# Env overrides: SIZE, COUNT, GAP, REPS, WARMUP, OUT
set -euo pipefail

cd "$(dirname "$0")/.."
BENCH=build/bench
[ -x "$BENCH" ] || { echo "build first: make"; exit 1; }
. scripts/lib.sh

mkdir -p results
OUT=${OUT:-results/hugepage_ab.csv}
SIZE=${SIZE:-1048576}
COUNT=${COUNT:-1250}
GAP=${GAP:-100}
REPS=${REPS:-5}
WARMUP=${WARMUP:-2000}

PROD=${PROD:-2}
CONS=${CONS:-3}

# This script takes OUT rather than OUT_SUFFIX, so satisfy the machine guard
# by setting OUT_SUFFIX when OUT has already been pointed somewhere custom.
[ "$OUT" = "results/hugepage_ab.csv" ] || OUT_SUFFIX=${OUT_SUFFIX:-custom}
check_machine "$OUT"

sudo -n cpupower idle-info >/dev/null 2>&1 || {
    echo "need passwordless sudo for cpupower; see RUNNING.md §4a" >&2; exit 1; }

# The arena is sized by bench as ~64 MiB (it halves the slot count until
# slots*size <= 64 MiB), and the control block takes one more huge page, so
# 34 pages is the true requirement. Ask for headroom.
NEED_PAGES=${NEED_PAGES:-40}
have=$(awk '/^HugePages_Total:/ {print $2}' /proc/meminfo)
if [ "${have:-0}" -lt "$NEED_PAGES" ]; then
    cat >&2 <<EOF
hugetlbfs pool is $have pages, need >= $NEED_PAGES (2 MiB each = $((NEED_PAGES*2)) MiB).

Reserve it, then re-run this script:

    sudo sysctl -w vm.nr_hugepages=$NEED_PAGES

and afterwards give it back:

    sudo sysctl -w vm.nr_hugepages=0

Reserving is deliberately not automated: it takes memory away from the rest
of the system for as long as it is set, and this benchmark is not entitled to
do that silently.
EOF
    exit 1
fi

# Both arms at a fixed frequency and with deep C-states disabled: that is the
# configuration the small-message determinism claim requires (RUNNING.md §4a),
# and it is the one under which the large-payload loss was observed. Measuring
# the fix in a configuration the problem does not occur in would prove nothing.
sudo -n cpupower frequency-set -g performance >/dev/null 2>&1 || true

echo "backing,transport,rep,pages,size,mean_ns,p50_ns,p99_ns,p999_ns,p9999_ns,max_ns" > "$OUT"

ERRLOG=$(mktemp)
trap 'rm -f "$ERRLOG"' EXIT

run_one() {   # backing transport rep pagesflag
    local backing=$1 t=$2 rep=$3 flag=$4 row pages

    # Cool first, then disable C-states: wait_for_cool() re-enables them to
    # reach a low baseline (lib.sh), so the order matters.
    wait_for_cool
    sudo -n cpupower idle-set -D 0 >/dev/null 2>&1
    printf '%-5s %-6s rep=%s\n' "$backing" "$t" "$rep" >&2

    row=$($BENCH --transport="$t" --size="$SIZE" --count="$COUNT" --gap-us="$GAP" \
                 --warmup="$WARMUP" --touch $flag \
                 --cpu-prod="$PROD" --cpu-cons="$CONS" --csv 2>"$ERRLOG")
    # Ground truth for the row, not the arm label. pipe/unix have no arena.
    pages=$(sed -n 's/^ *pages=//p' "$ERRLOG" | head -1)
    [ -n "$pages" ] || pages="n/a"
    if [ "$backing" = "huge" ] && [ "$t" = "zcring" ] && [ "$pages" != "hugetlb" ]; then
        echo "ABORT: huge arm reported pages=$pages, not hugetlb" >&2
        exit 1
    fi
    printf '%s,%s,%s,%s,%s\n' "$backing" "$t" "$rep" "$pages" \
           "${row#*,}" >> "$OUT"
}

for rep in $(seq 1 "$REPS"); do
    run_one 4k   zcring "$rep" "--pages=4k"
    run_one huge zcring "$rep" "--pages=huge"
    run_one 4k   unix   "$rep" ""            # control: same machine state
done

# Leave the machine as RUNNING.md says to leave it.
sudo -n cpupower idle-set -E >/dev/null 2>&1

echo >&2
echo "wrote $OUT" >&2
column -t -s, "$OUT"

# Per-arm summary. The question is whether huge beats 4k by more than the
# spread within an arm; anything under a few percent is a negative result and
# reports.txt §29 says to stop there.
awk -F, 'NR>1 && $2=="zcring" {
    n[$1]++; s[$1]+=$7; if (mn[$1]=="" || $7<mn[$1]) mn[$1]=$7;
    if ($7>mx[$1]) mx[$1]=$7
}
END {
    printf "\nzcring p50 by arm (ns):\n"
    for (a in n) printf "  %-5s mean-of-reps %9.0f   min %9d   max %9d   n=%d\n",
                        a, s[a]/n[a], mn[a], mx[a], n[a]
    if (n["4k"] && n["huge"])
        printf "  huge/4k = %.3fx (%.1f%% change)\n",
               (s["4k"]/n["4k"]) / (s["huge"]/n["huge"]),
               100 * (1 - (s["huge"]/n["huge"]) / (s["4k"]/n["4k"]))
}' "$OUT" >&2
