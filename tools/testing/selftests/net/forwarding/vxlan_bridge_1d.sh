#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +--------------------+                               +----------------------+
# | H1 (vrf)           |                               |             H2 (vrf) |
# |    + $h1           |                               |  + $h2               |
# |    | 192.0.2.1/28  |                               |  | 192.0.2.2/28      |
# +----|---------------+                               +--|-------------------+
#      |                                                  |
# +----|--------------------------------------------------|-------------------+
# | SW |                                                  |                   |
# | +--|--------------------------------------------------|-----------------+ |
# | |  + $swp1                   BR1 (802.1d)             + $swp2           | |
# | |                                                                       | |
# | |  + vx1 (vxlan)                                                        | |
# | |    local 192.0.2.17                                                   | |
# | |    remote 192.0.2.34 192.0.2.50                                       | |
# | |    id 1000 dstport $VXPORT                                            | |
# | +-----------------------------------------------------------------------+ |
# |                                                                           |
# |  192.0.2.32/28 via 192.0.2.18                                             |
# |  192.0.2.48/28 via 192.0.2.18                                             |
# |                                                                           |
# |    + $rp1                                                                 |
# |    | 192.0.2.17/28                                                        |
# +----|----------------------------------------------------------------------+
#      |
# +----|--------------------------------------------------------+
# |    |                                             VRP2 (vrf) |
# |    + $rp2                                                   |
# |      192.0.2.18/28                                          |
# |                                                             |   (maybe) HW
# =============================================================================
# |                                                             |  (likely) SW
# |    + v1 (veth)                             + v3 (veth)      |
# |    | 192.0.2.33/28                         | 192.0.2.49/28  |
# +----|---------------------------------------|----------------+
#      |                                       |
# +----|------------------------------+   +----|------------------------------+
# |    + v2 (veth)        NS1 (netns) |   |    + v4 (veth)        NS2 (netns) |
# |      192.0.2.34/28                |   |      192.0.2.50/28                |
# |                                   |   |                                   |
# |   192.0.2.16/28 via 192.0.2.33    |   |   192.0.2.16/28 via 192.0.2.49    |
# |   192.0.2.50/32 via 192.0.2.33    |   |   192.0.2.34/32 via 192.0.2.49    |
# |                                   |   |                                   |
# | +-------------------------------+ |   | +-------------------------------+ |
# | |                  BR2 (802.1d) | |   | |                  BR2 (802.1d) | |
# | |  + vx2 (vxlan)                | |   | |  + vx2 (vxlan)                | |
# | |    local 192.0.2.34           | |   | |    local 192.0.2.50           | |
# | |    remote 192.0.2.17          | |   | |    remote 192.0.2.17          | |
# | |    remote 192.0.2.50          | |   | |    remote 192.0.2.34          | |
# | |    id 1000 dstport $VXPORT    | |   | |    id 1000 dstport $VXPORT    | |
# | |                               | |   | |                               | |
# | |  + w1 (veth)                  | |   | |  + w1 (veth)                  | |
# | +--|----------------------------+ |   | +--|----------------------------+ |
# |    |                              |   |    |                              |
# | +--|----------------------------+ |   | +--|----------------------------+ |
# | |  |                  VW2 (vrf) | |   | |  |                  VW2 (vrf) | |
# | |  + w2 (veth)                  | |   | |  + w2 (veth)                  | |
# | |    192.0.2.3/28               | |   | |    192.0.2.4/28               | |
# | +-------------------------------+ |   | +-------------------------------+ |
# +-----------------------------------+   +-----------------------------------+

: ${VXPORT:=4789}
export VXPORT

: ${ALL_TESTS:="
	ping_ipv4
	test_flood
	test_unicast
	test_ttl
	test_tos
	test_ecn_encap
	test_ecn_decap
	reapply_config
	ping_ipv4
	test_flood
	test_unicast
	test_learning
    "}

NUM_NETIFS=6
source lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/28
	tc qdisc add dev $h1 clsact
}

h1_destroy()
{
	tc qdisc del dev $h1 clsact
	simple_if_fini $h1 192.0.2.1/28
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/28
	tc qdisc add dev $h2 clsact
}

