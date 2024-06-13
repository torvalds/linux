#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	mac_profile_test
"
NUM_NETIFS=4
source $lib_dir/lib.sh
source $lib_dir/tc_common.sh
source $lib_dir/devlink_lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/24
	ip route add 198.51.100.0/24 vrf v$h1 nexthop via 192.0.2.2

	tc qdisc add dev $h1 ingress
}

h1_destroy()
{
	tc qdisc del dev $h1 ingress

	ip route del 198.51.100.0/24 vrf v$h1
	simple_if_fini $h1 192.0.2.1/24
}

h2_create()
{
	simple_if_init $h2 198.51.100.1/24
	ip route add 192.0.2.0/24 vrf v$h2 nexthop via 198.51.100.2

	tc qdisc add dev $h2 ingress
}

h2_destroy()
{
	tc qdisc del dev $h2 ingress

	ip route del 192.0.2.0/24 vrf v$h2
	simple_if_fini $h2 198.51.100.1/24
}

router_create()
{
	ip link set dev $rp1 up
	ip link set dev $rp2 up

	tc qdisc add dev $rp1 clsact
	tc qdisc add dev $rp2 clsact
	ip address add 192.0.2.2/24 dev $rp1
	ip address add 198.51.100.2/24 dev $rp2
}

router_destroy()
{
	ip address del 198.51.100.2/24 dev $rp2
	ip address del 192.0.2.2/24 dev $rp1
	tc qdisc del dev $rp2 clsact
	tc qdisc del dev $rp1 clsact

	ip link set dev $rp2 down
	ip link set dev $rp1 down
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	rp1=${NETIFS[p2]}

	rp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	vrf_prepare

	h1_create
	h2_create

	router_create

	forwarding_enable
}

cleanup()
{
	pre_cleanup

	forwarding_restore

	router_destroy

	h2_destroy
	h1_destroy

	vrf_cleanup
}

h1_to_h2()
{
	local test_name=$@; shift
	local smac=$(mac_get $rp2)

	RET=0

	# Replace neighbour to avoid first packet being forwarded in software
	ip neigh replace dev $rp2 198.51.100.1 lladdr $(mac_get $h2)

	# Add a filter to ensure that packets are forwarded in hardware. Cannot
	# match on source MAC because it is not set in eACL after routing
	tc filter add dev $rp2 egress proto ip pref 1 handle 101 \
		flower skip_sw ip_proto udp src_port 12345 dst_port 54321 \
		action pass

	# Add a filter to ensure that packets are received with the correct
	# source MAC
	tc filter add dev $h2 ingress proto ip pref 1 handle 101 \
		flower skip_sw src_mac $smac ip_proto udp src_port 12345 \
		dst_port 54321 action pass

	$MZ $h1 -a own -b $(mac_get $rp1) -t udp "sp=12345,dp=54321" \
		-A 192.0.2.1 -B 198.51.100.1 -c 10 -p 100 -d 1msec -q

	tc_check_packets "dev $rp2 egress" 101 10
	check_err $? "packets not forwarded in hardware"

	tc_check_packets "dev $h2 ingress" 101 10
	check_err $? "packets not forwarded with correct source mac"

	log_test "h1->h2: $test_name"

	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower
	tc filter del dev $rp2 egress protocol ip pref 1 handle 101 flower
	ip neigh del dev $rp2 198.51.100.1 lladdr $(mac_get $h2)
}

h2_to_h1()
{
	local test_name=$@; shift
	local rp1_mac=$(mac_get $rp1)

	RET=0

	ip neigh replace dev $rp1 192.0.2.1 lladdr $(mac_get $h1)

	tc filter add dev $rp1 egress proto ip pref 1 handle 101 \
		flower skip_sw ip_proto udp src_port 54321 dst_port 12345 \
		action pass

	tc filter add dev $h1 ingress proto ip pref 1 handle 101 \
		flower skip_sw src_mac $rp1_mac ip_proto udp src_port 54321 \
		dst_port 12345 action pass

	$MZ $h2 -a own -b $(mac_get $rp2) -t udp "sp=54321,dp=12345" \
		-A 198.51.100.1 -B 192.0.2.1 -c 10 -p 100 -d 1msec -q

	tc_check_packets "dev $rp1 egress" 101 10
	check_err $? "packets not forwarded in hardware"

	tc_check_packets "dev $h1 ingress" 101 10
	check_err $? "packets not forwarded with correct source mac"

	log_test "h2->h1: $test_name"

	tc filter del dev $h1 ingress protocol ip pref 1 handle 101 flower
	tc filter del dev $rp1 egress protocol ip pref 1 handle 101 flower
	ip neigh del dev $rp1 192.0.2.1 lladdr $(mac_get $h1)
}

smac_test()
{
	local test_name=$@; shift

	# Test that packets forwarded to $h2 via $rp2 are forwarded with the
	# current source MAC of $rp2
	h1_to_h2 $test_name

	# Test that packets forwarded to $h1 via $rp1 are forwarded with the
	# current source MAC of $rp1. This MAC is never changed during the test,
	# but given the shared nature of MAC profile, the point is to see that
	# changes to the MAC of $rp2 do not affect that of $rp1
	h2_to_h1 $test_name
}

mac_profile_test()
{
	local rp2_mac=$(mac_get $rp2)

	# Test behavior when the RIF backing $rp2 is transitioned to use
	# a new MAC profile
	ip link set dev $rp2 addr 00:11:22:33:44:55
	smac_test "new mac profile"

	# Test behavior when the MAC profile used by the RIF is edited
	ip link set dev $rp2 address 00:22:22:22:22:22
	smac_test "edit mac profile"

	# Restore original MAC
	ip link set dev $rp2 addr $rp2_mac
}

trap cleanup EXIT

setup_prepare
setup_wait

mac_profiles=$(devlink_resource_size_get rif_mac_profiles)
if [[ $mac_profiles -ne 1 ]]; then
	tests_run
fi

exit $EXIT_STATUS
