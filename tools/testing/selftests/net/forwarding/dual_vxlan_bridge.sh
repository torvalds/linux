#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +--------------------+                               +----------------------+
# | H1 (vrf)           |                               |             H2 (vrf) |
# |    + h1.10         |                               |  + h2.20             |
# |    | 192.0.2.1/28  |                               |  | 192.0.2.2/28      |
# |    |               |                               |  |                   |
# |    + $h1           |                               |  + $h2               |
# |    |               |                               |  |                   |
# +----|---------------+                               +--|-------------------+
#      |                                                  |
# +----|--------------------------------------------------|--------------------+
# | SW |                                                  |                    |
# | +--|-------------------------------+ +----------------|------------------+ |
# | |  + $swp1         BR1 (802.1ad)   | | BR2 (802.1d)   + $swp2            | |
# | |    vid 100 pvid untagged         | |                |                  | |
# | |                                  | |                + $swp2.20         | |
# | |                                  | |                                   | |
# | |  + vx100 (vxlan)                 | |  + vx200 (vxlan)                  | |
# | |    local 192.0.2.17              | |    local 192.0.2.17               | |
# | |    remote 192.0.2.34             | |    remote 192.0.2.50              | |
# | |    id 1000 dstport $VXPORT       | |    id 2000 dstport $VXPORT        | |
# | |    vid 100 pvid untagged         | |                                   | |
# | +--------------------------------- + +-----------------------------------+ |
# |                                                                            |
# |  192.0.2.32/28 via 192.0.2.18                                              |
# |  192.0.2.48/28 via 192.0.2.18                                              |
# |                                                                            |
# |    + $rp1                                                                  |
# |    | 192.0.2.17/28                                                         |
# +----|-----------------------------------------------------------------------+
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
# | |                 BR3 (802.1ad) | |   | |                  BR3 (802.1d) | |
# | |  + vx100 (vxlan)              | |   | |  + vx200 (vxlan)              | |
# | |    local 192.0.2.34           | |   | |    local 192.0.2.50           | |
# | |    remote 192.0.2.17          | |   | |    remote 192.0.2.17          | |
# | |    remote 192.0.2.50          | |   | |    remote 192.0.2.34          | |
# | |    id 1000 dstport $VXPORT    | |   | |    id 2000 dstport $VXPORT    | |
# | |    vid 100 pvid untagged      | |   | |                               | |
# | |                               | |   | |  + w1.20                      | |
# | |                               | |   | |  |                            | |
# | |  + w1 (veth)                  | |   | |  + w1 (veth)                  | |
# | |  | vid 100 pvid untagged      | |   | |  |                            | |
# | +--|----------------------------+ |   | +--|----------------------------+ |
# |    |                              |   |    |                              |
# | +--|----------------------------+ |   | +--|----------------------------+ |
# | |  |                  VW2 (vrf) | |   | |  |                  VW2 (vrf) | |
# | |  + w2 (veth)                  | |   | |  + w2 (veth)                  | |
# | |  |                            | |   | |  |                            | |
# | |  |                            | |   | |  |                            | |
# | |  + w2.10                      | |   | |  + w2.20                      | |
# | |    192.0.2.3/28               | |   | |    192.0.2.4/28               | |
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
}

h1_destroy()
{
	vlan_destroy $h1 10
	tc qdisc del dev $h1 clsact
	simple_if_fini $h1
}

h2_create()
{
	simple_if_init $h2
	tc qdisc add dev $h2 clsact
	vlan_create $h2 20 v$h2 192.0.2.2/28
}

