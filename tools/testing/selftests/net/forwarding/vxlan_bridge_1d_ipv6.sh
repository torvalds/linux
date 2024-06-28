#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +-----------------------+                          +------------------------+
# | H1 (vrf)              |                          | H2 (vrf)               |
# |    + $h1              |                          |    + $h2               |
# |    | 192.0.2.1/28     |                          |    | 192.0.2.2/28      |
# |    | 2001:db8:1::1/64 |                          |    | 2001:db8:1::2/64  |
# +----|------------------+                          +----|-------------------+
#      |                                                  |
# +----|--------------------------------------------------|-------------------+
# | SW |                                                  |                   |
# | +--|--------------------------------------------------|-----------------+ |
# | |  + $swp1                   BR1 (802.1d)             + $swp2           | |
# | |                                                                       | |
# | |  + vx1 (vxlan)                                                        | |
# | |    local 2001:db8:3::1                                                | |
# | |    remote 2001:db8:4::1 2001:db8:5::1                                 | |
# | |    id 1000 dstport $VXPORT                                            | |
# | +-----------------------------------------------------------------------+ |
# |                                                                           |
# |  2001:db8:4::0/64 via 2001:db8:3::2                                       |
# |  2001:db8:5::0/64 via 2001:db8:3::2                                       |
# |                                                                           |
# |    + $rp1                                                                 |
# |    | 2001:db8:3::1/64                                                     |
# +----|----------------------------------------------------------------------+
#      |
# +----|----------------------------------------------------------+
# |    |                                             VRP2 (vrf)   |
# |    + $rp2                                                     |
# |      2001:db8:3::2/64                                         |
# |                                                               |  (maybe) HW
# =============================================================================
# |                                                               |  (likely) SW
# |    + v1 (veth)                             + v3 (veth)        |
# |    | 2001:db8:4::2/64                      | 2001:db8:5::2/64 |
# +----|---------------------------------------|------------------+
#      |                                       |
# +----|--------------------------------+ +----|-------------------------------+
# |    + v2 (veth)        NS1 (netns)   | |    + v4 (veth)        NS2 (netns)  |
# |      2001:db8:4::1/64               | |      2001:db8:5::1/64              |
# |                                     | |                                    |
# | 2001:db8:3::0/64 via 2001:db8:4::2  | | 2001:db8:3::0/64 via 2001:db8:5::2 |
# | 2001:db8:5::1/128 via 2001:db8:4::2 | | 2001:db8:4::1/128 via              |
# |                                     | |         2001:db8:5::2              |
# |                                     | |                                    |
# | +-------------------------------+   | | +-------------------------------+  |
# | |                  BR2 (802.1d) |   | | |                  BR2 (802.1d) |  |
# | |  + vx2 (vxlan)                |   | | |  + vx2 (vxlan)                |  |
# | |    local 2001:db8:4::1        |   | | |    local 2001:db8:5::1        |  |
# | |    remote 2001:db8:3::1       |   | | |    remote 2001:db8:3::1       |  |
# | |    remote 2001:db8:5::1       |   | | |    remote 2001:db8:4::1       |  |
# | |    id 1000 dstport $VXPORT    |   | | |    id 1000 dstport $VXPORT    |  |
# | |                               |   | | |                               |  |
# | |  + w1 (veth)                  |   | | |  + w1 (veth)                  |  |
# | +--|----------------------------+   | | +--|----------------------------+  |
# |    |                                | |    |                               |
# | +--|----------------------------+   | | +--|----------------------------+  |
# | |  + w2 (veth)        VW2 (vrf) |   | | |  + w2 (veth)        VW2 (vrf) |  |
# | |    192.0.2.3/28               |   | | |    192.0.2.4/28               |  |
# | |    2001:db8:1::3/64           |   | | |    2001:db8:1::4/64           |  |
# | +-------------------------------+   | | +-------------------------------+  |
# +-------------------------------------+ +------------------------------------+

: ${VXPORT:=4789}
export VXPORT

: ${ALL_TESTS:="
	ping_ipv4
	ping_ipv6
	test_flood
	test_unicast
	test_ttl
	test_tos
	test_ecn_encap
	test_ecn_decap
	reapply_config
	ping_ipv4
	ping_ipv6
	test_flood
	test_unicast
"}

NUM_NETIFS=6
source lib.sh
source tc_common.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/28 2001:db8:1::1/64
	tc qdisc add dev $h1 clsact
}

