#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test for RIF MAC profiles resource. The test adds VLAN netdevices according to
# the maximum number of RIF MAC profiles, sets each of them with a random
# MAC address, and checks that eventually the number of occupied RIF MAC
# profiles equals the maximum number of RIF MAC profiles.


RIF_MAC_PROFILE_NUM_NETIFS=2

rif_mac_profiles_create()
{
	local count=$1; shift
	local should_fail=$1; shift
	local batch_file="$(mktemp)"

	for ((i = 1; i <= count; i++)); do
		vlan=$(( i*10 ))
		m=$(( i*11 ))

		cat >> $batch_file <<-EOF
			link add link $h1 name $h1.$vlan \
				address 00:$m:$m:$m:$m:$m type vlan id $vlan
			address add 192.0.$m.1/24 dev $h1.$vlan
		EOF
	done

	ip -b $batch_file &> /dev/null
	check_err_fail $should_fail $? "RIF creation"

	rm -f $batch_file
}

rif_mac_profile_test()
{
	local count=$1; shift
	local should_fail=$1; shift

	rif_mac_profiles_create $count $should_fail

	occ=$(devlink -j resource show $DEVLINK_DEV \
	      | jq '.[][][] | select(.name=="rif_mac_profiles") |.["occ"]')

	[[ $occ -eq $count ]]
	check_err_fail $should_fail $? "Attempt to use $count profiles (actual result $occ)"
}

rif_mac_profile_setup_prepare()
{
	h1=${NETIFS[p1]}
	h2=${NETIFS[p2]}

	# Disable IPv6 on the two interfaces to avoid IPv6 link-local addresses
	# being generated and RIFs being created.
	sysctl_set net.ipv6.conf.$h1.disable_ipv6 1
	sysctl_set net.ipv6.conf.$h2.disable_ipv6 1

	ip link set $h1 up
	ip link set $h2 up
}

rif_mac_profile_cleanup()
{
	pre_cleanup

	ip link set $h2 down
	ip link set $h1 down

	sysctl_restore net.ipv6.conf.$h2.disable_ipv6
	sysctl_restore net.ipv6.conf.$h1.disable_ipv6
}
