#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test for physical ports resource. The test splits each splittable port
# to its width and checks that eventually the number of physical ports equals
# the maximum number of physical ports.

PORT_NUM_NETIFS=0

port_setup_prepare()
{
	:
}

port_cleanup()
{
	pre_cleanup

	for port in "${unsplit[@]}"; do
		devlink port unsplit $port
		check_err $? "Did not unsplit $netdev"
	done
}

split_all_ports()
{
	local should_fail=$1; shift
	local -a unsplit

	# Loop over the splittable netdevs and create tuples of netdev along
	# with its width. For example:
	# '$netdev1 $count1 $netdev2 $count2...', when:
	# $netdev1-2 are splittable netdevs in the device, and
	# $count1-2 are the netdevs width respectively.
	while read netdev count <<<$(
		devlink -j port show |
		jq -r '.[][] | select(.splittable==true) | "\(.netdev) \(.lanes)"'
		)
		[[ ! -z $netdev ]]
	do
		devlink port split $netdev count $count
		check_err $? "Did not split $netdev into $count"
		unsplit+=( "${netdev}s0" )
	done
}

port_test()
{
	local max_ports=$1; shift
	local should_fail=$1; shift

	split_all_ports $should_fail

	occ=$(devlink -j resource show $DEVLINK_DEV \
	      | jq '.[][][] | select(.name=="physical_ports") |.["occ"]')

	[[ $occ -eq $max_ports ]]
	check_err_fail $should_fail $? "Attempt to create $max_ports ports (actual result $occ)"

}
