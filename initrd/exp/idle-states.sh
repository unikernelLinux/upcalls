#!/bin/bash
#
# Disable CPU sleep (idle) states by writing 1 to every cpuidle state except
# state0 (POLL), so cores stay in C0/POLL instead of entering C1+. On the
# baremetal targets this is belt-and-suspenders with the intel_idle.max_cstate=0
# / processor.max_cstate=0 kargs; it also covers environments booted without
# those kargs. Whatever states the active idle driver exposes get disabled.

if [[ ! -z $1 && $1 != "enable" && $1 != "disable" ]]; then
    echo "Invalid argument: $1" >&2
    echo ""
    echo "Usage: $(basename $0) [disable|enable]"
    exit 1
fi

if [[ $1 == "disable" ]]; then
	for S in /sys/devices/system/cpu/cpu[0-9]*/cpuidle/state[1-9]*/disable; do
		[ -e "$S" ] && echo "1" > "$S"
	done
fi

if [[ $1 == "enable" ]]; then
	for S in /sys/devices/system/cpu/cpu[0-9]*/cpuidle/state[1-9]*/disable; do
		[ -e "$S" ] && echo "0" > "$S"
	done
fi
