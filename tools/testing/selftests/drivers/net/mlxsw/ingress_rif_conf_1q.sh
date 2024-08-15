#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test routing over bridge and verify that the order of configuration does not
# impact switch behavior. Verify that RIF is added correctly for existing
# mapping and that packets can be routed via port which is added after the FID
# already has a RIF.

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
# | SW             |                         |                 |
# | +--------------|-------------------------|---------------+ |
# | |        $swp1 +                         + $swp2         | |
# | |                                                        | |
# | |                           br0                          | |
# | +--------------------------------------------------------+ |
# |                              |                             |
# |                           br0.10                           |
# |                        192.0.2.2/28                        |
# |                                                            |
# |                                                            |
# |          $swp3 +                                           |
# |  192.0.2.17/28 |                                           |
# +----------------|-------------------------------------------+
#                  |
# +----------------|--+
# |            $h3 +  |
# |  192.0.2.18/28    |
# |                   |
# | H3                |
# +-------------------+

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	vid_map_rif
	rif_vid_map
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
	simple_if_init $h3 192.0.2.18/28
	ip route add 192.0.2.0/28 vrf v$h3 nexthop via 192.0.2.17
}

h3_destroy()
{
	ip route del 192.0.2.0/28 vrf v$h3 nexthop via 192.0.2.17
	simple_if_fini $h3 192.0.2.18/28
}

switch_create()
{
	ip link set dev $swp1 up

	ip link add dev br0 type bridge vlan_filtering 1 mcast_snooping 0

	# By default, a link-local address is generated when netdevice becomes
	# up. Adding an address to the bridge will cause creating a RIF for it.
	# Prevent generating link-local address to be able to control when the
	# RIF is added.
	sysctl_set net.ipv6.conf.br0.addr_gen_mode 1
	ip link set dev br0 up

	ip link set dev $swp2 up
	ip link set dev $swp2 master br0
	bridge vlan add vid 10 dev $swp2

	ip link set dev $swp3 up
	__addr_add_del $swp3 add 192.0.2.17/28
	tc qdisc add dev $swp3 clsact

	# Replace neighbor to avoid 1 packet which is forwarded in software due
	# to "unresolved neigh".
	ip neigh replace dev $swp3 192.0.2.18 lladdr $(mac_get $h3)
}

switch_destroy()
{
	tc qdisc del dev $swp3 clsact
	__addr_add_del $swp3 del 192.0.2.17/28
	ip link set dev $swp3 down

	bridge vlan del vid 10 dev $swp2
	ip link set dev $swp2 nomaster
	ip link set dev $swp2 down

	ip link set dev br0 down
	sysctl_restore net.ipv6.conf.br0.addr_gen_mode
	ip link del dev br0

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
	vlan_create br0 10 "" 192.0.2.2/28
	rifs_occ_t1=$(devlink_resource_occ_get rifs)

	expected_rifs=$((rifs_occ_t0 + 1))

	[[ $expected_rifs -eq $rifs_occ_t1 ]]
	check_err $? "Expected $expected_rifs RIFs, $rifs_occ_t1 are used"

	sleep 1
}

bridge_rif_del()
{
	vlan_destroy br0 10
}

vid_map_rif()
{
	RET=0

	# First add VID->FID for vlan 10, then add a RIF and verify that
	# packets can be routed via the existing mapping.
	bridge vlan add vid 10 dev br0 self
	ip link set dev $swp1 master br0
	bridge vlan add vid 10 dev $swp1

	bridge_rif_add

	tc filter add dev $swp3 egress protocol ip pref 1 handle 101 \
		flower skip_sw dst_ip 192.0.2.18 action pass

	ping_do $h1.10 192.0.2.18
	check_err $? "Ping failed"

	tc_check_at_least_x_packets "dev $swp3 egress" 101 10
	check_err $? "Packets were not routed in hardware"

	log_test "Add RIF for existing VID->FID mapping"

	tc filter del dev $swp3 egress

	bridge_rif_del

	bridge vlan del vid 10 dev $swp1
	ip link set dev $swp1 nomaster
	bridge vlan del vid 10 dev br0 self
}

rif_vid_map()
{
	RET=0

	# Using 802.1Q, there is only one VID->FID map for each VID. That means
	# that we cannot really check adding a new map for existing FID with a
	# RIF. Verify that packets can be routed via port which is added after
	# the FID already has a RIF, although in practice there is no new
	# mapping in the hardware.
	bridge vlan add vid 10 dev br0 self
	bridge_rif_add

	ip link set dev $swp1 master br0
	bridge vlan add vid 10 dev $swp1

	tc filter add dev $swp3 egress protocol ip pref 1 handle 101 \
		flower skip_sw dst_ip 192.0.2.18 action pass

	ping_do $h1.10 192.0.2.18
	check_err $? "Ping failed"

	tc_check_at_least_x_packets "dev $swp3 egress" 101 10
	check_err $? "Packets were not routed in hardware"

	log_test "Add port to VID->FID mapping for FID with a RIF"

	tc filter del dev $swp3 egress

	bridge vlan del vid 10 dev $swp1
	ip link set dev $swp1 nomaster

	bridge_rif_del
	bridge vlan del vid 10 dev br0 self
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