h1_destroy()
{
	tc qdisc del dev $h1 clsact
	simple_if_fini $h1 192.0.2.1/28 2001:db8:1::1/64
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/28 2001:db8:1::2/64
	tc qdisc add dev $h2 clsact
}

h2_destroy()
{
	tc qdisc del dev $h2 clsact
	simple_if_fini $h2 192.0.2.2/28 2001:db8:1::2/64
}

rp1_set_addr()
{
	ip address add dev $rp1 2001:db8:3::1/64

	ip route add 2001:db8:4::0/64 nexthop via 2001:db8:3::2
	ip route add 2001:db8:5::0/64 nexthop via 2001:db8:3::2
}

rp1_unset_addr()
{
	ip route del 2001:db8:5::0/64 nexthop via 2001:db8:3::2
	ip route del 2001:db8:4::0/64 nexthop via 2001:db8:3::2

	ip address del dev $rp1 2001:db8:3::1/64
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
	tc qdisc add dev $rp1 clsact

	ip link add name vx1 type vxlan id 1000	local 2001:db8:3::1 \
		dstport "$VXPORT" nolearning udp6zerocsumrx udp6zerocsumtx \
		tos inherit ttl 100
	ip link set dev vx1 up

	ip link set dev vx1 master br1
	ip link set dev $swp1 master br1
	ip link set dev $swp1 up
	tc qdisc add dev $swp1 clsact

	ip link set dev $swp2 master br1
	ip link set dev $swp2 up

	bridge fdb append dev vx1 00:00:00:00:00:00 dst 2001:db8:4::1 self
	bridge fdb append dev vx1 00:00:00:00:00:00 dst 2001:db8:5::1 self
}

switch_destroy()
{
	bridge fdb del dev vx1 00:00:00:00:00:00 dst 2001:db8:5::1 self
	bridge fdb del dev vx1 00:00:00:00:00:00 dst 2001:db8:4::1 self

	ip link set dev $swp2 down
	ip link set dev $swp2 nomaster

	tc qdisc del dev $swp1 clsact
	ip link set dev $swp1 down
	ip link set dev $swp1 nomaster

	ip link set dev vx1 nomaster
	ip link set dev vx1 down
	ip link del dev vx1

	tc qdisc del dev $rp1 clsact
	rp1_unset_addr
	ip link set dev $rp1 down

	ip link set dev br1 down
	ip link del dev br1
}

vrp2_create()
{
	simple_if_init $rp2 2001:db8:3::2/64
	__simple_if_init v1 v$rp2 2001:db8:4::2/64
	__simple_if_init v3 v$rp2 2001:db8:5::2/64
	tc qdisc add dev v1 clsact
}

vrp2_destroy()
{
	tc qdisc del dev v1 clsact
	__simple_if_fini v3 2001:db8:5::2/64
	__simple_if_fini v1 2001:db8:4::2/64
	simple_if_fini $rp2 2001:db8:3::2/64
}

ns_init_common()
{
	local in_if=$1; shift
	local in_addr=$1; shift
	local other_in_addr=$1; shift
	local nh_addr=$1; shift
	local host_addr_ipv4=$1; shift
	local host_addr_ipv6=$1; shift

	ip link set dev $in_if up
	ip address add dev $in_if $in_addr/64
	tc qdisc add dev $in_if clsact

	ip link add name br2 type bridge vlan_filtering 0
	ip link set dev br2 up

	ip link add name w1 type veth peer name w2

	ip link set dev w1 master br2
	ip link set dev w1 up

	ip link add name vx2 type vxlan id 1000 local $in_addr \
		dstport "$VXPORT" udp6zerocsumrx
	ip link set dev vx2 up
	bridge fdb append dev vx2 00:00:00:00:00:00 dst 2001:db8:3::1 self
	bridge fdb append dev vx2 00:00:00:00:00:00 dst $other_in_addr self

	ip link set dev vx2 master br2
	tc qdisc add dev vx2 clsact

	simple_if_init w2 $host_addr_ipv4/28 $host_addr_ipv6/64

	ip route add 2001:db8:3::0/64 nexthop via $nh_addr
	ip route add $other_in_addr/128 nexthop via $nh_addr
}
export -f ns_init_common

