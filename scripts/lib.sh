# Shared helpers for scripts/sweep.sh and scripts/fanout.sh. Not executable
# on its own; sourced.

# ---------------------------------------------------------------------------
# Machine provenance guard.
#
# Every dataset committed to results/ was measured on an Intel i3-1115G4
# (2 physical cores + SMT), bare metal. THAT LAPTOP FAILED on 2026-08-14; its
# SSD now runs as a dual boot in an AMD Ryzen 9 270 (8C/16T). The committed
# numbers are therefore valid as measured but NOT reproducible on the hardware
# that now exists -- see results/PROVENANCE.md.
#
# Without a guard the obvious accident is a plain `make sweep` on the new box
# silently overwriting results/sweep.csv, leaving one file holding two
# machines' numbers with nothing to distinguish them. We have already
# destroyed a committed dataset once this way (results/fanout.csv, recovered
# from git), and that time the two datasets at least differed by a flag name
# in the log. Two different CPUs in one CSV would be invisible.
#
# So: if the CPU is not the machine the canonical datasets came from, refuse
# to write a canonical filename. OUT_SUFFIX is still allowed, because
# measuring the new machine deliberately, into its own file, is fine and is
# how a cross-platform comparison would be built.
ZC_DATASET_CPU=${ZC_DATASET_CPU:-"Intel(R) Core(TM) i3-1115G4"}

current_cpu() {
    grep -m1 '^model name' /proc/cpuinfo 2>/dev/null | sed 's/^model name[[:space:]]*:[[:space:]]*//'
}

# Usage: check_machine "$OUT"
check_machine() {
    local out="$1" cpu
    cpu=$(current_cpu)
    [ -n "$cpu" ] || return 0

    case "$cpu" in
        *"$ZC_DATASET_CPU"*) return 0 ;;
    esac

    echo "" >&2
    echo "=========================================================" >&2
    echo " MACHINE MISMATCH" >&2
    echo "   committed datasets: $ZC_DATASET_CPU" >&2
    echo "   this machine:       $cpu" >&2
    echo "=========================================================" >&2

    if [ -z "${OUT_SUFFIX:-}" ]; then
        echo "refusing to write '$out' on a different CPU than the committed" >&2
        echo "data came from -- it would mix two machines into one file." >&2
        echo "" >&2
        echo "  measure this machine into its own dataset:" >&2
        echo "    OUT_SUFFIX=_ryzen  <the same command you just ran>" >&2
        echo "" >&2
        echo "  or override deliberately (you almost never want this):" >&2
        echo "    ZC_DATASET_CPU='$cpu'  <the same command you just ran>" >&2
        echo "" >&2
        echo "See results/PROVENANCE.md." >&2
        exit 1
    fi

    echo "OUT_SUFFIX is set, writing '$out' -- record the CPU in" >&2
    echo "results/PROVENANCE.md or this file becomes unattributable." >&2
    echo "" >&2
}
# ---------------------------------------------------------------------------

# Warn (do not fail) if any cpuidle state deeper than C1 is enabled. A
# governor that predicts a long idle gap between messages can pick a deep
# C-state; waking from it costs its exit latency, which lands directly in
# p99.9+. Measured on the dev machine: C3 exit latency 1048us produced a
# p99.9 that ranged 787ns-1.77ms across otherwise-identical reps, collapsing
# to a consistent ~2us once disabled with `cpupower idle-set -D 0`. See
# RUNNING.md #4a.
check_cstates() {
    local base=/sys/devices/system/cpu/cpu0/cpuidle
    [ -d "$base" ] || return 0
    local deep=""
    for s in "$base"/state*/; do
        local lat name disabled
        lat=$(cat "$s/latency" 2>/dev/null || echo 0)
        name=$(cat "$s/name" 2>/dev/null || echo "?")
        disabled=$(cat "$s/disable" 2>/dev/null || echo 1)
        [ "$lat" -gt 1 ] && [ "$disabled" = "0" ] && deep="${deep:+$deep, }$name(${lat}us)"
    done
    if [ -n "$deep" ]; then
        echo "warning: deep C-states enabled: $deep" >&2
        echo "         tail latency will be polluted by idle-state exit cost." >&2
        echo "         fix: sudo cpupower idle-set -D 0   (see RUNNING.md #4a)" >&2
    fi
}

