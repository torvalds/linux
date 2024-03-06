#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test VLAN classification after routing and verify that the order of
# configuration does not impact switch behavior. Verify that {RIF, Port}->VID
# mapping is added correctly for existing {Port, VID}->FID mapping and that
# {RIF, Port}->VID mapping is added correctly for new {Port, VID}->FID mapping.

# +-------------------+                   +--------------------+
# | H1                |                   | H2                 |
# |                   |                   |                    |
# |         $h1.10 +  |                   |  + $h2.10          |
# |   192.0.2.1/28 |  |                   |  | 192.0.2.3/28    |
# |                |  |                   |  |                 |
# |            $h1 +  |                   |  + $h2             |
# +----------------|--+                   +--|-----------------+
#                  |                         |
# +----------------|-------------------------|-----------------+
# | SW       $swp1 +                         + $swp2           |
# |                |                         |                 |
# | +--------------|-------------------------|---------------+ |
# | |     $swp1.10 +                         + $swp2.10      | |
# | |                                                        | |
# | |                           br0                          | |
# | |                       192.0.2.2/28                     | |
# | +--------------------------------------------------------+ |
# |                                                            |
# |      $swp3.20 +                                            |
# | 192.0.2.17/28 |                                            |
# |               |                                            |
# |         $swp3 +                                            |
# +---------------|--------------------------------------------+
#                 |
# +---------------|--+
# |           $h3 +  |
# |               |  |
# |        $h3.20 +  |
# | 192.0.2.18/28    |
# |                  |
# | H3               |
# +------------------+

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	port_vid_map_rif
	rif_port_vid_map
"

NUM_NETIFS=6
source $lib_dir/lib.sh
source $lib_dir/tc_common.sh
source $lib_dir/devlink_lib.sh

h1_create()
{
	simple_if_init $h1
	vlan_create $h1 10 v$h1 192.0.2.1/28

	ip route add 192.0.2.16/28 vrf v$h1 nexthop via 192.0.2.2
}

h1_destroy()
{
	ip route del 192.0.2.16/28 vrf v$h1 nexthop via 192.0.2.2

	vlan_destroy $h1 10
	simple_if_fini $h1
}

h2_create()
{
	simple_if_init $h2
	vlan_create $h2 10 v$h2 192.0.2.3/28
}

h2_destroy()
{
	vlan_destroy $h2 10
	simple_if_fini $h2
}

h3_create()
{
	simple_if_init $h3
	vlan_create $h3 20 v$h3 192.0.2.18/28

	ip route add 192.0.2.0/28 vrf v$h3 nexthop via 192.0.2.17
}

h3_destroy()
{
	ip route del 192.0.2.0/28 vrf v$h3 nexthop via 192.0.2.17

	vlan_destroy $h3 20
	simple_if_fini $h3
}

switch_create()
{
	ip link set dev $swp1 up
	tc qdisc add dev $swp1 clsact

	ip link add dev br0 type bridge mcast_snooping 0

	# By default, a link-local address is generated when netdevice becomes
	# up. Adding an address to the bridge will cause creating a RIF for it.
	# Prevent generating link-local address to be able to control when the
	# RIF is added.
	sysctl_set net.ipv6.conf.br0.addr_gen_mode 1
	ip link set dev br0 up

	ip link set dev $swp2 up
	vlan_create $swp2 10
	ip link set dev $swp2.10 master br0

	ip link set dev $swp3 up
	vlan_create $swp3 20 "" 192.0.2.17/28

	# Replace neighbor to avoid 1 packet which is forwarded in software due
	# to "unresolved neigh".
	ip neigh replace dev $swp3.20 192.0.2.18 lladdr $(mac_get $h3.20)
}

switch_destroy()
{
	vlan_destroy $swp3 20
	ip link set dev $swp3 down

	ip link set dev $swp2.10 nomaster
	vlan_destroy $swp2 10
	ip link set dev $swp2 down

	ip link set dev br0 down
	sysctl_restore net.ipv6.conf.br0.addr_gen_mode
	ip link del dev br0

	tc qdisc del dev $swp1 clsact
	ip link set dev $swp1 down
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	swp3=${NETIFS[p5]}
	h3=${NETIFS[p6]}

	vrf_prepare
	forwarding_enable

	h1_create
	h2_create
	h3_create

	switch_create
}

cleanup()
{
	pre_cleanup

	switch_destroy

	h3_destroy
	h2_destroy
	h1_destroy

	forwarding_restore
	vrf_cleanup
}

bridge_rif_add()
{
	rifs_occ_t0=$(devlink_resource_occ_get rifs)
	__addr_add_del br0 add 192.0.2.2/28
	rifs_occ_t1=$(devlink_resource_occ_get rifs)

	expected_rifs=$((rifs_occ_t0 + 1))

	[[ $expected_rifs -eq $rifs_occ_t1 ]]
	check_err $? "Expected $expected_rifs RIFs, $rifs_occ_t1 are used"

	sleep 1
}

bridge_rif_del()
{
	__addr_add_del br0 del 192.0.2.2/28
}

port_vid_map_rif()
{
	RET=0

	# First add {port, VID}->FID for swp1.10, then add a RIF and verify that
	# packets get the correct VID after routing.
	vlan_create $swp1 10
	ip link set dev $swp1.10 master br0
	bridge_rif_add

	# Replace neighbor to avoid 1 packet which is forwarded in software due
	# to "unresolved neigh".
	ip neigh replace dev br0 192.0.2.1 lladdr $(mac_get $h1.10)

	# The hardware matches on the first ethertype which is not VLAN,
	# so the protocol should be IP.
	tc filter add dev $swp1 egress protocol ip pref 1 handle 101 \
		flower skip_sw dst_ip 192.0.2.1 action pass

	ping_do $h1.10 192.0.2.18
	check_err $? "Ping failed"

	tc_check_at_least_x_packets "dev $swp1 egress" 101 10
	check_err $? "Packets were not routed in hardware"

	log_test "Add RIF for existing {port, VID}->FID mapping"

	tc filter del dev $swp1 egress

	bridge_rif_del
	ip link set dev $swp1.10 nomaster
	vlan_destroy $swp1 10
}

rif_port_vid_map()
{
	RET=0

	# First add an address to the bridge, which will create a RIF on top of
	# it, then add a new {port, VID}->FID mapping and verify that packets
	# get the correct VID after routing.
	bridge_rif_add
	vlan_create $swp1 10
	ip link set dev $swp1.10 master br0

	# Replace neighbor to avoid 1 packet which is forwarded in software due
	# to "unresolved neigh".
	ip neigh replace dev br0 192.0.2.1 lladdr $(mac_get $h1.10)

	# The hardware matches on the first ethertype which is not VLAN,
	# so the protocol should be IP.
	tc filter add dev $swp1 egress protocol ip pref 1 handle 101 \
		flower skip_sw dst_ip 192.0.2.1 action pass

	ping_do $h1.10 192.0.2.18
	check_err $? "Ping failed"

	tc_check_at_least_x_packets "dev $swp1 egress" 101 10
	check_err $? "Packets were not routed in hardware"

	log_test "Add {port, VID}->FID mapping for FID with a RIF"

	tc filter del dev $swp1 egress

	ip link set dev $swp1.10 nomaster
	vlan_destroy $swp1 10
	bridge_rif_del
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