h2_destroy()
{
	tc qdisc del dev $h2 clsact
	simple_if_fini $h2 192.0.2.2/28
}

rp1_set_addr()
{
	ip address add dev $rp1 192.0.2.17/28

	ip route add 192.0.2.32/28 nexthop via 192.0.2.18
	ip route add 192.0.2.48/28 nexthop via 192.0.2.18
}

rp1_unset_addr()
{
	ip route del 192.0.2.48/28 nexthop via 192.0.2.18
	ip route del 192.0.2.32/28 nexthop via 192.0.2.18

	ip address del dev $rp1 192.0.2.17/28
}

switch_create()
{
	ip link add name br1 type bridge vlan_filtering 0 mcast_snooping 0
	# Make sure the bridge uses the MAC address of the local port and not
	# that of the VxLAN's device.
	ip link set dev br1 address $(mac_get $swp1)
	ip link set dev br1 up

	ip link set dev $rp1 up
	rp1_set_addr

	ip link add name vx1 type vxlan id 1000		\
		local 192.0.2.17 dstport "$VXPORT"	\
		nolearning noudpcsum tos inherit ttl 100
	ip link set dev vx1 up

	ip link set dev vx1 master br1
	ip link set dev $swp1 master br1
	ip link set dev $swp1 up

	ip link set dev $swp2 master br1
	ip link set dev $swp2 up

	bridge fdb append dev vx1 00:00:00:00:00:00 dst 192.0.2.34 self
	bridge fdb append dev vx1 00:00:00:00:00:00 dst 192.0.2.50 self
}

switch_destroy()
{
	rp1_unset_addr
	ip link set dev $rp1 down

	bridge fdb del dev vx1 00:00:00:00:00:00 dst 192.0.2.50 self
	bridge fdb del dev vx1 00:00:00:00:00:00 dst 192.0.2.34 self

	ip link set dev vx1 nomaster
	ip link set dev vx1 down
	ip link del dev vx1

	ip link set dev $swp2 down
	ip link set dev $swp2 nomaster

	ip link set dev $swp1 down
	ip link set dev $swp1 nomaster

	ip link set dev br1 down
	ip link del dev br1
}

vrp2_create()
{
	simple_if_init $rp2 192.0.2.18/28
	__simple_if_init v1 v$rp2 192.0.2.33/28
	__simple_if_init v3 v$rp2 192.0.2.49/28
	tc qdisc add dev v1 clsact
}

vrp2_destroy()
{
	tc qdisc del dev v1 clsact
	__simple_if_fini v3 192.0.2.49/28
	__simple_if_fini v1 192.0.2.33/28
	simple_if_fini $rp2 192.0.2.18/28
}

ns_init_common()
{
	local in_if=$1; shift
	local in_addr=$1; shift
	local other_in_addr=$1; shift
	local nh_addr=$1; shift
	local host_addr=$1; shift

	ip link set dev $in_if up
	ip address add dev $in_if $in_addr/28
	tc qdisc add dev $in_if clsact

	ip link add name br2 type bridge vlan_filtering 0
	ip link set dev br2 up

	ip link add name w1 type veth peer name w2

	ip link set dev w1 master br2
	ip link set dev w1 up

	ip link add name vx2 type vxlan id 1000 local $in_addr dstport "$VXPORT"
	ip link set dev vx2 up
	bridge fdb append dev vx2 00:00:00:00:00:00 dst 192.0.2.17 self
	bridge fdb append dev vx2 00:00:00:00:00:00 dst $other_in_addr self

	ip link set dev vx2 master br2
	tc qdisc add dev vx2 clsact

	simple_if_init w2 $host_addr/28

	ip route add 192.0.2.16/28 nexthop via $nh_addr
	ip route add $other_in_addr/32 nexthop via $nh_addr
}
export -f ns_init_common

ns1_create()
{
	ip netns add ns1
	ip link set dev v2 netns ns1
	in_ns ns1 \
	      ns_init_common v2 192.0.2.34 192.0.2.50 192.0.2.33 192.0.2.3
}

ns1_destroy()
{
	ip netns exec ns1 ip link set dev v2 netns 1
	ip netns del ns1
}

