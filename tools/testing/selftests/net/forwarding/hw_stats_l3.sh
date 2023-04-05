#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +--------------------+                     +----------------------+
# | H1                 |                     |                   H2 |
# |                    |                     |                      |
# |          $h1.200 + |                     | + $h2.200            |
# |     192.0.2.1/28 | |                     | | 192.0.2.18/28      |
# | 2001:db8:1::1/64 | |                     | | 2001:db8:2::1/64   |
# |                  | |                     | |                    |
# |              $h1 + |                     | + $h2                |
# |                  | |                     | |                    |
# +------------------|-+                     +-|--------------------+
#                    |                         |
# +------------------|-------------------------|--------------------+
# | SW               |                         |                    |
# |                  |                         |                    |
# |             $rp1 +                         + $rp2               |
# |                  |                         |                    |
# |         $rp1.200 +                         + $rp2.200           |
# |     192.0.2.2/28                             192.0.2.17/28      |
# | 2001:db8:1::2/64                             2001:db8:2::2/64   |
# |                                                                 |
# +-----------------------------------------------------------------+

ALL_TESTS="
	ping_ipv4
	ping_ipv6
	test_stats_rx_ipv4
	test_stats_tx_ipv4
	test_stats_rx_ipv6
	test_stats_tx_ipv6
	respin_enablement
	test_stats_rx_ipv4
	test_stats_tx_ipv4
	test_stats_rx_ipv6
	test_stats_tx_ipv6
	reapply_config
	ping_ipv4
	ping_ipv6
	test_stats_rx_ipv4
	test_stats_tx_ipv4
	test_stats_rx_ipv6
	test_stats_tx_ipv6
	test_stats_report_rx
	test_stats_report_tx
	test_destroy_enabled
	test_double_enable
"
NUM_NETIFS=4
source lib.sh

h1_create()
{
	simple_if_init $h1
	vlan_create $h1 200 v$h1 192.0.2.1/28 2001:db8:1::1/64
	ip route add 192.0.2.16/28 vrf v$h1 nexthop via 192.0.2.2
	ip -6 route add 2001:db8:2::/64 vrf v$h1 nexthop via 2001:db8:1::2
}

h1_destroy()
{
	ip -6 route del 2001:db8:2::/64 vrf v$h1 nexthop via 2001:db8:1::2
	ip route del 192.0.2.16/28 vrf v$h1 nexthop via 192.0.2.2
	vlan_destroy $h1 200
	simple_if_fini $h1
}

h2_create()
{
	simple_if_init $h2
	vlan_create $h2 200 v$h2 192.0.2.18/28 2001:db8:2::1/64
	ip route add 192.0.2.0/28 vrf v$h2 nexthop via 192.0.2.17
	ip -6 route add 2001:db8:1::/64 vrf v$h2 nexthop via 2001:db8:2::2
}

h2_destroy()
{
	ip -6 route del 2001:db8:1::/64 vrf v$h2 nexthop via 2001:db8:2::2
	ip route del 192.0.2.0/28 vrf v$h2 nexthop via 192.0.2.17
	vlan_destroy $h2 200
	simple_if_fini $h2
}

router_rp1_200_create()
{
	ip link add name $rp1.200 up \
		link $rp1 addrgenmode eui64 type vlan id 200
	ip address add dev $rp1.200 192.0.2.2/28
	ip address add dev $rp1.200 2001:db8:1::2/64
	ip stats set dev $rp1.200 l3_stats on
}

router_rp1_200_destroy()
{
	ip stats set dev $rp1.200 l3_stats off
	ip address del dev $rp1.200 2001:db8:1::2/64
	ip address del dev $rp1.200 192.0.2.2/28
	ip link del dev $rp1.200
}

router_create()
{
	ip link set dev $rp1 up
	router_rp1_200_create

	ip link set dev $rp2 up
	vlan_create $rp2 200 "" 192.0.2.17/28 2001:db8:2::2/64
}

