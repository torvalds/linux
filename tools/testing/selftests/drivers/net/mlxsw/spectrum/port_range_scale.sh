# SPDX-License-Identifier: GPL-2.0
source ../port_range_scale.sh

port_range_get_target()
{
	local should_fail=$1; shift
	local target

	target=$(devlink_resource_size_get port_range_registers)

	if ((! should_fail)); then
		echo $target
	else
		echo $((target + 1))
	fi
}
