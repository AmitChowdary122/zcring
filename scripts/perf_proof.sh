#!/usr/bin/env bash
# Discharges the proof obligation at PLAN.md line 117: "show via perf stat
# that memcpy does not appear in the profile." (The other half of that
# obligation, latency flat wrt message size, was retracted -- it turned out
# to be a --touch harness artifact -- and is not addressed here.)
#
# Symbolised perf profile of ./build/bench at 64 KiB, --touch, producer and
# consumer pinned to distinct physical cores, for --transport=zcring and
# --transport=unix. 64 KiB because a copy would dominate a profile at that
# size if one existed -- small payloads wouldn't show it even if present.
#
# The contrast is the proof: unix should show copy_user_*/__memmove
# prominently (it is a real one-copy transport, used here as a control that
# the method actually detects a copy when one exists); zcring should show
# neither in its transport path. Both profiles are written to $OUT, sorted
# by symbol, whatever they actually say -- including a copy symbol turning
# up in zcring's path, which would contradict the project's central claim.
#
# Env overrides: SIZE, COUNT, WARMUP, PROD, CONS, TOPN, FREQ, OUT
set -euo pipefail

cd "$(dirname "$0")/.."
BENCH=build/bench
[ -x "$BENCH" ] || { echo "build first: make" >&2; exit 1; }
. scripts/lib.sh

mkdir -p results
OUT=${OUT:-results/perf_proof.txt}

SIZE=${SIZE:-65536}     # 64 KiB
COUNT=${COUNT:-20000}
WARMUP=${WARMUP:-2000}
PROD=${PROD:-2}
CONS=${CONS:-3}
TOPN=${TOPN:-20}
FREQ=${FREQ:-3000}

# ---------------------------------------------------------------------------
# perf availability. A `perf` binary can exist and still not work for the
# running kernel if only an older kernel's linux-tools package is installed
# (the wrapper execs a version-specific binary that isn't there).
# ---------------------------------------------------------------------------
if ! command -v perf >/dev/null 2>&1; then
    echo "perf not found. Install:" >&2
    echo "  sudo apt install linux-tools-common linux-tools-generic linux-tools-\$(uname -r)" >&2
    exit 1
fi
if ! perf version >/dev/null 2>&1; then
    echo "perf found but not runnable for kernel $(uname -r)." >&2
    echo "  sudo apt install linux-tools-$(uname -r)" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# perf_event_paranoid / kptr_restrict guard.
#
# A blocked kernel makes `perf record` either fail outright or silently
# capture zero/unsymbolised samples -- which reads exactly like a clean
# "no copy found" profile to anyone who doesn't check sample counts. Fail
# loudly and name the exact fix instead of writing that out.
#
# Per this kernel's own perf_event_paranoid doc text: -1 allows everything;
# >=1 additionally disallows CPU event access (what `perf record` needs at
# all); >=2 additionally disallows kernel profiling (needed to see the
# kernel-side copy in unix's read()/write() path, which is the control arm
# of this comparison). So the requirement is <=0.
# ---------------------------------------------------------------------------
PARANOID=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo "")
KPTR=$(cat /proc/sys/kernel/kptr_restrict 2>/dev/null || echo "0")

if [ -z "$PARANOID" ]; then
    echo "warning: /proc/sys/kernel/perf_event_paranoid not readable; proceeding blind" >&2
elif [ "$PARANOID" -gt 0 ]; then
    cat >&2 <<EOF
perf_event_paranoid=$PARANOID blocks CPU event collection for this run
(needs <=0; this kernel's own doc: ">=1: Disallow CPU event access").
Not proceeding -- a blocked run would either fail outright or produce a
profile with no real samples, which is indistinguishable from a clean
"no copy" result unless someone checks for this. Fix:
  sudo sysctl -w kernel.perf_event_paranoid=0
This is runtime-only and resets on reboot unless also written to
/etc/sysctl.conf. If 0 still isn't enough on this kernel, use -1 (allows
all events, the maximally permissive setting):
  sudo sysctl -w kernel.perf_event_paranoid=-1
EOF
    exit 1
fi

if [ "$KPTR" != "0" ]; then
    cat >&2 <<EOF
kernel.kptr_restrict=$KPTR hides kernel symbol addresses from perf as
non-root. That would make unix's kernel-side copy -- the control arm this
comparison depends on to show the method actually detects a copy when one
exists -- show up as unresolved hex addresses in [kernel.kallsyms] instead
of copy_user_generic/__memmove/etc. Fix:
  sudo sysctl -w kernel.kptr_restrict=0
Not proceeding with a profile that can't symbolise the control arm.
EOF
    exit 1
fi

check_cstates || true

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

