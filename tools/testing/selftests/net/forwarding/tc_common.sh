#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

CHECK_TC="yes"

tc_check_packets()
{
	local id=$1
	local handle=$2
	local count=$3
	local ret

	output="$(tc -j -s filter show $id)"
	# workaround the jq bug which causes jq to return 0 in case input is ""
	ret=$?
	if [[ $ret -ne 0 ]]; then
		return $ret
	fi
	echo $output | \
		jq -e ".[] \
		| select(.options.handle == $handle) \
		| select(.options.actions[0].stats.packets == $count)" \
		&> /dev/null
	return $?
}
