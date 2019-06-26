#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +---------------------------+                +------------------------------+
# |                    vrf-h1 |                |                       vrf-h2 |
# |    + $h1                  |                |    + $h2                     |
# |    | 10.1.1.101/24        |                |    | 10.1.2.101/24           |
# |    | default via 10.1.1.1 |                |    | default via 10.1.2.1    |
# +----|----------------------+                +----|-------------------------+
#      |                                            |
# +----|--------------------------------------------|-------------------------+
# | SW |                                            |                         |
# | +--|--------------------------------------------|-----------------------+ |
# | |  + $swp1                         br1          + $swp2                 | |
# | |     vid 10 pvid untagged                         vid 20 pvid untagged | |
# | |                                                                       | |
# | |  + vx10                                       + vx20                  | |
# | |    local 10.0.0.1                               local 10.0.0.1        | |
# | |    remote 10.0.0.2                              remote 10.0.0.2       | |
# | |    id 1000                                      id 2000               | |
# | |    dstport 4789                                 dstport 4789          | |
# | |    vid 10 pvid untagged                         vid 20 pvid untagged  | |
# | |                                                                       | |
# | +-----------------------------------+-----------------------------------+ |
# |                                     |                                     |
# | +-----------------------------------|-----------------------------------+ |
# | |                                   |                                   | |
# | |  +--------------------------------+--------------------------------+  | |
# | |  |                                                                 |  | |
# | |  + vlan10                                                   vlan20 +  | |
# | |  | 10.1.1.11/24                                       10.1.2.11/24 |  | |
# | |  |                                                                 |  | |
# | |  + vlan10-v (macvlan)                           vlan20-v (macvlan) +  | |
# | |    10.1.1.1/24                                         10.1.2.1/24    | |
# | |    00:00:5e:00:01:01                             00:00:5e:00:01:01    | |
# | |                               vrf-green                               | |
# | +-----------------------------------------------------------------------+ |
# |                                                                           |
# |    + $rp1                                       +lo                       |
# |    | 192.0.2.1/24                                10.0.0.1/32              |
# +----|----------------------------------------------------------------------+
#      |
# +----|--------------------------------------------------------+
# |    |                            vrf-spine                   |
# |    + $rp2                                                   |
# |      192.0.2.2/24                                           |
# |                                                             |   (maybe) HW
# =============================================================================
# |                                                             |  (likely) SW
# |                                                             |
# |    + v1 (veth)                                              |
# |    | 192.0.3.2/24                                           |
# +----|--------------------------------------------------------+
#      |
# +----|----------------------------------------------------------------------+
# |    + v2 (veth)                                  +lo           NS1 (netns) |
# |      192.0.3.1/24                                10.0.0.2/32              |
# |                                                                           |
# | +-----------------------------------------------------------------------+ |
# | |                               vrf-green                               | |
# | |  + vlan10-v (macvlan)                           vlan20-v (macvlan) +  | |
# | |  | 10.1.1.1/24                                         10.1.2.1/24 |  | |
# | |  | 00:00:5e:00:01:01                             00:00:5e:00:01:01 |  | |
# | |  |                                                                 |  | |
# | |  + vlan10                                                   vlan20 +  | |
# | |  | 10.1.1.12/24                                       10.1.2.12/24 |  | |
# | |  |                                                                 |  | |
# | |  +--------------------------------+--------------------------------+  | |
# | |                                   |                                   | |
# | +-----------------------------------|-----------------------------------+ |
# |                                     |                                     |
# | +-----------------------------------+-----------------------------------+ |
# | |                                                                       | |
# | |  + vx10                                     + vx20                    | |
# | |    local 10.0.0.2                             local 10.0.0.2          | |
# | |    remote 10.0.0.1                            remote 10.0.0.1         | |
# | |    id 1000                                    id 2000                 | |
# | |    dstport 4789                               dstport 4789            | |
# | |    vid 10 pvid untagged                       vid 20 pvid untagged    | |
# | |                                                                       | |
# | |  + w1 (veth)                                + w3 (veth)               | |
# | |  | vid 10 pvid untagged          br1        | vid 20 pvid untagged    | |
# | +--|------------------------------------------|-------------------------+ |
# |    |                                          |                           |
# |    |                                          |                           |
# | +--|----------------------+                +--|-------------------------+ |
# | |  |               vrf-h1 |                |  |                  vrf-h2 | |
# | |  + w2 (veth)            |                |  + w4 (veth)               | |
# | |    10.1.1.102/24        |                |    10.1.2.102/24           | |
# | |    default via 10.1.1.1 |                |    default via 10.1.2.1    | |
# | +-------------------------+                +----------------------------+ |
# +---------------------------------------------------------------------------+

