# SPDX-License-Identifier: GPL-2.0
source ../tc_flower_scale.sh

tc_flower_get_target()
{
	local should_fail=$1; shift
	local max_cnts

	# The driver associates a counter with each tc filter, which means the
	# number of supported filters is bounded by the number of available
	# counters.
	max_cnts=$(devlink_resource_size_get counters flow)

	# Remove already allocated counters.
	((max_cnts -= $(devlink_resource_occ_get counters flow)))

	# Each rule uses two counters, for packets and bytes.
	((max_cnts /= 2))

	if ((! should_fail)); then
		echo $max_cnts
	else
		echo $((max_cnts + 1))
	fi
}
