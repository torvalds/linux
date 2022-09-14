#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# This test sends many small packets (size is less than cell size) through the
# switch. A shaper is used in $swp2, so the traffic is limited there. Packets
# are queued till they will be sent.
#
# The idea is to verify that the switch can handle at least 85% of maximum
# supported descrpitors by hardware. Then, we verify that the driver configures
# firmware to allow infinite size of egress descriptor pool, and does not use a
# lower limitation. Increase the size of the relevant pools such that the pool's
# size does not limit the traffic.

# +-----------------------+
# | H1                    |
# |   + $h1.111           |
# |   | 192.0.2.33/28     |
# |   |                   |
# |   + $h1               |
# +---|-------------------+
#     |
# +---|-----------------------------+
# |   + $swp1                       |
# |   | iPOOL1                      |
# |   |                             |
# | +-|------------------------+    |
# | | + $swp1.111              |    |
# | |                          |    |
# | | BR1                      |    |
# | |                          |    |
# | | + $swp2.111              |    |
# | +-|------------------------+    |
# |   |                             |
# |   + $swp2                       |
# |   | ePOOL6                      |
# |   | 1mbit                       |
# +---+-----------------------------+
#     |
# +---|-------------------+
# |   + $h2            H2 |
# |   |                   |
# |   + $h2.111           |
# |     192.0.2.34/28     |
# +-----------------------+
#

ALL_TESTS="
	ping_ipv4
	max_descriptors
"

lib_dir=$(dirname $0)/../../../net/forwarding

NUM_NETIFS=4
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh
source mlxsw_lib.sh

MAX_POOL_SIZE=$(devlink_pool_size_get)
SHAPER_RATE=1mbit

# The current TBF qdisc interface does not allow us to configure the shaper to
# flat zero. The ASIC shaper is guaranteed to work with a granularity of
# 200Mbps. On Spectrum-2, writing a value close to zero instead of zero works
# well, but the performance on Spectrum-1 is unpredictable. Thus, do not run the
# test on Spectrum-1.
mlxsw_only_on_spectrum 2+ || exit

h1_create()
{
	simple_if_init $h1

	vlan_create $h1 111 v$h1 192.0.2.33/28
	ip link set dev $h1.111 type vlan egress-qos-map 0:1
}

h1_destroy()
{
	vlan_destroy $h1 111

	simple_if_fini $h1
}

h2_create()
{
	simple_if_init $h2

	vlan_create $h2 111 v$h2 192.0.2.34/28
}

h2_destroy()
{
	vlan_destroy $h2 111

	simple_if_fini $h2
}

switch_create()
{
	# pools
	# -----

	devlink_pool_size_thtype_save 1
	devlink_pool_size_thtype_save 6

	devlink_port_pool_th_save $swp1 1
	devlink_port_pool_th_save $swp2 6

	devlink_tc_bind_pool_th_save $swp1 1 ingress
	devlink_tc_bind_pool_th_save $swp2 1 egress

	devlink_pool_size_thtype_set 1 dynamic $MAX_POOL_SIZE
	devlink_pool_size_thtype_set 6 static $MAX_POOL_SIZE

	# $swp1
	# -----

	ip link set dev $swp1 up
	vlan_create $swp1 111
	ip link set dev $swp1.111 type vlan ingress-qos-map 0:0 1:1

	devlink_port_pool_th_set $swp1 1 16
	devlink_tc_bind_pool_th_set $swp1 1 ingress 1 16

	tc qdisc replace dev $swp1 root handle 1: \
	   ets bands 8 strict 8 priomap 7 6
	dcb buffer set dev $swp1 prio-buffer all:0 1:1

	# $swp2
	# -----

	ip link set dev $swp2 up
	vlan_create $swp2 111
	ip link set dev $swp2.111 type vlan egress-qos-map 0:0 1:1

	devlink_port_pool_th_set $swp2 6 $MAX_POOL_SIZE
	devlink_tc_bind_pool_th_set $swp2 1 egress 6 $MAX_POOL_SIZE

	tc qdisc replace dev $swp2 root handle 1: tbf rate $SHAPER_RATE \
		burst 128K limit 500M
	tc qdisc replace dev $swp2 parent 1:1 handle 11: \
		ets bands 8 strict 8 priomap 7 6

	# bridge
	# ------

	ip link add name br1 type bridge vlan_filtering 0
	ip link set dev $swp1.111 master br1
	ip link set dev br1 up

	ip link set dev $swp2.111 master br1
}

switch_destroy()
{
	# Do this first so that we can reset the limits to values that are only
	# valid for the original static / dynamic setting.
	devlink_pool_size_thtype_restore 6
	devlink_pool_size_thtype_restore 1

	# bridge
	# ------

	ip link set dev $swp2.111 nomaster

	ip link set dev br1 down
	ip link set dev $swp1.111 nomaster
	ip link del dev br1

	# $swp2
	# -----

	tc qdisc del dev $swp2 parent 1:1 handle 11:
	tc qdisc del dev $swp2 root

	devlink_tc_bind_pool_th_restore $swp2 1 egress
	devlink_port_pool_th_restore $swp2 6

	vlan_destroy $swp2 111
	ip link set dev $swp2 down

	# $swp1
	# -----

	dcb buffer set dev $swp1 prio-buffer all:0
	tc qdisc del dev $swp1 root

	devlink_tc_bind_pool_th_restore $swp1 1 ingress
	devlink_port_pool_th_restore $swp1 1

	vlan_destroy $swp1 111
	ip link set dev $swp1 down
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	h2mac=$(mac_get $h2)

	vrf_prepare

	h1_create
	h2_create
	switch_create
}

cleanup()
{
	pre_cleanup

	switch_destroy
	h2_destroy
	h1_destroy

	vrf_cleanup
}

ping_ipv4()
{
	ping_test $h1 192.0.2.34 " h1->h2"
}

percentage_used()
{
	local num_packets=$1; shift
	local max_packets=$1; shift

	bc <<< "
	    scale=2
	    100 * $num_packets / $max_packets
	"
}

max_descriptors()
{
	local cell_size=$(devlink_cell_size_get)
	local exp_perc_used=85
	local max_descriptors
	local pktsize=30

	RET=0

	max_descriptors=$(mlxsw_max_descriptors_get) || exit 1

	local d0=$(ethtool_stats_get $swp2 tc_no_buffer_discard_uc_tc_1)

	log_info "Send many small packets, packet size = $pktsize bytes"
	start_traffic_pktsize $pktsize $h1.111 192.0.2.33 192.0.2.34 $h2mac

	# Sleep to wait for congestion.
	sleep 5

	local d1=$(ethtool_stats_get $swp2 tc_no_buffer_discard_uc_tc_1)
	((d1 == d0))
	check_err $? "Drops seen on egress port: $d0 -> $d1 ($((d1 - d0)))"

	# Check how many packets the switch can handle, the limitation is
	# maximum descriptors.
	local pkts_bytes=$(ethtool_stats_get $swp2 tc_transmit_queue_tc_1)
	local pkts_num=$((pkts_bytes / cell_size))
	local perc_used=$(percentage_used $pkts_num $max_descriptors)

	check_err $(bc <<< "$perc_used < $exp_perc_used") \
		"Expected > $exp_perc_used% of descriptors, handle $perc_used%"

	stop_traffic
	sleep 1

	log_test "Maximum descriptors usage. The percentage used is $perc_used%"
}

trap cleanup EXIT
setup_prepare
setup_wait
tests_run

exit $EXIT_STATUS