router_destroy()
{
	vlan_destroy $rp2 200
	ip link set dev $rp2 down

	router_rp1_200_destroy
	ip link set dev $rp1 down
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	rp1=${NETIFS[p2]}

	rp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	rp1mac=$(mac_get $rp1)
	rp2mac=$(mac_get $rp2)

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

ping_ipv4()
{
	ping_test $h1.200 192.0.2.18 " IPv4"
}

ping_ipv6()
{
	ping_test $h1.200 2001:db8:2::1 " IPv6"
}

send_packets_rx_ipv4()
{
	# Send 21 packets instead of 20, because the first one might trap and go
	# through the SW datapath, which might not bump the HW counter.
	$MZ $h1.200 -c 21 -d 20msec -p 100 \
	    -a own -b $rp1mac -A 192.0.2.1 -B 192.0.2.18 \
	    -q -t udp sp=54321,dp=12345
}

send_packets_rx_ipv6()
{
	$MZ $h1.200 -6 -c 21 -d 20msec -p 100 \
	    -a own -b $rp1mac -A 2001:db8:1::1 -B 2001:db8:2::1 \
	    -q -t udp sp=54321,dp=12345
}

send_packets_tx_ipv4()
{
	$MZ $h2.200 -c 21 -d 20msec -p 100 \
	    -a own -b $rp2mac -A 192.0.2.18 -B 192.0.2.1 \
	    -q -t udp sp=54321,dp=12345
}

send_packets_tx_ipv6()
{
	$MZ $h2.200 -6 -c 21 -d 20msec -p 100 \
	    -a own -b $rp2mac -A 2001:db8:2::1 -B 2001:db8:1::1 \
	    -q -t udp sp=54321,dp=12345
}

___test_stats()
{
	local dir=$1; shift
	local prot=$1; shift

	local a
	local b

	a=$(hw_stats_get l3_stats $rp1.200 ${dir} packets)
	send_packets_${dir}_${prot}
	"$@"
	b=$(busywait "$TC_HIT_TIMEOUT" until_counter_is ">= $a + 20" \
		       hw_stats_get l3_stats $rp1.200 ${dir} packets)
	check_err $? "Traffic not reflected in the counter: $a -> $b"
}

__test_stats()
{
	local dir=$1; shift
	local prot=$1; shift

	RET=0
	___test_stats "$dir" "$prot"
	log_test "Test $dir packets: $prot"
}

test_stats_rx_ipv4()
{
	__test_stats rx ipv4
}

test_stats_tx_ipv4()
{
	__test_stats tx ipv4
}

test_stats_rx_ipv6()
{
	__test_stats rx ipv6
}

test_stats_tx_ipv6()
{
	__test_stats tx ipv6
}

# Make sure everything works well even after stats have been disabled and
# reenabled on the same device without touching the L3 configuration.
respin_enablement()
{
	log_info "Turning stats off and on again"
	ip stats set dev $rp1.200 l3_stats off
	ip stats set dev $rp1.200 l3_stats on
}

# For the initial run, l3_stats is enabled on a completely set up netdevice. Now
# do it the other way around: enabling the L3 stats on an L2 netdevice, and only
# then apply the L3 configuration.
reapply_config()
{
	log_info "Reapplying configuration"

	router_rp1_200_destroy

	ip link add name $rp1.200 link $rp1 addrgenmode none type vlan id 200
	ip stats set dev $rp1.200 l3_stats on
	ip link set dev $rp1.200 up addrgenmode eui64
	ip address add dev $rp1.200 192.0.2.2/28
	ip address add dev $rp1.200 2001:db8:1::2/64
}

__test_stats_report()
{
	local dir=$1; shift
	local prot=$1; shift

	local a
	local b

	RET=0

	a=$(hw_stats_get l3_stats $rp1.200 ${dir} packets)
	send_packets_${dir}_${prot}
	ip address flush dev $rp1.200
	b=$(busywait "$TC_HIT_TIMEOUT" until_counter_is ">= $a + 20" \
		       hw_stats_get l3_stats $rp1.200 ${dir} packets)
	check_err $? "Traffic not reflected in the counter: $a -> $b"
	log_test "Test ${dir} packets: stats pushed on loss of L3"

	ip stats set dev $rp1.200 l3_stats off
	ip link del dev $rp1.200
	router_rp1_200_create
}

test_stats_report_rx()
{
	__test_stats_report rx ipv4
}

test_stats_report_tx()
{
	__test_stats_report tx ipv4
}

test_destroy_enabled()
{
	RET=0

	ip link del dev $rp1.200
	router_rp1_200_create

	log_test "Destroy l3_stats-enabled netdev"
}

test_double_enable()
{
	RET=0
	___test_stats rx ipv4 \
		ip stats set dev $rp1.200 l3_stats on
	log_test "Test stat retention across a spurious enablement"
}

trap cleanup EXIT

setup_prepare
setup_wait

used=$(ip -j stats show dev $rp1.200 group offload subgroup hw_stats_info |
	   jq '.[].info.l3_stats.used')
kind=$(ip -j -d link show dev $rp1 |
	   jq -r '.[].linkinfo.info_kind')
if [[ $used != true ]]; then
	if [[ $kind == veth ]]; then
		log_test_skip "l3_stats not offloaded on veth interface"
		EXIT_STATUS=$ksft_skip
	else
		RET=1 log_test "l3_stats not offloaded"
	fi
else
	tests_run
fi

exit $EXIT_STATUS
