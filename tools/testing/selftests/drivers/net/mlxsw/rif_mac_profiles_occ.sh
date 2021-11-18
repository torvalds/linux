#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	rif_mac_profile_edit_test
"
NUM_NETIFS=2
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh

setup_prepare()
{
	h1=${NETIFS[p1]}
	h2=${NETIFS[p2]}

	# Disable IPv6 on the two interfaces to avoid IPv6 link-local addresses
	# being generated and RIFs being created
	sysctl_set net.ipv6.conf.$h1.disable_ipv6 1
	sysctl_set net.ipv6.conf.$h2.disable_ipv6 1

	ip link set $h1 up
	ip link set $h2 up
}

cleanup()
{
	pre_cleanup

	ip link set $h2 down
	ip link set $h1 down

	sysctl_restore net.ipv6.conf.$h2.disable_ipv6
	sysctl_restore net.ipv6.conf.$h1.disable_ipv6

	# Reload in order to clean all the RIFs and RIF MAC profiles created
	devlink_reload
}

create_max_rif_mac_profiles()
{
	local count=$1; shift
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
	rm -f $batch_file
}

rif_mac_profile_replacement_test()
{
	local h1_10_mac=$(mac_get $h1.10)

	RET=0

	ip link set $h1.10 address 00:12:34:56:78:99
	check_err $?

	log_test "RIF MAC profile replacement"

	ip link set $h1.10 address $h1_10_mac
}

rif_mac_profile_shared_replacement_test()
{
	local count=$1; shift
	local i=$((count + 1))
	local vlan=$(( i*10 ))
	local m=11

	RET=0

	# Create a VLAN netdevice that has the same MAC as the first one.
	ip link add link $h1 name $h1.$vlan address 00:$m:$m:$m:$m:$m \
		type vlan id $vlan
	ip address add 192.0.$m.1/24 dev $h1.$vlan

	# MAC replacement should fail because all the MAC profiles are in use
	# and the profile is shared between multiple RIFs
	m=$(( i*11 ))
	ip link set $h1.$vlan address 00:$m:$m:$m:$m:$m &> /dev/null
	check_fail $?

	log_test "RIF MAC profile shared replacement"

	ip link del dev $h1.$vlan
}

rif_mac_profile_edit_test()
{
	local count=$(devlink_resource_size_get rif_mac_profiles)

	create_max_rif_mac_profiles $count

	rif_mac_profile_replacement_test
	rif_mac_profile_shared_replacement_test $count
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
