#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test routing after VXLAN decapsulation and verify that the order of
# configuration does not impact switch behavior. Verify that RIF is added
# correctly for existing mapping and that new mapping uses the correct RIF.

# +---------------------------+
# |                        H1 |
# |    + $h1                  |
# |    | 192.0.2.1/28         |
# +----|----------------------+
#      |
# +----|----------------------------------------------------------------------+
# | SW |                                                                      |
# | +--|--------------------------------------------------------------------+ |
# | |  + $swp1                         br1                                  | |
# | |     vid 10 pvid untagged                                              | |
# | |                                                                       | |
# | |                                                                       | |
# | |                                            + vx4001                   | |
# | |                                              local 192.0.2.17         | |
# | |                                              remote 192.0.2.18        | |
# | |                                              id 104001                | |
# | |                                              dstport $VXPORT          | |
# | |                                              vid 4001 pvid untagged   | |
# | |                                                                       | |
# | +----------------------------------+------------------------------------+ |
# |                                    |                                      |
# | +----------------------------------|------------------------------------+ |
# | |                                  |                                    | |
# | |  +-------------------------------+---------------------------------+  | |
# | |  |                                                                 |  | |
# | |  + vlan10                                                 vlan4001 +  | |
# | |    192.0.2.2/28                                                       | |
# | |                                                                       | |
# | |                               vrf-green                               | |
# | +-----------------------------------------------------------------------+ |
# |                                                                           |
# |    + $rp1                                       +lo                       |
# |    | 198.51.100.1/24                             192.0.2.17/32            |
# +----|----------------------------------------------------------------------+
#      |
# +----|--------------------------------------------------------+
# |    |                                             v$rp2      |
# |    + $rp2                                                   |
# |      198.51.100.2/24                                        |
# |                                                             |
# +-------------------------------------------------------------+

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	vni_fid_map_rif
	rif_vni_fid_map
"

NUM_NETIFS=4
source $lib_dir/lib.sh
source $lib_dir/tc_common.sh
source $lib_dir/devlink_lib.sh

: ${VXPORT:=4789}
export VXPORT

h1_create()
{
	simple_if_init $h1 192.0.2.1/28
}

h1_destroy()
{
	simple_if_fini $h1 192.0.2.1/28
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
	ip address add dev $rp1 198.51.100.1/24

	ip link set dev $swp1 master br1
	ip link set dev $swp1 up
	bridge vlan add vid 10 dev $swp1 pvid untagged

	tc qdisc add dev $swp1 clsact

	ip link add name vx4001 type vxlan id 104001 \
		local 192.0.2.17 dstport $VXPORT \
		nolearning noudpcsum tos inherit ttl 100
	ip link set dev vx4001 up

	ip link set dev vx4001 master br1

	ip address add 192.0.2.17/32 dev lo

	# Create SVIs.
	vrf_create "vrf-green"
	ip link set dev vrf-green up

	ip link add link br1 name vlan10 up master vrf-green type vlan id 10

	# Replace neighbor to avoid 1 packet which is forwarded in software due
	# to "unresolved neigh".
	ip neigh replace dev vlan10 192.0.2.1 lladdr $(mac_get $h1)

	ip address add 192.0.2.2/28 dev vlan10

	bridge vlan add vid 10 dev br1 self
	bridge vlan add vid 4001 dev br1 self

	sysctl_set net.ipv4.conf.all.rp_filter 0
}

switch_destroy()
{
	sysctl_restore net.ipv4.conf.all.rp_filter

	bridge vlan del vid 4001 dev br1 self
	bridge vlan del vid 10 dev br1 self

	ip link del dev vlan10

	vrf_destroy "vrf-green"

	ip address del 192.0.2.17/32 dev lo

	tc qdisc del dev $swp1 clsact

	bridge vlan del vid 10 dev $swp1
	ip link set dev $swp1 down
	ip link set dev $swp1 nomaster

	ip link set dev vx4001 nomaster

	ip link set dev vx4001 down
	ip link del dev vx4001

	ip address del dev $rp1 198.51.100.1/24
	ip link set dev $rp1 down

	ip link set dev br1 down
	ip link del dev br1
}

vrp2_create()
{
	simple_if_init $rp2 198.51.100.2/24

	ip route add 192.0.2.17/32 vrf v$rp2 nexthop via 198.51.100.1
}

