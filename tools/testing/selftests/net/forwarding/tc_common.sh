#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

CHECK_TC="yes"

# Can be overridden by the configuration file. See lib.sh
TC_HIT_TIMEOUT=${TC_HIT_TIMEOUT:=1000} # ms

__tc_check_packets()
{
	local id=$1
	local handle=$2
	local count=$3
	local operator=$4

	start_time="$(date -u +%s%3N)"
	while true
	do
		cmd_jq "tc -j -s filter show $id" \
		       ".[] | select(.options.handle == $handle) | \
			    select(.options.actions[0].stats.packets $operator $count)" \
		    &> /dev/null
		ret=$?
		if [[ $ret -eq 0 ]]; then
			return $ret
		fi
		current_time="$(date -u +%s%3N)"
		diff=$(expr $current_time - $start_time)
		if [ "$diff" -gt "$TC_HIT_TIMEOUT" ]; then
			return 1
		fi
	done
}

tc_check_packets()
{
	local id=$1
	local handle=$2
	local count=$3

	__tc_check_packets "$id" "$handle" "$count" "=="
}

tc_check_packets_hitting()
{
	local id=$1
	local handle=$2

	__tc_check_packets "$id" "$handle" 0 ">"
}
