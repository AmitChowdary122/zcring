# Shared helpers for scripts/sweep.sh and scripts/fanout.sh. Not executable
# on its own; sourced.

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