vrp2_destroy()
{
	ip route del 192.0.2.17/32 vrf v$rp2 nexthop via 198.51.100.1

	simple_if_fini $rp2 198.51.100.2/24
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	rp1=${NETIFS[p3]}
	rp2=${NETIFS[p4]}

	vrf_prepare
	forwarding_enable

	h1_create
	switch_create

	vrp2_create
}

cleanup()
{
	pre_cleanup

	vrp2_destroy

	switch_destroy
	h1_destroy

	forwarding_restore
	vrf_cleanup
}

payload_get()
{
	local dest_mac=$(mac_get vlan4001)
	local src_mac=$(mac_get $rp1)

	p=$(:
		)"08:"$(                      : VXLAN flags
		)"00:00:00:"$(                : VXLAN reserved
		)"01:96:41:"$(                : VXLAN VNI : 104001
		)"00:"$(                      : VXLAN reserved
		)"$dest_mac:"$(               : ETH daddr
		)"$src_mac:"$(                : ETH saddr
		)"08:00:"$(                   : ETH type
		)"45:"$(                      : IP version + IHL
		)"00:"$(                      : IP TOS
		)"00:54:"$(                   : IP total length
		)"3f:49:"$(                   : IP identification
		)"00:00:"$(                   : IP flags + frag off
		)"3f:"$(                      : IP TTL
		)"01:"$(                      : IP proto
		)"50:21:"$(                   : IP header csum
		)"c6:33:64:0a:"$(             : IP saddr: 198.51.100.10
		)"c0:00:02:01:"$(             : IP daddr: 192.0.2.1
	)
	echo $p
}

vlan_rif_add()
{
	rifs_occ_t0=$(devlink_resource_occ_get rifs)

	ip link add link br1 name vlan4001 up master vrf-green \
		type vlan id 4001

	rifs_occ_t1=$(devlink_resource_occ_get rifs)
	expected_rifs=$((rifs_occ_t0 + 1))

	[[ $expected_rifs -eq $rifs_occ_t1 ]]
	check_err $? "Expected $expected_rifs RIFs, $rifs_occ_t1 are used"
}

vlan_rif_del()
{
	ip link del dev vlan4001
}

vni_fid_map_rif()
{
	local rp1_mac=$(mac_get $rp1)

	RET=0

	# First add VNI->FID mapping to the FID of VLAN 4001
	bridge vlan add vid 4001 dev vx4001 pvid untagged

	# Add a RIF to the FID with VNI->FID mapping
	vlan_rif_add

	tc filter add dev $swp1 egress protocol ip pref 1 handle 101 \
		flower skip_sw dst_ip 192.0.2.1 action pass

	payload=$(payload_get)
	ip vrf exec v$rp2 $MZ $rp2 -c 10 -d 1msec -b $rp1_mac \
		-B 192.0.2.17 -A 192.0.2.18 \
		-t udp sp=12345,dp=$VXPORT,p=$payload -q

	tc_check_at_least_x_packets "dev $swp1 egress" 101 10
	check_err $? "Packets were not routed in hardware"

	log_test "Add RIF for existing VNI->FID mapping"

	tc filter del dev $swp1 egress

	bridge vlan del vid 4001 dev vx4001 pvid untagged
	vlan_rif_del
}

rif_vni_fid_map()
{
	local rp1_mac=$(mac_get $rp1)

	RET=0

	# First add a RIF to the FID of VLAN 4001
	vlan_rif_add

	# Add VNI->FID mapping to FID with a RIF
	bridge vlan add vid 4001 dev vx4001 pvid untagged

	tc filter add dev $swp1 egress protocol ip pref 1 handle 101 \
		flower skip_sw dst_ip 192.0.2.1 action pass

	payload=$(payload_get)
	ip vrf exec v$rp2 $MZ $rp2 -c 10 -d 1msec -b $rp1_mac \
		-B 192.0.2.17 -A 192.0.2.18 \
		-t udp sp=12345,dp=$VXPORT,p=$payload -q

	tc_check_at_least_x_packets "dev $swp1 egress" 101 10
	check_err $? "Packets were not routed in hardware"

	log_test "Add VNI->FID mapping for FID with a RIF"

	tc filter del dev $swp1 egress

	bridge vlan del vid 4001 dev vx4001 pvid untagged
	vlan_rif_del
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