ns1_create()
{
	ip netns add ns1
	ip link set dev v2 netns ns1
	in_ns ns1 \
	      ns_init_common v2 2001:db8:4::1 2001:db8:5::1 2001:db8:4::2 \
	      192.0.2.3 2001:db8:1::3
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
	      ns_init_common v4 2001:db8:5::1 2001:db8:4::1 2001:db8:5::2 \
	      192.0.2.4 2001:db8:1::4
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

# For the first round of tests, vx1 is the first device to get
# attached to the bridge, and at that point the local IP is already
# configured. Try the other scenario of attaching the devices to a an
# already-offloaded bridge, and only then assign the local IP.
reapply_config()
{
	log_info "Reapplying configuration"

	bridge fdb del dev vx1 00:00:00:00:00:00 dst 2001:db8:5::1 self
	bridge fdb del dev vx1 00:00:00:00:00:00 dst 2001:db8:4::1 self
	ip link set dev vx1 nomaster
	rp1_unset_addr
	sleep 5

	ip link set dev vx1 master br1
	bridge fdb append dev vx1 00:00:00:00:00:00 dst 2001:db8:4::1 self
	bridge fdb append dev vx1 00:00:00:00:00:00 dst 2001:db8:5::1 self
	sleep 1
	rp1_set_addr
	sleep 5
}

__ping_ipv4()
{
	local vxlan_local_ip=$1; shift
	local vxlan_remote_ip=$1; shift
	local src_ip=$1; shift
	local dst_ip=$1; shift
	local dev=$1; shift
	local info=$1; shift

	RET=0

	tc filter add dev $rp1 egress protocol ipv6 pref 1 handle 101 \
		flower ip_proto udp src_ip $vxlan_local_ip \
		dst_ip $vxlan_remote_ip dst_port $VXPORT $TC_FLAG action pass
	# Match ICMP-reply packets after decapsulation, so source IP is
	# destination IP of the ping and destination IP is source IP of the
	# ping.
	tc filter add dev $swp1 egress protocol ip pref 1 handle 101 \
		flower src_ip $dst_ip dst_ip $src_ip \
		$TC_FLAG action pass

	# Send 100 packets and verify that at least 100 packets hit the rule,
	# to overcome ARP noise.
	PING_COUNT=100 PING_TIMEOUT=20 ping_do $dev $dst_ip
	check_err $? "Ping failed"

	tc_check_at_least_x_packets "dev $rp1 egress" 101 10 100
	check_err $? "Encapsulated packets did not go through router"

	tc_check_at_least_x_packets "dev $swp1 egress" 101 10 100
	check_err $? "Decapsulated packets did not go through switch"

	log_test "ping: $info"

	tc filter del dev $swp1 egress
	tc filter del dev $rp1 egress
}

ping_ipv4()
{
	RET=0

	local local_sw_ip=2001:db8:3::1
	local remote_ns1_ip=2001:db8:4::1
	local remote_ns2_ip=2001:db8:5::1
	local h1_ip=192.0.2.1
	local w2_ns1_ip=192.0.2.3
	local w2_ns2_ip=192.0.2.4

	ping_test $h1 192.0.2.2 ": local->local"

	__ping_ipv4 $local_sw_ip $remote_ns1_ip $h1_ip $w2_ns1_ip $h1 \
		"local->remote 1"
	__ping_ipv4 $local_sw_ip $remote_ns2_ip $h1_ip $w2_ns2_ip $h1 \
		"local->remote 2"
}

__ping_ipv6()
{
	local vxlan_local_ip=$1; shift
	local vxlan_remote_ip=$1; shift
	local src_ip=$1; shift
	local dst_ip=$1; shift
	local dev=$1; shift
	local info=$1; shift

	RET=0

	tc filter add dev $rp1 egress protocol ipv6 pref 1 handle 101 \
		flower ip_proto udp src_ip $vxlan_local_ip \
		dst_ip $vxlan_remote_ip dst_port $VXPORT $TC_FLAG action pass
	# Match ICMP-reply packets after decapsulation, so source IP is
	# destination IP of the ping and destination IP is source IP of the
	# ping.
	tc filter add dev $swp1 egress protocol ipv6 pref 1 handle 101 \
		flower src_ip $dst_ip dst_ip $src_ip $TC_FLAG action pass

	# Send 100 packets and verify that at least 100 packets hit the rule,
	# to overcome neighbor discovery noise.
	PING_COUNT=100 PING_TIMEOUT=20 ping6_do $dev $dst_ip
	check_err $? "Ping failed"

	tc_check_at_least_x_packets "dev $rp1 egress" 101 100
	check_err $? "Encapsulated packets did not go through router"

	tc_check_at_least_x_packets "dev $swp1 egress" 101 100
	check_err $? "Decapsulated packets did not go through switch"

	log_test "ping6: $info"

	tc filter del dev $swp1 egress
	tc filter del dev $rp1 egress
}

ping_ipv6()
{
	RET=0

	local local_sw_ip=2001:db8:3::1
	local remote_ns1_ip=2001:db8:4::1
	local remote_ns2_ip=2001:db8:5::1
	local h1_ip=2001:db8:1::1
	local w2_ns1_ip=2001:db8:1::3
	local w2_ns2_ip=2001:db8:1::4

	ping6_test $h1 2001:db8:1::2 ": local->local"

	__ping_ipv6 $local_sw_ip $remote_ns1_ip $h1_ip $w2_ns1_ip $h1 \
		"local->remote 1"
	__ping_ipv6 $local_sw_ip $remote_ns2_ip $h1_ip $w2_ns2_ip $h1 \
		"local->remote 2"
}

maybe_in_ns()
{
	echo ${1:+in_ns} $1
}

__flood_counter_add_del()
{
	local add_del=$1; shift
	local dst_ip=$1; shift
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

	$(maybe_in_ns $ns) tc filter $add_del dev "$dev" ingress \
	   proto ipv6 pref 100 flower dst_ip $dst_ip ip_proto \
	   icmpv6 skip_sw action pass 2>/dev/null || \
	$(maybe_in_ns $ns) tc filter $add_del dev "$dev" ingress \
	   proto ipv6 pref 100 flower dst_ip $dst_ip ip_proto \
	   icmpv6 skip_hw action pass
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
		flood_counter_install $dst $counter
	done

	local -a t0s=($(flood_fetch_stats "${counters[@]}"))
	$MZ -6 $h1 -c 10 -d 100msec -p 64 -b $mac -B $dst -t icmp6 type=128 -q
	sleep 1
	local -a t1s=($(flood_fetch_stats "${counters[@]}"))

	for key in ${!t0s[@]}; do
		local delta=$((t1s[$key] - t0s[$key]))
		local expect=${expects[$key]}

		((expect == delta))
		check_err $? "${counters[$key]}: Expected to capture $expect packets, got $delta."
	done

	for counter in "${counters[@]}"; do
		flood_counter_uninstall $dst $counter
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
	__test_flood de:ad:be:ef:13:37 2001:db8:1::100 "flood"
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
			  "$r1_mac vx1 2001:db8:4::1"
			  "$r2_mac vx1 2001:db8:5::1")
	local target

	for target in "${targets[@]}"; do
		vxlan_fdb_add_del add $target
	done

	__test_unicast $h2_mac 2001:db8:1::2 0 "local MAC unicast"
	__test_unicast $r1_mac 2001:db8:1::3 1 "remote MAC 1 unicast"
	__test_unicast $r2_mac 2001:db8:1::4 2 "remote MAC 2 unicast"

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
	ping6_do $ping_dev $ping_dip "$ping_args"
	local t1=$(tc_rule_stats_get $capture_dev $capture_pref $capture_dir)
	local delta=$((t1 - t0))

	# Tolerate a couple stray extra packets.
	((expect <= delta && delta <= expect + 2))
	check_err $? "$capture_dev: Expected to capture $expect packets, got $delta."
}