ns2_create()
{
	ip netns add ns2
	ip link set dev v4 netns ns2
	in_ns ns2 \
	      ns_init_common v4 192.0.2.50 192.0.2.34 192.0.2.49 192.0.2.4
}

ns2_destroy()
{
	ip netns exec ns2 ip link set dev v4 netns 1
	ip netns del ns2
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	rp1=${NETIFS[p5]}
	rp2=${NETIFS[p6]}

	vrf_prepare
	forwarding_enable

	h1_create
	h2_create
	switch_create

	ip link add name v1 type veth peer name v2
	ip link add name v3 type veth peer name v4
	vrp2_create
	ns1_create
	ns2_create

	r1_mac=$(in_ns ns1 mac_get w2)
	r2_mac=$(in_ns ns2 mac_get w2)
	h2_mac=$(mac_get $h2)
}

cleanup()
{
	pre_cleanup

	ns2_destroy
	ns1_destroy
	vrp2_destroy
	ip link del dev v3
	ip link del dev v1

	switch_destroy
	h2_destroy
	h1_destroy

	forwarding_restore
	vrf_cleanup
}

# For the first round of tests, vx1 is the first device to get attached to the
# bridge, and that at the point that the local IP is already configured. Try the
# other scenario of attaching the device to an already-offloaded bridge, and
# only then attach the local IP.
reapply_config()
{
	echo "Reapplying configuration"

	bridge fdb del dev vx1 00:00:00:00:00:00 dst 192.0.2.50 self
	bridge fdb del dev vx1 00:00:00:00:00:00 dst 192.0.2.34 self
	rp1_unset_addr
	ip link set dev vx1 nomaster
	sleep 5

	ip link set dev vx1 master br1
	bridge fdb append dev vx1 00:00:00:00:00:00 dst 192.0.2.34 self
	bridge fdb append dev vx1 00:00:00:00:00:00 dst 192.0.2.50 self
	sleep 1
	rp1_set_addr
	sleep 5
}

ping_ipv4()
{
	ping_test $h1 192.0.2.2 ": local->local"
	ping_test $h1 192.0.2.3 ": local->remote 1"
	ping_test $h1 192.0.2.4 ": local->remote 2"
}

maybe_in_ns()
{
	echo ${1:+in_ns} $1
}

__flood_counter_add_del()
{
	local add_del=$1; shift
	local dev=$1; shift
	local ns=$1; shift

	# Putting the ICMP capture both to HW and to SW will end up
	# double-counting the packets that are trapped to slow path, such as for
	# the unicast test. Adding either skip_hw or skip_sw fixes this problem,
	# but with skip_hw, the flooded packets are not counted at all, because
	# those are dropped due to MAC address mismatch; and skip_sw is a no-go
	# for veth-based topologies.
	#
	# So try to install with skip_sw and fall back to skip_sw if that fails.

	$(maybe_in_ns $ns) __icmp_capture_add_del          \
			   $add_del 100 "" $dev skip_sw 2>/dev/null || \
	$(maybe_in_ns $ns) __icmp_capture_add_del          \
			   $add_del 100 "" $dev skip_hw
}

flood_counter_install()
{
	__flood_counter_add_del add "$@"
}

flood_counter_uninstall()
{
	__flood_counter_add_del del "$@"
}

flood_fetch_stat()
{
	local dev=$1; shift
	local ns=$1; shift

	$(maybe_in_ns $ns) tc_rule_stats_get $dev 100 ingress
}

flood_fetch_stats()
{
	local counters=("${@}")
	local counter

	for counter in "${counters[@]}"; do
		flood_fetch_stat $counter
	done
}

vxlan_flood_test()
{
	local mac=$1; shift
	local dst=$1; shift
	local -a expects=("${@}")

	local -a counters=($h2 "vx2 ns1" "vx2 ns2")
	local counter
	local key

	for counter in "${counters[@]}"; do
		flood_counter_install $counter
	done

	local -a t0s=($(flood_fetch_stats "${counters[@]}"))
	$MZ $h1 -c 10 -d 100msec -p 64 -b $mac -B $dst -t icmp -q
	sleep 1
	local -a t1s=($(flood_fetch_stats "${counters[@]}"))

	for key in ${!t0s[@]}; do
		local delta=$((t1s[$key] - t0s[$key]))
		local expect=${expects[$key]}

		((expect == delta))
		check_err $? "${counters[$key]}: Expected to capture $expect packets, got $delta."
	done

	for counter in "${counters[@]}"; do
		flood_counter_uninstall $counter
	done
}