ALL_TESTS="
	ping_ipv4
	arp_decap
	arp_suppression
"
NUM_NETIFS=6
source lib.sh

require_command $ARPING

hx_create()
{
	local vrf_name=$1; shift
	local if_name=$1; shift
	local ip_addr=$1; shift
	local gw_ip=$1; shift

	vrf_create $vrf_name
	ip link set dev $if_name master $vrf_name
	ip link set dev $vrf_name up
	ip link set dev $if_name up

	ip address add $ip_addr/24 dev $if_name
	ip neigh replace $gw_ip lladdr 00:00:5e:00:01:01 nud permanent \
		dev $if_name
	ip route add default vrf $vrf_name nexthop via $gw_ip
}
export -f hx_create

hx_destroy()
{
	local vrf_name=$1; shift
	local if_name=$1; shift
	local ip_addr=$1; shift
	local gw_ip=$1; shift

	ip route del default vrf $vrf_name nexthop via $gw_ip
	ip neigh del $gw_ip dev $if_name
	ip address del $ip_addr/24 dev $if_name

	ip link set dev $if_name down
	vrf_destroy $vrf_name
}

h1_create()
{
	hx_create "vrf-h1" $h1 10.1.1.101 10.1.1.1
}

h1_destroy()
{
	hx_destroy "vrf-h1" $h1 10.1.1.101 10.1.1.1
}

h2_create()
{
	hx_create "vrf-h2" $h2 10.1.2.101 10.1.2.1
}

h2_destroy()
{
	hx_destroy "vrf-h2" $h2 10.1.2.101 10.1.2.1
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
	ip address add dev $rp1 192.0.2.1/24
	ip route add 10.0.0.2/32 nexthop via 192.0.2.2

	ip link add name vx10 type vxlan id 1000		\
		local 10.0.0.1 remote 10.0.0.2 dstport 4789	\
		nolearning noudpcsum tos inherit ttl 100
	ip link set dev vx10 up

	ip link set dev vx10 master br1
	bridge vlan add vid 10 dev vx10 pvid untagged

	ip link add name vx20 type vxlan id 2000		\
		local 10.0.0.1 remote 10.0.0.2 dstport 4789	\
		nolearning noudpcsum tos inherit ttl 100
	ip link set dev vx20 up

	ip link set dev vx20 master br1
	bridge vlan add vid 20 dev vx20 pvid untagged

	ip link set dev $swp1 master br1
	ip link set dev $swp1 up
	bridge vlan add vid 10 dev $swp1 pvid untagged

	ip link set dev $swp2 master br1
	ip link set dev $swp2 up
	bridge vlan add vid 20 dev $swp2 pvid untagged

	ip address add 10.0.0.1/32 dev lo

	# Create SVIs
	vrf_create "vrf-green"
	ip link set dev vrf-green up

	ip link add link br1 name vlan10 up master vrf-green type vlan id 10
	ip address add 10.1.1.11/24 dev vlan10
	ip link add link vlan10 name vlan10-v up master vrf-green \
		address 00:00:5e:00:01:01 type macvlan mode private
	ip address add 10.1.1.1/24 dev vlan10-v

	ip link add link br1 name vlan20 up master vrf-green type vlan id 20
	ip address add 10.1.2.11/24 dev vlan20
	ip link add link vlan20 name vlan20-v up master vrf-green \
		address 00:00:5e:00:01:01 type macvlan mode private
	ip address add 10.1.2.1/24 dev vlan20-v

	bridge vlan add vid 10 dev br1 self
	bridge vlan add vid 20 dev br1 self

	bridge fdb add 00:00:5e:00:01:01 dev br1 self local vlan 10
	bridge fdb add 00:00:5e:00:01:01 dev br1 self local vlan 20
}

switch_destroy()
{
	bridge fdb del 00:00:5e:00:01:01 dev br1 self local vlan 20
	bridge fdb del 00:00:5e:00:01:01 dev br1 self local vlan 10

	bridge vlan del vid 20 dev br1 self
	bridge vlan del vid 10 dev br1 self

	ip link del dev vlan20

	ip link del dev vlan10

	vrf_destroy "vrf-green"

	ip address del 10.0.0.1/32 dev lo

	bridge vlan del vid 20 dev $swp2
	ip link set dev $swp2 down
	ip link set dev $swp2 nomaster

	bridge vlan del vid 10 dev $swp1
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

	ip route del 10.0.0.2/32 nexthop via 192.0.2.2
	ip address del dev $rp1 192.0.2.1/24
	ip link set dev $rp1 down

	ip link set dev br1 down
	ip link del dev br1
}

