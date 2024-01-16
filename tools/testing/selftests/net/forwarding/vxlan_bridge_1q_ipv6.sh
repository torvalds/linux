#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +-----------------------+                          +------------------------+
# | H1 (vrf)              |                          | H2 (vrf)               |
# | + $h1.10              |                          | + $h2.10               |
# | | 192.0.2.1/28        |                          | | 192.0.2.2/28         |
# | | 2001:db8:1::1/64    |                          | | 2001:db8:1::2/64     |
# | |                     |                          | |                      |
# | |  + $h1.20           |                          | |  + $h2.20            |
# | \  | 198.51.100.1/24  |                          | \  | 198.51.100.2/24   |
# |  \ | 2001:db8:2::1/64 |                          |  \ | 2001:db8:2::2/64  |
# |   \|                  |                          |   \|                   |
# |    + $h1              |                          |    + $h2               |
# +----|------------------+                          +----|-------------------+
#      |                                                  |
# +----|--------------------------------------------------|-------------------+
# | SW |                                                  |                   |
# | +--|--------------------------------------------------|-----------------+ |
# | |  + $swp1                   BR1 (802.1q)             + $swp2           | |
# | |     vid 10                                             vid 10         | |
# | |     vid 20                                             vid 20         | |
# | |                                                                       | |
# | |  + vx10 (vxlan)                        + vx20 (vxlan)                 | |
# | |    local:                                local:                       | |
# | |    2001:db8:3::1                         2001:db8:3::1                | |
# | |    remote:                               remote:                      | |
# | |    2001:db8:4::1 2001:db8:5::1           2001:db8:4::1 2001:db8:5::1  | |
# | |    id 1000 dstport $VXPORT               id 2000 dstport $VXPORT      | |
# | |    vid 10 pvid untagged                  vid 20 pvid untagged         | |
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
# | |                  BR2 (802.1q) |   | | |                  BR2 (802.1q) |  |
# | |  + vx10 (vxlan)               |   | | |  + vx10 (vxlan)               |  |
# | |    local 2001:db8:4::1        |   | | |    local 2001:db8:5::1        |  |
# | |    remote 2001:db8:3::1       |   | | |    remote 2001:db8:3::1       |  |
# | |    remote 2001:db8:5::1       |   | | |    remote 2001:db8:4::1       |  |
# | |    id 1000 dstport $VXPORT    |   | | |    id 1000 dstport $VXPORT    |  |
# | |    vid 10 pvid untagged       |   | | |    vid 10 pvid untagged       |  |
# | |                               |   | | |                               |  |
# | |  + vx20 (vxlan)               |   | | |  + vx20 (vxlan)               |  |
# | |    local 2001:db8:4::1        |   | | |    local 2001:db8:5::1        |  |
# | |    remote 2001:db8:3::1       |   | | |    remote 2001:db8:3::1       |  |
# | |    remote 2001:db8:5::1       |   | | |    remote 2001:db8:4::1       |  |
# | |    id 2000 dstport $VXPORT    |   | | |    id 2000 dstport $VXPORT    |  |
# | |    vid 20 pvid untagged       |   | | |    vid 20 pvid untagged       |  |
# | |                               |   | | |                               |  |
# | |  + w1 (veth)                  |   | | |  + w1 (veth)                  |  |
# | |  | vid 10                     |   | | |  | vid 10                     |  |
# | |  | vid 20                     |   | | |  | vid 20                     |  |
# | +--|----------------------------+   | | +--|----------------------------+  |
# |    |                                | |    |                               |
# | +--|----------------------------+   | | +--|----------------------------+  |
# | |  + w2 (veth)        VW2 (vrf) |   | | |  + w2 (veth)        VW2 (vrf) |  |
# | |  |\                           |   | | |  |\                           |  |
# | |  | + w2.10                    |   | | |  | + w2.10                    |  |
# | |  |   192.0.2.3/28             |   | | |  |   192.0.2.4/28             |  |
# | |  |   2001:db8:1::3/64         |   | | |  |   2001:db8:1::4/64         |  |
# | |  |                            |   | | |  |                            |  |
# | |  + w2.20                      |   | | |  + w2.20                      |  |
# | |    198.51.100.3/24            |   | | |    198.51.100.4/24            |  |
# | |    2001:db8:2::3/64           |   | | |    2001:db8:2::4/64           |  |
# | +-------------------------------+   | | +-------------------------------+  |
# +-------------------------------------+ +------------------------------------+