h2_destroy()
{
	vlan_destroy $h2 20
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
	#### BR1 ####
	ip link add name br1 type bridge vlan_filtering 1 \
		vlan_protocol 802.1ad vlan_default_pvid 0 mcast_snooping 0
	# Make sure the bridge uses the MAC address of the local port and not
	# that of the VxLAN's device.
	ip link set dev br1 address $(mac_get $swp1)
	ip link set dev br1 up

	#### BR2 ####
	ip link add name br2 type bridge vlan_filtering 0 mcast_snooping 0
	# Make sure the bridge uses the MAC address of the local port and not
	# that of the VxLAN's device.
	ip link set dev br2 address $(mac_get $swp2)
	ip link set dev br2 up

	ip link set dev $rp1 up
	rp1_set_addr

	#### VX100 ####
	ip link add name vx100 type vxlan id 1000 local 192.0.2.17 \
		dstport "$VXPORT" nolearning noudpcsum tos inherit ttl 100
	ip link set dev vx100 up

	ip link set dev vx100 master br1
	bridge vlan add vid 100 dev vx100 pvid untagged

	ip link set dev $swp1 master br1
	ip link set dev $swp1 up
	bridge vlan add vid 100 dev $swp1 pvid untagged

	#### VX200 ####
	ip link add name vx200 type vxlan id 2000 local 192.0.2.17 \
		dstport "$VXPORT" nolearning noudpcsum tos inherit ttl 100
	ip link set dev vx200 up

	ip link set dev vx200 master br2

	ip link set dev $swp2 up
	ip link add name $swp2.20 link $swp2 type vlan id 20
	ip link set dev $swp2.20 master br2
	ip link set dev $swp2.20 up

	bridge fdb append dev vx100 00:00:00:00:00:00 dst 192.0.2.34 self
	bridge fdb append dev vx200 00:00:00:00:00:00 dst 192.0.2.50 self
}

switch_destroy()
{
	bridge fdb del dev vx200 00:00:00:00:00:00 dst 192.0.2.50 self
	bridge fdb del dev vx100 00:00:00:00:00:00 dst 192.0.2.34 self

	ip link set dev vx200 nomaster
	ip link set dev vx200 down
	ip link del dev vx200

	ip link del dev $swp2.20
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

	ip link set dev br2 down
	ip link del dev br2

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
	local vxlan_name=$1; shift
	local vxlan_id=$1; shift
	local vlan_id=$1; shift
	local host_addr=$1; shift
	local nh_addr=$1; shift

	ip link set dev $in_if up
	ip address add dev $in_if $in_addr/28
	tc qdisc add dev $in_if clsact

	ip link add name br3 type bridge vlan_filtering 0
	ip link set dev br3 up

	ip link add name w1 type veth peer name w2

	ip link set dev w1 master br3
	ip link set dev w1 up

	ip link add name $vxlan_name type vxlan id $vxlan_id local $in_addr \
		dstport "$VXPORT"
	ip link set dev $vxlan_name up
	bridge fdb append dev $vxlan_name 00:00:00:00:00:00 dst 192.0.2.17 self
	bridge fdb append dev $vxlan_name 00:00:00:00:00:00 dst $other_in_addr self

	ip link set dev $vxlan_name master br3
	tc qdisc add dev $vxlan_name clsact

	simple_if_init w2
	vlan_create w2 $vlan_id vw2 $host_addr/28

	ip route add 192.0.2.16/28 nexthop via $nh_addr
	ip route add $other_in_addr/32 nexthop via $nh_addr
}
export -f ns_init_common

ns1_create()
{
	ip netns add ns1
	ip link set dev v2 netns ns1
	in_ns ns1 \
	      ns_init_common v2 192.0.2.34 192.0.2.50 vx100 1000 10 192.0.2.3 \
	      192.0.2.33

	in_ns ns1 bridge vlan add vid 100 dev vx100 pvid untagged
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
	      ns_init_common v4 192.0.2.50 192.0.2.34 vx200 2000 20 192.0.2.4 \
	      192.0.2.49

	in_ns ns2 ip link add name w1.20 link w1 type vlan id 20
	in_ns ns2 ip link set dev w1.20 master br3
	in_ns ns2 ip link set dev w1.20 up
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
	ping_test $h1 192.0.2.3 ": local->remote 1 through VxLAN with an 802.1ad bridge"
	ping_test $h2 192.0.2.4 ": local->remote 2 through VxLAN with an 802.1d bridge"
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
