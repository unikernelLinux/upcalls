#!/bin/bash
#
# Pin CPU frequency (disable DVFS) by forcing the performance governor, so the
# governor stops scaling cores down between bursts. With turbo-boost.sh already
# setting no_turbo=1, "performance" pins every core at the max non-turbo P-state.
# Assumes intel_pstate (active mode): governors are performance/powersave, and
# "powersave" is the scaling default restored on enable.

if [[ ! -z $1 && $1 != "enable" && $1 != "disable" ]]; then
    echo "Invalid argument: $1" >&2
    echo ""
    echo "Usage: $(basename $0) [disable|enable]"
    exit 1
fi

if [[ $1 == "disable" ]]; then
	for G in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor; do
		[ -e "$G" ] && echo "performance" > "$G"
	done
fi

if [[ $1 == "enable" ]]; then
	for G in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor; do
		[ -e "$G" ] && echo "powersave" > "$G"
	done
fi