test_ttl()
{
	RET=0

	tc filter add dev v1 egress pref 77 protocol ipv6 \
		flower ip_ttl 99 action pass
	vxlan_ping_test $h1 2001:db8:1::3 "" v1 egress 77 10
	tc filter del dev v1 egress pref 77 protocol ipv6

	log_test "VXLAN: envelope TTL"
}

test_tos()
{
	RET=0

	tc filter add dev v1 egress pref 77 protocol ipv6 \
		flower ip_tos 0x14 action pass
	vxlan_ping_test $h1 2001:db8:1::3 "-Q 0x14" v1 egress 77 10
	vxlan_ping_test $h1 2001:db8:1::3 "-Q 0x18" v1 egress 77 0
	tc filter del dev v1 egress pref 77 protocol ipv6

	log_test "VXLAN: envelope TOS inheritance"
}

__test_ecn_encap()
{
	local q=$1; shift
	local tos=$1; shift

	RET=0

	tc filter add dev v1 egress pref 77 protocol ipv6 \
		flower ip_tos $tos action pass
	sleep 1
	vxlan_ping_test $h1 2001:db8:1::3 "-Q $q" v1 egress 77 10
	tc filter del dev v1 egress pref 77 protocol ipv6

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
	local saddr="20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:03"
	local daddr="20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:01"

	$MZ -6 $dev -c $count -d 100msec -q \
		-b $next_hop_mac -B $dest_ip \
		-t udp tos=$outer_tos,sp=23456,dp=$VXPORT,p=$(:
		    )"08:"$(                      : VXLAN flags
		    )"00:00:00:"$(                : VXLAN reserved
		    )"00:03:e8:"$(                : VXLAN VNI
		    )"00:"$(                      : VXLAN reserved
		    )"$dest_mac:"$(               : ETH daddr
		    )"$(mac_get w2):"$(           : ETH saddr
		    )"86:dd:"$(                   : ETH type
		    )"6"$(			  : IP version
		    )"$inner_tos"$(               : Traffic class
		    )"0:00:00:"$(                 : Flow label
		    )"00:08:"$(                   : Payload length
		    )"3a:"$(                      : Next header
		    )"04:"$(                      : Hop limit
		    )"$saddr:"$(		  : IP saddr
		    )"$daddr:"$(		  : IP daddr
		    )"80:"$(			  : ICMPv6.type
		    )"00:"$(			  : ICMPv6.code
		    )"00:"$(			  : ICMPv6.checksum
		    )
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
	sleep 1
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

	tc filter add dev $h1 ingress pref 77 protocol ipv6 \
		flower src_ip 2001:db8:1::3 dst_ip 2001:db8:1::1 \
		ip_tos $decapped_tos action drop
	sleep 1
	vxlan_encapped_ping_test v2 v1 2001:db8:3::1 \
				 $orig_inner_tos $orig_outer_tos \
				 "tc_rule_stats_get $h1 77 ingress" 10
	tc filter del dev $h1 ingress pref 77

	log_test "VXLAN: ECN decap: $orig_outer_tos/$orig_inner_tos->$decapped_tos"
}

