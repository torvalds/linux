#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +-----------------------+                          +------------------------+
# | H1 (vrf)              |                          | H2 (vrf)               |
# |  + $h1.10             |                          |  + $h2.10              |
# |  | 192.0.2.1/28       |                          |  | 192.0.2.2/28        |
# |  |                    |                          |  |                     |
# |  | + $h1.20           |                          |  | + $h2.20            |
# |  \ | 198.51.100.1/24  |                          |  \ | 198.51.100.2/24   |
# |   \|                  |                          |   \|                   |
# |    + $h1              |                          |    + $h2               |
# +----|------------------+                          +----|-------------------+
#      |                                                  |
# +----|--------------------------------------------------|-------------------+
# | SW |                                                  |                   |
# | +--|--------------------------------------------------|-----------------+ |
# | |  + $swp1                   BR1 (802.1ad)            + $swp2           | |
# | |    vid 100 pvid untagged                              vid 100 pvid    | |
# | |                                                           untagged    | |
# | |  + vx100 (vxlan)                                                      | |
# | |    local 192.0.2.17                                                   | |
# | |    remote 192.0.2.34 192.0.2.50                                       | |
# | |    id 1000 dstport $VXPORT                                            | |
# | |    vid 100 pvid untagged                                              | |
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
# | |                 BR2 (802.1ad) | |   | |                 BR2 (802.1ad) | |
# | |  + vx100 (vxlan)              | |   | |  + vx100 (vxlan)              | |
# | |    local 192.0.2.34           | |   | |    local 192.0.2.50           | |
# | |    remote 192.0.2.17          | |   | |    remote 192.0.2.17          | |
# | |    remote 192.0.2.50          | |   | |    remote 192.0.2.34          | |
# | |    id 1000 dstport $VXPORT    | |   | |    id 1000 dstport $VXPORT    | |
# | |    vid 100 pvid untagged      | |   | |    vid 100 pvid untagged      | |
# | |                               | |   | |                               | |
# | |  + w1 (veth)                  | |   | |  + w1 (veth)                  | |
# | |  | vid 100 pvid untagged      | |   | |  | vid 100 pvid untagged      | |
# | +--|----------------------------+ |   | +--|----------------------------+ |
# |    |                              |   |    |                              |
# | +--|----------------------------+ |   | +--|----------------------------+ |
# | |  |                  VW2 (vrf) | |   | |  |                  VW2 (vrf) | |
# | |  + w2 (veth)                  | |   | |  + w2 (veth)                  | |
# | |  |\                           | |   | |  |\                           | |
# | |  | + w2.10                    | |   | |  | + w2.10                    | |
# | |  |   192.0.2.3/28             | |   | |  |   192.0.2.4/28             | |
# | |  |                            | |   | |  |                            | |
# | |  + w2.20                      | |   | |  + w2.20                      | |
# | |    198.51.100.3/24            | |   | |    198.51.100.4/24            | |
# | +-------------------------------+ |   | +-------------------------------+ |
# +-----------------------------------+   +-----------------------------------+

: ${VXPORT:=4789}
export VXPORT

: ${ALL_TESTS:="
	ping_ipv4
    "}

NUM_NETIFS=6
source lib.sh

h1_create()
{
	simple_if_init $h1
	tc qdisc add dev $h1 clsact
	vlan_create $h1 10 v$h1 192.0.2.1/28
	vlan_create $h1 20 v$h1 198.51.100.1/24
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
	vlan_create $h2 10 v$h2 192.0.2.2/28
	vlan_create $h2 20 v$h2 198.51.100.2/24
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
	ip link add name br1 type bridge vlan_filtering 1 vlan_protocol 802.1ad \
		vlan_default_pvid 0 mcast_snooping 0
	# Make sure the bridge uses the MAC address of the local port and not
	# that of the VxLAN's device.
	ip link set dev br1 address $(mac_get $swp1)
	ip link set dev br1 up

	ip link set dev $rp1 up
	rp1_set_addr

	ip link add name vx100 type vxlan id 1000		\
		local 192.0.2.17 dstport "$VXPORT"	\
		nolearning noudpcsum tos inherit ttl 100
	ip link set dev vx100 up

	ip link set dev vx100 master br1
	bridge vlan add vid 100 dev vx100 pvid untagged

	ip link set dev $swp1 master br1
	ip link set dev $swp1 up
	bridge vlan add vid 100 dev $swp1 pvid untagged

	ip link set dev $swp2 master br1
	ip link set dev $swp2 up
	bridge vlan add vid 100 dev $swp2 pvid untagged

	bridge fdb append dev vx100 00:00:00:00:00:00 dst 192.0.2.34 self
	bridge fdb append dev vx100 00:00:00:00:00:00 dst 192.0.2.50 self
}

switch_destroy()
{
	bridge fdb del dev vx100 00:00:00:00:00:00 dst 192.0.2.50 self
	bridge fdb del dev vx100 00:00:00:00:00:00 dst 192.0.2.34 self

	bridge vlan del vid 100 dev $swp2
	ip link set dev $swp2 down
	ip link set dev $swp2 nomaster

	bridge vlan del vid 100 dev $swp1
	ip link set dev $swp1 down
	ip link set dev $swp1 nomaster

	ip link set dev vx100 nomaster
	ip link set dev vx100 down
	ip link del dev vx100

	rp1_unset_addr
	ip link set dev $rp1 down

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
	local host_addr1=$1; shift
	local host_addr2=$1; shift

	ip link set dev $in_if up
	ip address add dev $in_if $in_addr/28
	tc qdisc add dev $in_if clsact

	ip link add name br2 type bridge vlan_filtering 1 vlan_protocol 802.1ad \
		vlan_default_pvid 0
	ip link set dev br2 up

	ip link add name w1 type veth peer name w2

	ip link set dev w1 master br2
	ip link set dev w1 up
	bridge vlan add vid 100 dev w1 pvid untagged

	ip link add name vx100 type vxlan id 1000 local $in_addr \
		dstport "$VXPORT"
	ip link set dev vx100 up
	bridge fdb append dev vx100 00:00:00:00:00:00 dst 192.0.2.17 self
	bridge fdb append dev vx100 00:00:00:00:00:00 dst $other_in_addr self

	ip link set dev vx100 master br2
	tc qdisc add dev vx100 clsact

	bridge vlan add vid 100 dev vx100 pvid untagged

	simple_if_init w2
        vlan_create w2 10 vw2 $host_addr1/28
        vlan_create w2 20 vw2 $host_addr2/24

	ip route add 192.0.2.16/28 nexthop via $nh_addr
	ip route add $other_in_addr/32 nexthop via $nh_addr
}
export -f ns_init_common

ns1_create()
{
	ip netns add ns1
	ip link set dev v2 netns ns1
	in_ns ns1 \
	      ns_init_common v2 192.0.2.34 192.0.2.50 192.0.2.33 \
			     192.0.2.3 198.51.100.3
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
	      ns_init_common v4 192.0.2.50 192.0.2.34 192.0.2.49 \
			     192.0.2.4 198.51.100.4
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

ping_ipv4()
{
	ping_test $h1 192.0.2.2 ": local->local"
	ping_test $h1 192.0.2.3 ": local->remote 1"
	ping_test $h1 192.0.2.4 ": local->remote 2"
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