__test_flood()
{
	local mac=$1; shift
	local dst=$1; shift
	local what=$1; shift

	RET=0

	vxlan_flood_test $mac $dst 10 10 10

	log_test "VXLAN: $what"
}

test_flood()
{
	__test_flood de:ad:be:ef:13:37 192.0.2.100 "flood"
}

vxlan_fdb_add_del()
{
	local add_del=$1; shift
	local mac=$1; shift
	local dev=$1; shift
	local dst=$1; shift

	bridge fdb $add_del dev $dev $mac self static permanent \
		${dst:+dst} $dst 2>/dev/null
	bridge fdb $add_del dev $dev $mac master static 2>/dev/null
}

__test_unicast()
{
	local mac=$1; shift
	local dst=$1; shift
	local hit_idx=$1; shift
	local what=$1; shift

	RET=0

	local -a expects=(0 0 0)
	expects[$hit_idx]=10

	vxlan_flood_test $mac $dst "${expects[@]}"

	log_test "VXLAN: $what"
}

test_unicast()
{
	local -a targets=("$h2_mac $h2"
			  "$r1_mac vx1 192.0.2.34"
			  "$r2_mac vx1 192.0.2.50")
	local target

	for target in "${targets[@]}"; do
		vxlan_fdb_add_del add $target
	done

	__test_unicast $h2_mac 192.0.2.2 0 "local MAC unicast"
	__test_unicast $r1_mac 192.0.2.3 1 "remote MAC 1 unicast"
	__test_unicast $r2_mac 192.0.2.4 2 "remote MAC 2 unicast"

	for target in "${targets[@]}"; do
		vxlan_fdb_add_del del $target
	done
}

vxlan_ping_test()
{
	local ping_dev=$1; shift
	local ping_dip=$1; shift
	local ping_args=$1; shift
	local capture_dev=$1; shift
	local capture_dir=$1; shift
	local capture_pref=$1; shift
	local expect=$1; shift

	local t0=$(tc_rule_stats_get $capture_dev $capture_pref $capture_dir)
	ping_do $ping_dev $ping_dip "$ping_args"
	local t1=$(tc_rule_stats_get $capture_dev $capture_pref $capture_dir)
	local delta=$((t1 - t0))

	# Tolerate a couple stray extra packets.
	((expect <= delta && delta <= expect + 2))
	check_err $? "$capture_dev: Expected to capture $expect packets, got $delta."
}

test_ttl()
{
	RET=0

	tc filter add dev v1 egress pref 77 prot ip \
		flower ip_ttl 99 action pass
	vxlan_ping_test $h1 192.0.2.3 "" v1 egress 77 10
	tc filter del dev v1 egress pref 77 prot ip

	log_test "VXLAN: envelope TTL"
}

test_tos()
{
	RET=0

	tc filter add dev v1 egress pref 77 prot ip \
		flower ip_tos 0x14 action pass
	vxlan_ping_test $h1 192.0.2.3 "-Q 0x14" v1 egress 77 10
	vxlan_ping_test $h1 192.0.2.3 "-Q 0x18" v1 egress 77 0
	tc filter del dev v1 egress pref 77 prot ip

	log_test "VXLAN: envelope TOS inheritance"
}

__test_ecn_encap()
{
	local q=$1; shift
	local tos=$1; shift

	RET=0

	tc filter add dev v1 egress pref 77 prot ip \
		flower ip_tos $tos action pass
	sleep 1
	vxlan_ping_test $h1 192.0.2.3 "-Q $q" v1 egress 77 10
	tc filter del dev v1 egress pref 77 prot ip

	log_test "VXLAN: ECN encap: $q->$tos"
}

