#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

CHECK_TC="yes"

tc_check_packets()
{
	local id=$1
	local handle=$2
	local count=$3

	cmd_jq "tc -j -s filter show $id" \
	       ".[] | select(.options.handle == $handle) | \
	              select(.options.actions[0].stats.packets == $count)" \
	       &> /dev/null
}
