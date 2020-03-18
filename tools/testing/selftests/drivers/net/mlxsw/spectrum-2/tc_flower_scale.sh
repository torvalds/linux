# SPDX-License-Identifier: GPL-2.0
source ../tc_flower_scale.sh

tc_flower_get_target()
{
	local should_fail=$1; shift

	# The driver associates a counter with each tc filter, which means the
	# number of supported filters is bounded by the number of available
	# counters.
	# Currently, the driver supports 30K (30,720) flow counters and six of
	# these are used for multicast routing.
	local target=30714

	if ((! should_fail)); then
		echo $target
	else
		echo $((target + 1))
	fi
}