test_ecn_encap()
{
	# In accordance with INET_ECN_encapsulate()
	__test_ecn_encap 0x00 0x00
	__test_ecn_encap 0x01 0x01
	__test_ecn_encap 0x02 0x02
	__test_ecn_encap 0x03 0x02
}

vxlan_encapped_ping_do()
{
	local count=$1; shift
	local dev=$1; shift
	local next_hop_mac=$1; shift
	local dest_ip=$1; shift
	local dest_mac=$1; shift
	local inner_tos=$1; shift
	local outer_tos=$1; shift

	$MZ $dev -c $count -d 100msec -q \
		-b $next_hop_mac -B $dest_ip \
		-t udp tos=$outer_tos,sp=23456,dp=$VXPORT,p=$(:
		    )"08:"$(                      : VXLAN flags
		    )"00:00:00:"$(                : VXLAN reserved
		    )"00:03:e8:"$(                : VXLAN VNI
		    )"00:"$(                      : VXLAN reserved
		    )"$dest_mac:"$(               : ETH daddr
		    )"$(mac_get w2):"$(           : ETH saddr
		    )"08:00:"$(                   : ETH type
		    )"45:"$(                      : IP version + IHL
		    )"$inner_tos:"$(              : IP TOS
		    )"00:54:"$(                   : IP total length
		    )"99:83:"$(                   : IP identification
		    )"40:00:"$(                   : IP flags + frag off
		    )"40:"$(                      : IP TTL
		    )"01:"$(                      : IP proto
		    )"00:00:"$(                   : IP header csum
		    )"c0:00:02:03:"$(             : IP saddr: 192.0.2.3
		    )"c0:00:02:01:"$(             : IP daddr: 192.0.2.1
		    )"08:"$(                      : ICMP type
		    )"00:"$(                      : ICMP code
		    )"8b:f2:"$(                   : ICMP csum
		    )"1f:6a:"$(                   : ICMP request identifier
		    )"00:01:"$(                   : ICMP request sequence number
		    )"4f:ff:c5:5b:00:00:00:00:"$( : ICMP payload
		    )"6d:74:0b:00:00:00:00:00:"$( :
		    )"10:11:12:13:14:15:16:17:"$( :
		    )"18:19:1a:1b:1c:1d:1e:1f:"$( :
		    )"20:21:22:23:24:25:26:27:"$( :
		    )"28:29:2a:2b:2c:2d:2e:2f:"$( :
		    )"30:31:32:33:34:35:36:37"
}
export -f vxlan_encapped_ping_do

vxlan_encapped_ping_test()
{
	local ping_dev=$1; shift
	local nh_dev=$1; shift
	local ping_dip=$1; shift
	local inner_tos=$1; shift
	local outer_tos=$1; shift
	local stat_get=$1; shift
	local expect=$1; shift

	local t0=$($stat_get)

	in_ns ns1 \
		vxlan_encapped_ping_do 10 $ping_dev $(mac_get $nh_dev) \
			$ping_dip $(mac_get $h1) \
			$inner_tos $outer_tos

	local t1=$($stat_get)
	local delta=$((t1 - t0))

	# Tolerate a couple stray extra packets.
	((expect <= delta && delta <= expect + 2))
	check_err $? "Expected to capture $expect packets, got $delta."
}
export -f vxlan_encapped_ping_test

__test_ecn_decap()
{
	local orig_inner_tos=$1; shift
	local orig_outer_tos=$1; shift
	local decapped_tos=$1; shift

	RET=0

	tc filter add dev $h1 ingress pref 77 prot ip \
		flower ip_tos $decapped_tos action drop
	sleep 1
	vxlan_encapped_ping_test v2 v1 192.0.2.17 \
				 $orig_inner_tos $orig_outer_tos \
				 "tc_rule_stats_get $h1 77 ingress" 10
	tc filter del dev $h1 ingress pref 77

	log_test "VXLAN: ECN decap: $orig_outer_tos/$orig_inner_tos->$decapped_tos"
}

test_ecn_decap_error()
{
	local orig_inner_tos=00
	local orig_outer_tos=03

	RET=0

	vxlan_encapped_ping_test v2 v1 192.0.2.17 \
				 $orig_inner_tos $orig_outer_tos \
				 "link_stats_rx_errors_get vx1" 10

	log_test "VXLAN: ECN decap: $orig_outer_tos/$orig_inner_tos->error"
}