# Wait for package temperature to drop below a safe threshold before
# starting the next payload size.
#
# Why this exists: this part has only 2 physical cores, and each pinned
# benchmark CPU (2, 3) is the SMT sibling of an unused logical CPU (0, 1
# respectively) -- P-state is shared per physical core, not per logical
# CPU, so disabling C-states on the pinned CPUs (required by check_cstates
# above, for the small-payload tail-latency fix) keeps BOTH physical cores
# continuously active for as long as the session runs, even between
# benchmark invocations. On this low-TDP embedded-class chip that heat-soaks
# the package into thermal throttling within tens of minutes, and it hits
# large payloads hardest because they take longer to run and zcring's
# busy-spin consumer generates its own heat while waiting (unix/pipe block
# instead). Confirmed 2026-08-01: package cool (54C) vs throttling (97-100C)
# gave zcring p50 at 1 MiB of 123us vs 210-217us for the IDENTICAL config --
# a bigger, more consistent effect than any offered-rate change tested, and
# it explains why sweep.sh's default ascending size order (small to large)
# was quietly degrading exactly the sizes that matter most for the
# large-payload story. See reports.txt for the investigation and
# README.md's determinism section for the numbers.
THERMAL_LIMIT_C=${THERMAL_LIMIT_C:-60}
THERMAL_MAX_WAIT_S=${THERMAL_MAX_WAIT_S:-300}

pkg_temp_zone() {
    local z t
    for z in /sys/class/thermal/thermal_zone*/; do
        t=$(cat "$z/type" 2>/dev/null)
        [ "$t" = "x86_pkg_temp" ] && { echo "$z"; return 0; }
    done
    for z in /sys/class/thermal/thermal_zone*/; do
        t=$(cat "$z/type" 2>/dev/null)
        [ "$t" = "TCPU" ] && { echo "$z"; return 0; }
    done
    return 1
}

wait_for_cool() {
    local zone millideg tempc waited=0 reenabled=0
    zone=$(pkg_temp_zone) || {
        echo "warning: no x86_pkg_temp/TCPU thermal zone found; skipping thermal gate" >&2
        return 0
    }
    millideg=$(cat "${zone}temp" 2>/dev/null) || return 0
    tempc=$((millideg / 1000))
    [ "$tempc" -lt "$THERMAL_LIMIT_C" ] && return 0

    echo "thermal gate: package at ${tempc}C, waiting for <${THERMAL_LIMIT_C}C..." >&2

    # Measured directly (2026-08-01): with C-states disabled on the pinned
    # CPUs, the package plateaus in the mid-70s to low-80s C and does NOT
    # converge further -- it oscillated 74-81C over 60s of idle wait with no
    # bench running. That's because disabling C-states removes the ONLY way
    # these cores can actually go low-power; there's nothing left to cool
    # into. So the wait has to temporarily UNDO the C-state disable to let
    # the package actually cool, then redo it before handing control back to
    # the caller -- otherwise this loop just burns THERMAL_MAX_WAIT_S and
    # gives up every single time, silently disabling the gate.
    if sudo -n cpupower idle-set -E >/dev/null 2>&1; then
        reenabled=1
    else
        echo "warning: no passwordless sudo for cpupower; cannot re-enable C-states to cool," >&2
        echo "         waiting at whatever floor is reachable with them disabled instead" >&2
    fi

    while [ "$tempc" -ge "$THERMAL_LIMIT_C" ]; do
        if [ "$waited" -ge "$THERMAL_MAX_WAIT_S" ]; then
            echo "warning: still ${tempc}C after ${waited}s, giving up and continuing anyway" >&2
            echo "         (results for this size may be thermally degraded)" >&2
            break
        fi
        sleep 5
        waited=$((waited + 5))
        millideg=$(cat "${zone}temp" 2>/dev/null) || break
        tempc=$((millideg / 1000))
    done

    if [ "$reenabled" -eq 1 ]; then
        sudo -n cpupower idle-set -D 0 >/dev/null 2>&1
    fi
    [ "$waited" -gt 0 ] && echo "thermal gate: at ${tempc}C after ${waited}s, C-states redisabled, continuing" >&2
    return 0
}
