# Per-size offered-rate schedule. Sourced by sweep.sh and fanout.sh.
#
# Why this exists: a flat --gap-us applied across the whole payload range is
# not one methodology, it is a different offered load at every point on the
# curve -- trivial for a 64 B message, saturating for a 1 MiB one. That is
# what produced the original inconsistency (1 MiB, N=1: 1.47x, 1.38x, 2.32x
# across three prior runs that differed only in gap-us, count scaling, and
# unicast-vs-broadcast code path). This table fixes that by giving every row
# in both sweeps a stated, justified offered rate instead of an incidental one.
#
# ---------------------------------------------------------------------------
# How GAP_SAT_US was actually derived (two abandoned attempts first)
# ---------------------------------------------------------------------------
#
# Attempt 1: bisect the knee at N=4 fan-out. Discarded -- with 4 consumers
# sharing this machine's 2 physical cores, N=4 has no offered rate at which
# it is genuinely "unloaded": CPU scheduling contention alone produced an
# "unloaded" p50 of 8.5ms for pipe at 1 MiB. Bisecting against that measures
# scheduler noise, not bandwidth.
#
# Attempt 2: bisect the knee at N=1 (uncontended), fine-grained, small
# per-probe sample counts. Discarded -- this machine is a live interactive
# session (this assistant's own process, a terminal, gnome-shell all
# present), and small-sample automated probing proved too fragile against
# that background noise: repeated runs of the identical bisection produced
# different knees, and a from-scratch minimal repro at one point showed
# UNIX outperforming zcring at 1 MiB, which contradicts every other
# measurement in this repo and was traced to session-level interference
# (confirmed by killing the browser mid-investigation: pipe p50 at 64B
# dropped from 59-72us to the expected 2.6-2.7us the moment it was closed).
#
# What was actually used: a coarse candidate-gap sweep (few candidates,
# REPS=3, count=3000, means not single samples) at N=1 found NO detectable
# knee anywhere in the tested range (100us-25000us) for any size up to and
# including 1 MiB -- p50 stayed flat within ~10-15% run noise throughout.
# That means gap=100us, the pre-existing default, was never actually close
# to saturating the N=1 path; the original inconsistency was dominated by
# session noise and a small unicast-vs-broadcast path difference, not by an
# offered-rate problem at N=1.
#
# The one genuine, large, reproducible saturation effect in this codebase is
# at N=4 fan-out, 1 MiB: gap=100us produces sustained queueing collapse
# (p50 in the milliseconds), while gap>=500us stabilises p50 to a flat
# ~230-280us. Confirmed twice, independently, hours apart. This is a real
# effect with a known mechanism (reports.txt #12): 4 consumers each reading
# a 1 MiB payload is ~4x the memory traffic of N=1, and zcring's 64 MiB ring
# absorbs the resulting overload as queueing delay rather than throttling
# the producer, unlike a small-buffer socket.
#
# GAP_SAT_US[size] below is that ONE calibrated anchor (500us at 1 MiB)
# scaled linearly with payload size -- physically justified, since total
# memory traffic at a fixed message rate scales linearly with message size,
# so the gap needed to hold traffic under a fixed bandwidth budget scales
# linearly too. It is not a re-derived bisection per size; re-bisecting
# every size on this noisy machine is what kept failing. Values below
# 100us are clamped to 100us by gap_for_fraction()'s floor, since 100us is
# the one rate this repo has extensive clean evidence is safe (the entire
# committed REPS=5 baseline and N=1/N=2 fan-out data was measured there
# without incident).
#
# The same per-size table is used for the baseline sweep (N=1 unicast) and
# every N in the fan-out sweep (N=1, 2, 4) -- deliberately keeping the
# offered-rate calibration orthogonal to the N-scaling question. N=4's own
# CPU-contention floor at small payloads is a real, separate finding
# (reports.txt #14); rate tuning is not meant to paper over it, and isn't
# large enough at small payloads to need it (fan-out latencies there are a
# few us at N=4, not a runaway collapse).
#
# Measured on the dev machine (Intel i3-1115G4), governor=performance,
# C-states disabled (cpupower idle-set -D 0), desktop apps closed,
# 2026-08-01. Re-anchor by re-running the 1 MiB / N=4 gap sweep in
# reports.txt and rescaling if the hardware changes.

declare -A GAP_SAT_US=(
    [64]=1
    [256]=1
    [1024]=1
    [4096]=2
    [16384]=8
    [65536]=31
    [262144]=125
    [1048576]=500
)

# gap_for_fraction SIZE FRACTION_PERCENT -> gap-us
#
# rate_chosen = fraction * rate_saturation  =>  gap_chosen = gap_sat / fraction
# e.g. fraction=25 (the primary rate) is 4x the saturation gap, i.e. runs at
# a quarter of the saturating message rate -- the headroom directly
# confirmed safe for the one anchor point this table is calibrated from
# (1 MiB / N=4: knee at ~500us, confirmed flat and stable at 2000us).
#
# Floored at 100us regardless of the arithmetic result: below that, this
# table is extrapolating past what was actually tested, and 100us is
# already validated extensively. The floor is what makes every payload
# <=16 KiB resolve to the pre-existing default rather than to some untested
# sub-100us value the linear scaling would otherwise suggest.
gap_for_fraction() {
    local size=$1 frac=$2
    local sat=${GAP_SAT_US[$size]:-}
    if [ -z "$sat" ]; then
        echo "gap_for_fraction: no saturation entry for size=$size" >&2
        return 1
    fi
    local gap=$(( sat * 100 / frac ))
    [ "$gap" -lt 100 ] && gap=100
    echo "$gap"
}