test_ecn_decap_error()
{
	local orig_inner_tos="0:0"
	local orig_outer_tos=03

	RET=0

	vxlan_encapped_ping_test v2 v1 2001:db8:3::1 \
				 $orig_inner_tos $orig_outer_tos \
				 "link_stats_rx_errors_get vx1" 10

	log_test "VXLAN: ECN decap: $orig_outer_tos/$orig_inner_tos->error"
}

test_ecn_decap()
{
	# In accordance with INET_ECN_decapsulate()
	__test_ecn_decap "0:0" 00 0x00
	__test_ecn_decap "0:0" 01 0x00
	__test_ecn_decap "0:0" 02 0x00
	# 00 03 is tested in test_ecn_decap_error()
	__test_ecn_decap "0:1" 00 0x01
	__test_ecn_decap "0:1" 01 0x01
	__test_ecn_decap "0:1" 02 0x01
	__test_ecn_decap "0:1" 03 0x03
	__test_ecn_decap "0:2" 00 0x02
	__test_ecn_decap "0:2" 01 0x01
	__test_ecn_decap "0:2" 02 0x02
	__test_ecn_decap "0:2" 03 0x03
	__test_ecn_decap "0:3" 00 0x03
	__test_ecn_decap "0:3" 01 0x03
	__test_ecn_decap "0:3" 02 0x03
	__test_ecn_decap "0:3" 03 0x03
	test_ecn_decap_error
}

test_all()
{
	log_info "Running tests with UDP port $VXPORT"
	tests_run
}

trap cleanup EXIT

setup_prepare
setup_wait
test_all

exit $EXIT_STATUS