record_and_report() {   # transport -> writes symbol table to stdout, samples to stderr
    local t="$1" data="$TMPDIR/$1.perf.data" cmd_status
    printf 'recording %s...\n' "$t" >&2
    if ! perf record -F "$FREQ" -o "$data" --quiet -- \
            "$BENCH" --transport="$t" --size="$SIZE" --count="$COUNT" \
                      --warmup="$WARMUP" --touch \
                      --cpu-prod="$PROD" --cpu-cons="$CONS" --csv \
            >"$TMPDIR/$1.bench.out" 2>"$TMPDIR/$1.bench.err"; then
        cmd_status=$?
        echo "perf record failed for $t (exit $cmd_status); bench stderr:" >&2
        cat "$TMPDIR/$1.bench.err" >&2
        return 1
    fi
    echo "bench csv: $(cat "$TMPDIR/$1.bench.out")"
    echo

    local full="$TMPDIR/$1.report.txt"
    # dso,symbol -- not plain "symbol". Plain --sort symbol groups only by
    # symbol name/address, so unresolved samples from DIFFERENT binaries
    # that happen to unresolved-print as the same bare hex offset (e.g.
    # [vdso]'s internal clock_gettime branches vs some other mapping)
    # collapse into one indistinguishable row with no origin shown. That's
    # not a hypothetical: it happened on the first pass of this script and
    # made the harness's own clock_gettime() timing calls look like a
    # single unexplained ~50% blob. dso,symbol keeps the DSO column so
    # every row is attributable.
    perf report -i "$data" --stdio -n --sort dso,symbol >"$full" 2>/dev/null

    sed -n '/^# Samples:/p;/^# Event count/p' "$full"
    echo
    echo "top $TOPN symbols by overhead:"
    # NOT "... | head -n N": with pipefail, head closing the pipe early
    # SIGPIPEs the upstream grep and the pipeline's exit status becomes
    # 141, which looks exactly like a real failure to the `|| echo FAILED`
    # caller above -- happened nondeterministically on the first pass (hit
    # for unix, not for zcring, same script, same run). awk reads the file
    # directly, no pipe, no race.
    awk -v n="$TOPN" '!/^#/ && NF>0 { print; c++; if (c>=n) exit }' "$full"

    echo
    echo "full-report sweep for copy-shaped symbols (not just the top $TOPN --"
    echo "this is the actual check, the top-N table above is for human eyeballing):"
    if grep -iE 'memcpy|memmove|copy_user|copy_to_iter|copy_from_iter|copy_page' "$full"; then
        echo "^^ COPY SYMBOL FOUND -- see above"
        echo "yes" > "$TMPDIR/$1.copyfound"
    else
        echo "none found in $(grep -vc '^#\|^[[:space:]]*$' "$full") total symbol rows"
        echo "no" > "$TMPDIR/$1.copyfound"
    fi
}

check_machine "$OUT"

{
    echo "perf profile: zero-copy proof obligation (PLAN.md line 117)"
    echo "generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "commit: $(git rev-parse HEAD 2>/dev/null || echo unknown)"
    echo "cpu: $(current_cpu)"
    echo "kernel: $(uname -r)"
    echo "size=${SIZE} count=${COUNT} warmup=${WARMUP} cpu-prod=${PROD} cpu-cons=${CONS} freq=${FREQ}Hz"
    echo
    echo "NOTE on --touch: --touch makes the benchmark harness itself write"
    echo "and read every byte of the payload, on both the producer and"
    echo "consumer side, by design (it's what makes the throughput/latency"
    echo "numbers comparable across transports that otherwise wouldn't touch"
    echo "the data at all). That means both profiles below will legitimately"
    echo "show time in the harness's own fill/verify loop for every"
    echo "transport, including zcring. That is expected and is NOT the"
    echo "thing being tested. The question this profile answers is narrower:"
    echo "does the TRANSPORT (zc_reserve/zc_commit/zc_acquire/zc_release for"
    echo "zcring; write(2)/read(2) for unix) additionally copy the payload on"
    echo "top of the harness's own touch -- i.e. does a copy symbol"
    echo "(copy_user_generic, __memmove_*, memcpy, etc) appear attributable"
    echo "to the transport path, not to the harness's touch loop."
    echo
    echo "Two things to know before reading the tables below as \"missing =="
    echo "innocent\":"
    echo "  1. zc_reserve/zc_commit/zc_acquire/zc_release are 'static inline'"
    echo "     in src/zcring.h, so at -O2 they are inlined into main() rather"
    echo "     than appearing as their own symbols. Their cost (what there is"
    echo "     of it) is folded into the 'main' row, not absent from the"
    echo "     profile. Confirmed by grep: 'static inline' on all four."
    echo "  2. clock_gettime()/[vdso] entries are the harness's own latency"
    echo "     timestamping (one call per message on each side), not the"
    echo "     transport. They show up in both profiles at similar weight and"
    echo "     are not part of what this proof is checking."
    echo
    echo "======================================================================"
    echo "TRANSPORT: unix (control -- known one-copy transport, sanity check"
    echo "that this method actually detects a copy when one exists)"
    echo "======================================================================"
    record_and_report unix || echo "!! unix profiling FAILED, see stderr"
    echo
    echo "======================================================================"
    echo "TRANSPORT: zcring"
    echo "======================================================================"
    record_and_report zcring || echo "!! zcring profiling FAILED, see stderr"

    echo
    echo "======================================================================"
    echo "VERDICT (computed from the full-report sweeps above, not asserted)"
    echo "======================================================================"
    unix_copy=$(cat "$TMPDIR/unix.copyfound" 2>/dev/null || echo "unknown (profiling failed)")
    zcring_copy=$(cat "$TMPDIR/zcring.copyfound" 2>/dev/null || echo "unknown (profiling failed)")
    echo "unix control  -- copy symbol present: $unix_copy"
    echo "zcring        -- copy symbol present: $zcring_copy"
    if [ "$unix_copy" != "yes" ]; then
        echo "CAVEAT: the control did not show a copy symbol either. That means"
        echo "this method may not be sensitive enough to detect a copy at all"
        echo "under this configuration, which undermines the zcring result below"
        echo "-- a negative result only means something if the method is shown"
        echo "to be able to produce a positive one."
    fi
    if [ "$zcring_copy" = "yes" ]; then
        echo "CONTRADICTS THE ZERO-COPY CLAIM: see the zcring section above for"
        echo "the exact symbol and its DSO."
    elif [ "$unix_copy" = "yes" ] && [ "$zcring_copy" = "no" ]; then
        echo "Consistent with the zero-copy claim: the method demonstrably"
        echo "detects a real copy (unix) and does not find one on zcring's"
        echo "transport path at this payload size."
    fi
} | tee "$OUT" >&2

echo >&2
echo "wrote $OUT" >&2