test_ecn_decap()
{
	# In accordance with INET_ECN_decapsulate()
	__test_ecn_decap 00 00 0x00
	__test_ecn_decap 01 01 0x01
	__test_ecn_decap 02 01 0x02
	__test_ecn_decap 01 03 0x03
	__test_ecn_decap 02 03 0x03
	test_ecn_decap_error
}

test_learning()
{
	local mac=de:ad:be:ef:13:37
	local dst=192.0.2.100

	# Enable learning on the VxLAN device and set ageing time to 10 seconds
	ip link set dev br1 type bridge ageing_time 1000
	ip link set dev vx1 type vxlan ageing 10
	ip link set dev vx1 type vxlan learning
	reapply_config

	# Check that flooding works
	RET=0

	vxlan_flood_test $mac $dst 10 10 10

	log_test "VXLAN: flood before learning"

	# Send a packet with source mac set to $mac from host w2 and check that
	# a corresponding entry is created in VxLAN device vx1
	RET=0

	in_ns ns1 $MZ w2 -c 1 -p 64 -a $mac -b ff:ff:ff:ff:ff:ff -B $dst \
		-t icmp -q
	sleep 1

	bridge fdb show brport vx1 | grep $mac | grep -q self
	check_err $?
	bridge fdb show brport vx1 | grep $mac | grep -q -v self
	check_err $?

	log_test "VXLAN: show learned FDB entry"

	# Repeat first test and check that packets only reach host w2 in ns1
	RET=0

	vxlan_flood_test $mac $dst 0 10 0

	log_test "VXLAN: learned FDB entry"

	# Delete the learned FDB entry from the VxLAN and bridge devices and
	# check that packets are flooded
	RET=0

	bridge fdb del dev vx1 $mac master self
	sleep 1

	vxlan_flood_test $mac $dst 10 10 10

	log_test "VXLAN: deletion of learned FDB entry"

	# Re-learn the first FDB entry and check that it is correctly aged-out
	RET=0

	in_ns ns1 $MZ w2 -c 1 -p 64 -a $mac -b ff:ff:ff:ff:ff:ff -B $dst \
		-t icmp -q
	sleep 1

	bridge fdb show brport vx1 | grep $mac | grep -q self
	check_err $?
	bridge fdb show brport vx1 | grep $mac | grep -q -v self
	check_err $?

	vxlan_flood_test $mac $dst 0 10 0

	sleep 20

	bridge fdb show brport vx1 | grep $mac | grep -q self
	check_fail $?
	bridge fdb show brport vx1 | grep $mac | grep -q -v self
	check_fail $?

	vxlan_flood_test $mac $dst 10 10 10

	log_test "VXLAN: Ageing of learned FDB entry"

	# Toggle learning on the bridge port and check that the bridge's FDB
	# is populated only when it should
	RET=0

	ip link set dev vx1 type bridge_slave learning off

	in_ns ns1 $MZ w2 -c 1 -p 64 -a $mac -b ff:ff:ff:ff:ff:ff -B $dst \
		-t icmp -q
	sleep 1

	bridge fdb show brport vx1 | grep $mac | grep -q -v self
	check_fail $?

	ip link set dev vx1 type bridge_slave learning on

	in_ns ns1 $MZ w2 -c 1 -p 64 -a $mac -b ff:ff:ff:ff:ff:ff -B $dst \
		-t icmp -q
	sleep 1

	bridge fdb show brport vx1 | grep $mac | grep -q -v self
	check_err $?

	log_test "VXLAN: learning toggling on bridge port"

	# Restore previous settings
	ip link set dev vx1 type vxlan nolearning
	ip link set dev vx1 type vxlan ageing 300
	ip link set dev br1 type bridge ageing_time 30000
	reapply_config
}

test_all()
{
	echo "Running tests with UDP port $VXPORT"
	tests_run
}

trap cleanup EXIT

setup_prepare
setup_wait
test_all

exit $EXIT_STATUS