spine_create()
{
	vrf_create "vrf-spine"
	ip link set dev $rp2 master vrf-spine
	ip link set dev v1 master vrf-spine
	ip link set dev vrf-spine up
	ip link set dev $rp2 up
	ip link set dev v1 up

	ip address add 192.0.2.2/24 dev $rp2
	ip address add 192.0.3.2/24 dev v1

	ip route add 10.0.0.1/32 vrf vrf-spine nexthop via 192.0.2.1
	ip route add 10.0.0.2/32 vrf vrf-spine nexthop via 192.0.3.1
}

spine_destroy()
{
	ip route del 10.0.0.2/32 vrf vrf-spine nexthop via 192.0.3.1
	ip route del 10.0.0.1/32 vrf vrf-spine nexthop via 192.0.2.1

	ip address del 192.0.3.2/24 dev v1
	ip address del 192.0.2.2/24 dev $rp2

	ip link set dev v1 down
	ip link set dev $rp2 down
	vrf_destroy "vrf-spine"
}

ns_h1_create()
{
	hx_create "vrf-h1" w2 10.1.1.102 10.1.1.1
}
export -f ns_h1_create

ns_h2_create()
{
	hx_create "vrf-h2" w4 10.1.2.102 10.1.2.1
}
export -f ns_h2_create

ns_switch_create()
{
	ip link add name br1 type bridge vlan_filtering 1 vlan_default_pvid 0 \
		mcast_snooping 0
	ip link set dev br1 up

	ip link set dev v2 up
	ip address add dev v2 192.0.3.1/24
	ip route add 10.0.0.1/32 nexthop via 192.0.3.2

	ip link add name vx10 type vxlan id 1000		\
		local 10.0.0.2 remote 10.0.0.1 dstport 4789	\
		nolearning noudpcsum tos inherit ttl 100
	ip link set dev vx10 up

	ip link set dev vx10 master br1
	bridge vlan add vid 10 dev vx10 pvid untagged

	ip link add name vx20 type vxlan id 2000		\
		local 10.0.0.2 remote 10.0.0.1 dstport 4789	\
		nolearning noudpcsum tos inherit ttl 100
	ip link set dev vx20 up

	ip link set dev vx20 master br1
	bridge vlan add vid 20 dev vx20 pvid untagged

	ip link set dev w1 master br1
	ip link set dev w1 up
	bridge vlan add vid 10 dev w1 pvid untagged

	ip link set dev w3 master br1
	ip link set dev w3 up
	bridge vlan add vid 20 dev w3 pvid untagged

	ip address add 10.0.0.2/32 dev lo

	# Create SVIs
	vrf_create "vrf-green"
	ip link set dev vrf-green up

	ip link add link br1 name vlan10 up master vrf-green type vlan id 10
	ip address add 10.1.1.12/24 dev vlan10
	ip link add link vlan10 name vlan10-v up master vrf-green \
		address 00:00:5e:00:01:01 type macvlan mode private
	ip address add 10.1.1.1/24 dev vlan10-v

	ip link add link br1 name vlan20 up master vrf-green type vlan id 20
	ip address add 10.1.2.12/24 dev vlan20
	ip link add link vlan20 name vlan20-v up master vrf-green \
		address 00:00:5e:00:01:01 type macvlan mode private
	ip address add 10.1.2.1/24 dev vlan20-v

	bridge vlan add vid 10 dev br1 self
	bridge vlan add vid 20 dev br1 self

	bridge fdb add 00:00:5e:00:01:01 dev br1 self local vlan 10
	bridge fdb add 00:00:5e:00:01:01 dev br1 self local vlan 20
}
export -f ns_switch_create

ns_init()
{
	ip link add name w1 type veth peer name w2
	ip link add name w3 type veth peer name w4

	ip link set dev lo up

	ns_h1_create
	ns_h2_create
	ns_switch_create
}
export -f ns_init

ns1_create()
{
	ip netns add ns1
	ip link set dev v2 netns ns1
	in_ns ns1 ns_init
}

ns1_destroy()
{
	ip netns exec ns1 ip link set dev v2 netns 1
	ip netns del ns1
}