: ${VXPORT:=4789}
export VXPORT

: ${ALL_TESTS:="
	ping_ipv4
	ping_ipv6
	test_flood
	test_unicast
	reapply_config
	ping_ipv4
	ping_ipv6
	test_flood
	test_unicast
	test_pvid
	ping_ipv4
	ping_ipv6
	test_flood
	test_pvid
"}

NUM_NETIFS=6
source lib.sh
source tc_common.sh

h1_create()
{
	simple_if_init $h1
	tc qdisc add dev $h1 clsact
	vlan_create $h1 10 v$h1 192.0.2.1/28 2001:db8:1::1/64
	vlan_create $h1 20 v$h1 198.51.100.1/24 2001:db8:2::1/64
}

h1_destroy()
{
	vlan_destroy $h1 20
	vlan_destroy $h1 10
	tc qdisc del dev $h1 clsact
	simple_if_fini $h1
}

h2_create()
{
	simple_if_init $h2
	tc qdisc add dev $h2 clsact
	vlan_create $h2 10 v$h2 192.0.2.2/28 2001:db8:1::2/64
	vlan_create $h2 20 v$h2 198.51.100.2/24 2001:db8:2::2/64
}

h2_destroy()
{
	vlan_destroy $h2 20
	vlan_destroy $h2 10
	tc qdisc del dev $h2 clsact
	simple_if_fini $h2
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
	ip link add name br1 type bridge vlan_filtering 1 vlan_default_pvid 0 \
		mcast_snooping 0
	# Make sure the bridge uses the MAC address of the local port and not
	# that of the VxLAN's device.
	ip link set dev br1 address $(mac_get $swp1)
	ip link set dev br1 up

	ip link set dev $rp1 up
	rp1_set_addr
	tc qdisc add dev $rp1 clsact

	ip link add name vx10 type vxlan id 1000 local 2001:db8:3::1 \
		dstport "$VXPORT" nolearning udp6zerocsumrx udp6zerocsumtx \
		tos inherit ttl 100
	ip link set dev vx10 up

	ip link set dev vx10 master br1
	bridge vlan add vid 10 dev vx10 pvid untagged

	ip link add name vx20 type vxlan id 2000 local 2001:db8:3::1 \
		dstport "$VXPORT" nolearning udp6zerocsumrx udp6zerocsumtx \
		tos inherit ttl 100
	ip link set dev vx20 up

	ip link set dev vx20 master br1
	bridge vlan add vid 20 dev vx20 pvid untagged

	ip link set dev $swp1 master br1
	ip link set dev $swp1 up
	tc qdisc add dev $swp1 clsact
	bridge vlan add vid 10 dev $swp1
	bridge vlan add vid 20 dev $swp1

	ip link set dev $swp2 master br1
	ip link set dev $swp2 up
	bridge vlan add vid 10 dev $swp2
	bridge vlan add vid 20 dev $swp2

	bridge fdb append dev vx10 00:00:00:00:00:00 dst 2001:db8:4::1 self
	bridge fdb append dev vx10 00:00:00:00:00:00 dst 2001:db8:5::1 self

	bridge fdb append dev vx20 00:00:00:00:00:00 dst 2001:db8:4::1 self
	bridge fdb append dev vx20 00:00:00:00:00:00 dst 2001:db8:5::1 self
}

switch_destroy()
{
	bridge fdb del dev vx20 00:00:00:00:00:00 dst 2001:db8:5::1 self
	bridge fdb del dev vx20 00:00:00:00:00:00 dst 2001:db8:4::1 self

	bridge fdb del dev vx10 00:00:00:00:00:00 dst 2001:db8:5::1 self
	bridge fdb del dev vx10 00:00:00:00:00:00 dst 2001:db8:4::1 self

	bridge vlan del vid 20 dev $swp2
	bridge vlan del vid 10 dev $swp2
	ip link set dev $swp2 down
	ip link set dev $swp2 nomaster

	bridge vlan del vid 20 dev $swp1
	bridge vlan del vid 10 dev $swp1
	tc qdisc del dev $swp1 clsact
	ip link set dev $swp1 down
	ip link set dev $swp1 nomaster

	bridge vlan del vid 20 dev vx20
	ip link set dev vx20 nomaster

	ip link set dev vx20 down
	ip link del dev vx20

	bridge vlan del vid 10 dev vx10
	ip link set dev vx10 nomaster

	ip link set dev vx10 down
	ip link del dev vx10

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
	local host_addr1_ipv4=$1; shift
	local host_addr1_ipv6=$1; shift
	local host_addr2_ipv4=$1; shift
	local host_addr2_ipv6=$1; shift

	ip link set dev $in_if up
	ip address add dev $in_if $in_addr/64
	tc qdisc add dev $in_if clsact

	ip link add name br2 type bridge vlan_filtering 1 vlan_default_pvid 0
	ip link set dev br2 up

	ip link add name w1 type veth peer name w2

	ip link set dev w1 master br2
	ip link set dev w1 up

	bridge vlan add vid 10 dev w1
	bridge vlan add vid 20 dev w1

	ip link add name vx10 type vxlan id 1000 local $in_addr \
		dstport "$VXPORT" udp6zerocsumrx
	ip link set dev vx10 up
	bridge fdb append dev vx10 00:00:00:00:00:00 dst 2001:db8:3::1 self
	bridge fdb append dev vx10 00:00:00:00:00:00 dst $other_in_addr self

	ip link set dev vx10 master br2
	tc qdisc add dev vx10 clsact

	bridge vlan add vid 10 dev vx10 pvid untagged

	ip link add name vx20 type vxlan id 2000 local $in_addr \
		dstport "$VXPORT" udp6zerocsumrx
	ip link set dev vx20 up
	bridge fdb append dev vx20 00:00:00:00:00:00 dst 2001:db8:3::1 self
	bridge fdb append dev vx20 00:00:00:00:00:00 dst $other_in_addr self

	ip link set dev vx20 master br2
	tc qdisc add dev vx20 clsact

	bridge vlan add vid 20 dev vx20 pvid untagged

	simple_if_init w2
        vlan_create w2 10 vw2 $host_addr1_ipv4/28 $host_addr1_ipv6/64
        vlan_create w2 20 vw2 $host_addr2_ipv4/24 $host_addr2_ipv6/64

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
	      192.0.2.3 2001:db8:1::3 198.51.100.3 2001:db8:2::3
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
	      192.0.2.4 2001:db8:1::4 198.51.100.4 2001:db8:2::4
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

# For the first round of tests, vx10 and vx20 were the first devices to get
# attached to the bridge, and at that point the local IP is already
# configured. Try the other scenario of attaching these devices to a bridge
# that already has local ports members, and only then assign the local IP.
reapply_config()
{
	log_info "Reapplying configuration"

	bridge fdb del dev vx20 00:00:00:00:00:00 dst 2001:db8:5::1 self
	bridge fdb del dev vx20 00:00:00:00:00:00 dst 2001:db8:4::1 self

	bridge fdb del dev vx10 00:00:00:00:00:00 dst 2001:db8:5::1 self
	bridge fdb del dev vx10 00:00:00:00:00:00 dst 2001:db8:4::1 self

	ip link set dev vx20 nomaster
	ip link set dev vx10 nomaster

	rp1_unset_addr
	sleep 5

	ip link set dev vx10 master br1
	bridge vlan add vid 10 dev vx10 pvid untagged

	ip link set dev vx20 master br1
	bridge vlan add vid 20 dev vx20 pvid untagged

	bridge fdb append dev vx10 00:00:00:00:00:00 dst 2001:db8:4::1 self
	bridge fdb append dev vx10 00:00:00:00:00:00 dst 2001:db8:5::1 self

	bridge fdb append dev vx20 00:00:00:00:00:00 dst 2001:db8:4::1 self
	bridge fdb append dev vx20 00:00:00:00:00:00 dst 2001:db8:5::1 self

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
	tc filter add dev $swp1 egress protocol 802.1q pref 1 handle 101 \
		flower vlan_ethtype ipv4 src_ip $dst_ip dst_ip $src_ip \
		$TC_FLAG action pass

	# Send 100 packets and verify that at least 100 packets hit the rule,
	# to overcome ARP noise.
	PING_COUNT=100 PING_TIMEOUT=11 ping_do $dev $dst_ip
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
	local h1_10_ip=192.0.2.1
	local h1_20_ip=198.51.100.1
	local w2_10_ns1_ip=192.0.2.3
	local w2_10_ns2_ip=192.0.2.4
	local w2_20_ns1_ip=198.51.100.3
	local w2_20_ns2_ip=198.51.100.4

	ping_test $h1.10 192.0.2.2 ": local->local vid 10"
	ping_test $h1.20 198.51.100.2 ": local->local vid 20"

	__ping_ipv4 $local_sw_ip $remote_ns1_ip $h1_10_ip $w2_10_ns1_ip $h1.10 \
		"local->remote 1 vid 10"
	__ping_ipv4 $local_sw_ip $remote_ns2_ip $h1_10_ip $w2_10_ns2_ip $h1.10 \
		"local->remote 2 vid 10"
	__ping_ipv4 $local_sw_ip $remote_ns1_ip $h1_20_ip $w2_20_ns1_ip $h1.20 \
		"local->remote 1 vid 20"
	__ping_ipv4 $local_sw_ip $remote_ns2_ip $h1_20_ip $w2_20_ns2_ip $h1.20 \
		"local->remote 2 vid 20"
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
	tc filter add dev $swp1 egress protocol 802.1q pref 1 handle 101 \
		flower vlan_ethtype ipv6 src_ip $dst_ip dst_ip $src_ip \
		$TC_FLAG action pass

	# Send 100 packets and verify that at least 100 packets hit the rule,
	# to overcome neighbor discovery noise.
	PING_COUNT=100 PING_TIMEOUT=11 ping6_do $dev $dst_ip
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
	local h1_10_ip=2001:db8:1::1
	local h1_20_ip=2001:db8:2::1
	local w2_10_ns1_ip=2001:db8:1::3
	local w2_10_ns2_ip=2001:db8:1::4
	local w2_20_ns1_ip=2001:db8:2::3
	local w2_20_ns2_ip=2001:db8:2::4

	ping6_test $h1.10 2001:db8:1::2 ": local->local vid 10"
	ping6_test $h1.20 2001:db8:2::2 ": local->local vid 20"

	__ping_ipv6 $local_sw_ip $remote_ns1_ip $h1_10_ip $w2_10_ns1_ip $h1.10 \
		"local->remote 1 vid 10"
	__ping_ipv6 $local_sw_ip $remote_ns2_ip $h1_10_ip $w2_10_ns2_ip $h1.10 \
		"local->remote 2 vid 10"
	__ping_ipv6 $local_sw_ip $remote_ns1_ip $h1_20_ip $w2_20_ns1_ip $h1.20 \
		"local->remote 1 vid 20"
	__ping_ipv6 $local_sw_ip $remote_ns2_ip $h1_20_ip $w2_20_ns2_ip $h1.20 \
		"local->remote 2 vid 20"
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
	local vid=$1; shift
	local -a expects=("${@}")

	local -a counters=($h2 "vx10 ns1" "vx20 ns1" "vx10 ns2" "vx20 ns2")
	local counter
	local key

	# Packets reach the local host tagged whereas they reach the VxLAN
	# devices untagged. In order to be able to use the same filter for
	# all counters, make sure the packets also reach the local host
	# untagged
	bridge vlan add vid $vid dev $swp2 untagged
	for counter in "${counters[@]}"; do
		flood_counter_install $dst $counter
	done

	local -a t0s=($(flood_fetch_stats "${counters[@]}"))
	$MZ -6 $h1 -Q $vid -c 10 -d 100msec -p 64 -b $mac -B $dst -t icmp6 type=128 -q
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
	bridge vlan add vid $vid dev $swp2
}

__test_flood()
{
	local mac=$1; shift
	local dst=$1; shift
	local vid=$1; shift
	local what=$1; shift
	local -a expects=("${@}")

	RET=0

	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	log_test "VXLAN: $what"
}

test_flood()
{
	__test_flood de:ad:be:ef:13:37 2001:db8:1::100 10 "flood vlan 10" \
		10 10 0 10 0
	__test_flood ca:fe:be:ef:13:37 2001:db8:2::100 20 "flood vlan 20" \
		10 0 10 0 10
}

vxlan_fdb_add_del()
{
	local add_del=$1; shift
	local vid=$1; shift
	local mac=$1; shift
	local dev=$1; shift
	local dst=$1; shift

	bridge fdb $add_del dev $dev $mac self static permanent \
		${dst:+dst} $dst 2>/dev/null
	bridge fdb $add_del dev $dev $mac master static vlan $vid 2>/dev/null
}

__test_unicast()
{
	local mac=$1; shift
	local dst=$1; shift
	local hit_idx=$1; shift
	local vid=$1; shift
	local what=$1; shift

	RET=0

	local -a expects=(0 0 0 0 0)
	expects[$hit_idx]=10

	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	log_test "VXLAN: $what"
}

test_unicast()
{
	local -a targets=("$h2_mac $h2"
			  "$r1_mac vx10 2001:db8:4::1"
			  "$r2_mac vx10 2001:db8:5::1")
	local target

	log_info "unicast vlan 10"

	for target in "${targets[@]}"; do
		vxlan_fdb_add_del add 10 $target
	done

	__test_unicast $h2_mac 2001:db8:1::2 0 10 "local MAC unicast"
	__test_unicast $r1_mac 2001:db8:1::3 1 10 "remote MAC 1 unicast"
	__test_unicast $r2_mac 2001:db8:1::4 3 10 "remote MAC 2 unicast"

	for target in "${targets[@]}"; do
		vxlan_fdb_add_del del 10 $target
	done

	log_info "unicast vlan 20"

	targets=("$h2_mac $h2" "$r1_mac vx20 2001:db8:4::1" \
		 "$r2_mac vx20 2001:db8:5::1")

	for target in "${targets[@]}"; do
		vxlan_fdb_add_del add 20 $target
	done

	__test_unicast $h2_mac 2001:db8:2::2 0 20 "local MAC unicast"
	__test_unicast $r1_mac 2001:db8:2::3 2 20 "remote MAC 1 unicast"
	__test_unicast $r2_mac 2001:db8:2::4 4 20 "remote MAC 2 unicast"

	for target in "${targets[@]}"; do
		vxlan_fdb_add_del del 20 $target
	done
}

test_pvid()
{
	local -a expects=(0 0 0 0 0)
	local mac=de:ad:be:ef:13:37
	local dst=2001:db8:1::100
	local vid=10

	# Check that flooding works
	RET=0

	expects[0]=10; expects[1]=10; expects[3]=10
	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	log_test "VXLAN: flood before pvid off"

	# Toggle PVID off and test that flood to remote hosts does not work
	RET=0

	bridge vlan add vid 10 dev vx10

	expects[0]=10; expects[1]=0; expects[3]=0
	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	log_test "VXLAN: flood after pvid off"

	# Toggle PVID on and test that flood to remote hosts does work
	RET=0

	bridge vlan add vid 10 dev vx10 pvid untagged

	expects[0]=10; expects[1]=10; expects[3]=10
	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	log_test "VXLAN: flood after pvid on"

	# Add a new VLAN and test that it does not affect flooding
	RET=0

	bridge vlan add vid 30 dev vx10

	expects[0]=10; expects[1]=10; expects[3]=10
	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	bridge vlan del vid 30 dev vx10

	log_test "VXLAN: flood after vlan add"

	# Remove currently mapped VLAN and test that flood to remote hosts does
	# not work
	RET=0

	bridge vlan del vid 10 dev vx10

	expects[0]=10; expects[1]=0; expects[3]=0
	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	log_test "VXLAN: flood after vlan delete"

	# Re-add the VLAN and test that flood to remote hosts does work
	RET=0

	bridge vlan add vid 10 dev vx10 pvid untagged

	expects[0]=10; expects[1]=10; expects[3]=10
	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	log_test "VXLAN: flood after vlan re-add"
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