macs_populate()
{
	local mac1=$1; shift
	local mac2=$1; shift
	local ip1=$1; shift
	local ip2=$1; shift
	local dst=$1; shift

	bridge fdb add $mac1 dev vx10 self master extern_learn static \
		dst $dst vlan 10
	bridge fdb add $mac2 dev vx20 self master extern_learn static \
		dst $dst vlan 20

	ip neigh add $ip1 lladdr $mac1 nud noarp dev vlan10 \
		extern_learn
	ip neigh add $ip2 lladdr $mac2 nud noarp dev vlan20 \
		extern_learn
}
export -f macs_populate

macs_initialize()
{
	local h1_ns_mac=$(in_ns ns1 mac_get w2)
	local h2_ns_mac=$(in_ns ns1 mac_get w4)
	local h1_mac=$(mac_get $h1)
	local h2_mac=$(mac_get $h2)

	macs_populate $h1_ns_mac $h2_ns_mac 10.1.1.102 10.1.2.102 10.0.0.2
	in_ns ns1 macs_populate $h1_mac $h2_mac 10.1.1.101 10.1.2.101 10.0.0.1
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
	spine_create
	ns1_create

	macs_initialize
}

cleanup()
{
	pre_cleanup

	ns1_destroy
	spine_destroy
	ip link del dev v1

	switch_destroy
	h2_destroy
	h1_destroy

	forwarding_restore
	vrf_cleanup
}

ping_ipv4()
{
	ping_test $h1 10.1.2.101 ": local->local vid 10->vid 20"
	ping_test $h1 10.1.1.102 ": local->remote vid 10->vid 10"
	ping_test $h2 10.1.2.102 ": local->remote vid 20->vid 20"
	ping_test $h1 10.1.2.102 ": local->remote vid 10->vid 20"
	ping_test $h2 10.1.1.102 ": local->remote vid 20->vid 10"
}

arp_decap()
{
	# Repeat the ping tests, but without populating the neighbours. This
	# makes sure we correctly decapsulate ARP packets
	log_info "deleting neighbours from vlan interfaces"

	ip neigh del 10.1.1.102 dev vlan10
	ip neigh del 10.1.2.102 dev vlan20

	ping_ipv4

	ip neigh replace 10.1.1.102 lladdr $(in_ns ns1 mac_get w2) nud noarp \
		dev vlan10 extern_learn
	ip neigh replace 10.1.2.102 lladdr $(in_ns ns1 mac_get w4) nud noarp \
		dev vlan20 extern_learn
}

arp_suppression_compare()
{
	local expect=$1; shift
	local actual=$(in_ns ns1 tc_rule_stats_get vx10 1 ingress)

	(( expect == actual ))
	check_err $? "expected $expect arps got $actual"
}

arp_suppression()
{
	ip link set dev vx10 type bridge_slave neigh_suppress on

	in_ns ns1 tc qdisc add dev vx10 clsact
	in_ns ns1 tc filter add dev vx10 ingress proto arp pref 1 handle 101 \
		flower dst_mac ff:ff:ff:ff:ff:ff arp_tip 10.1.1.102 arp_op \
		request action pass

	# The neighbour is configured on the SVI and ARP suppression is on, so
	# the ARP request should be suppressed
	RET=0

	$ARPING -I $h1 -fqb -c 1 -w 1 10.1.1.102
	check_err $? "arping failed"

	arp_suppression_compare 0

	log_test "neigh_suppress: on / neigh exists: yes"

	# Delete the neighbour from the the SVI. A single ARP request should be
	# received by the remote VTEP
	RET=0

	ip neigh del 10.1.1.102 dev vlan10

	$ARPING -I $h1 -fqb -c 1 -w 1 10.1.1.102
	check_err $? "arping failed"

	arp_suppression_compare 1

	log_test "neigh_suppress: on / neigh exists: no"

	# Turn off ARP suppression and make sure ARP is not suppressed,
	# regardless of neighbour existence on the SVI
	RET=0

	ip neigh del 10.1.1.102 dev vlan10 &> /dev/null
	ip link set dev vx10 type bridge_slave neigh_suppress off

	$ARPING -I $h1 -fqb -c 1 -w 1 10.1.1.102
	check_err $? "arping failed"

	arp_suppression_compare 2

	log_test "neigh_suppress: off / neigh exists: no"

	RET=0

	ip neigh add 10.1.1.102 lladdr $(in_ns ns1 mac_get w2) nud noarp \
		dev vlan10 extern_learn

	$ARPING -I $h1 -fqb -c 1 -w 1 10.1.1.102
	check_err $? "arping failed"

	arp_suppression_compare 3

	log_test "neigh_suppress: off / neigh exists: yes"

	in_ns ns1 tc qdisc del dev vx10 clsact
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
