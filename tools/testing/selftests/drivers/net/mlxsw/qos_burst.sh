#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# This test sends 1Gbps of traffic through the switch, into which it then
# injects a burst of traffic and tests that there are no drops.
#
# The 1Gbps stream is created by sending >1Gbps stream from H1. This stream
# ingresses through $swp1, and is forwarded thtrough a small temporary pool to a
# 1Gbps $swp3.
#
# Thus a 1Gbps stream enters $swp4, and is forwarded through a large pool to
# $swp2, and eventually to H2. Since $swp2 is a 1Gbps port as well, no backlog
# is generated.
#
# At this point, a burst of traffic is forwarded from H3. This enters $swp5, is
# forwarded to $swp2, which is fully subscribed by the 1Gbps stream. The
# expectation is that the burst is wholly absorbed by the large pool and no
# drops are caused. After the burst, there should be a backlog that is hard to
# get rid of, because $sw2 is fully subscribed. But because each individual
# packet is scheduled soon after getting enqueued, SLL and HLL do not impact the
# test.
#
# +-----------------------+                           +-----------------------+
# | H1                    |			      | H3                    |
# |   + $h1.111           |			      |          $h3.111 +    |
# |   | 192.0.2.33/28     |			      |    192.0.2.35/28 |    |
# |   |                   |			      |                  |    |
# |   + $h1               |			      |              $h3 +    |
# +---|-------------------+  +--------------------+   +------------------|----+
#     |                      |                    |       		 |
# +---|----------------------|--------------------|----------------------|----+
# |   + $swp1          $swp3 +                    + $swp4          $swp5 |    |
# |   | iPOOL1        iPOOL0 |                    | iPOOL2        iPOOL2 |    |
# |   | ePOOL4        ePOOL5 |                    | ePOOL4        ePOOL4 |    |
# |   |                1Gbps |                    | 1Gbps                |    |
# | +-|----------------------|-+                +-|----------------------|-+  |
# | | + $swp1.111  $swp3.111 + |                | + $swp4.111  $swp5.111 + |  |
# | |                          |                |                          |  |
# | | BR1                      |                | BR2                      |  |
# | |                          |                |                          |  |
# | |                          |                |         + $swp2.111      |  |
# | +--------------------------+                +---------|----------------+  |
# |                                                       |                   |
# | iPOOL0: 500KB dynamic                                 |                   |
# | iPOOL1: 500KB dynamic                                 |                   |
# | iPOOL2: 10MB dynamic                                  + $swp2             |
# | ePOOL4: 500KB dynamic                                 | iPOOL0            |
# | ePOOL5: 500KB dnamic                                  | ePOOL6            |
# | ePOOL6: 10MB dynamic                                  | 1Gbps             |
# +-------------------------------------------------------|-------------------+
#                                                         |
#                                                     +---|-------------------+
#                                                     |   + $h2            H2 |
#                                                     |   | 1Gbps             |
#                                                     |   |                   |
#                                                     |   + $h2.111           |
#                                                     |     192.0.2.34/28     |
#                                                     +-----------------------+
#
# iPOOL0+ePOOL4 are helper pools for control traffic etc.
# iPOOL1+ePOOL5 are helper pools for modeling the 1Gbps stream
# iPOOL2+ePOOL6 are pools for soaking the burst traffic

ALL_TESTS="
	ping_ipv4
	test_8K
	test_800
"

lib_dir=$(dirname $0)/../../../net/forwarding

NUM_NETIFS=8
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh
source qos_lib.sh
source mlxsw_lib.sh

_1KB=1000
_500KB=$((500 * _1KB))
_1MB=$((1000 * _1KB))

# The failure mode that this specifically tests is exhaustion of descriptor
# buffer. The point is to produce a burst that shared buffer should be able
# to accommodate, but produce it with small enough packets that the machine
# runs out of the descriptor buffer space with default configuration.
#
# The machine therefore needs to be able to produce line rate with as small
# packets as possible, and at the same time have large enough buffer that
# when filled with these small packets, it runs out of descriptors.
# Spectrum-2 is very close, but cannot perform this test. Therefore use
# Spectrum-3 as a minimum, and permit larger burst size, and therefore
# larger packets, to reduce spurious failures.
#
mlxsw_only_on_spectrum 3+ || exit

BURST_SIZE=$((50000000))
POOL_SIZE=$BURST_SIZE

h1_create()
{
	simple_if_init $h1
	mtu_set $h1 10000

	vlan_create $h1 111 v$h1 192.0.2.33/28
	ip link set dev $h1.111 type vlan egress-qos-map 0:1
}

h1_destroy()
{
	vlan_destroy $h1 111

	mtu_restore $h1
	simple_if_fini $h1
}

h2_create()
{
	simple_if_init $h2
	mtu_set $h2 10000
	ethtool -s $h2 speed 1000 autoneg off

	vlan_create $h2 111 v$h2 192.0.2.34/28
}

h2_destroy()
{
	vlan_destroy $h2 111

	ethtool -s $h2 autoneg on
	mtu_restore $h2
	simple_if_fini $h2
}

h3_create()
{
	simple_if_init $h3
	mtu_set $h3 10000

	vlan_create $h3 111 v$h3 192.0.2.35/28
}

h3_destroy()
{
	vlan_destroy $h3 111

	mtu_restore $h3
	simple_if_fini $h3
}

switch_create()
{
	# pools
	# -----

	devlink_pool_size_thtype_save 0
	devlink_pool_size_thtype_save 4
	devlink_pool_size_thtype_save 1
	devlink_pool_size_thtype_save 5
	devlink_pool_size_thtype_save 2
	devlink_pool_size_thtype_save 6

	devlink_port_pool_th_save $swp1 1
	devlink_port_pool_th_save $swp2 6
	devlink_port_pool_th_save $swp3 5
	devlink_port_pool_th_save $swp4 2
	devlink_port_pool_th_save $swp5 2

	devlink_tc_bind_pool_th_save $swp1 1 ingress
	devlink_tc_bind_pool_th_save $swp2 1 egress
	devlink_tc_bind_pool_th_save $swp3 1 egress
	devlink_tc_bind_pool_th_save $swp4 1 ingress
	devlink_tc_bind_pool_th_save $swp5 1 ingress

	# Control traffic pools. Just reduce the size.
	devlink_pool_size_thtype_set 0 dynamic $_500KB
	devlink_pool_size_thtype_set 4 dynamic $_500KB

	# Stream modeling pools.
	devlink_pool_size_thtype_set 1 dynamic $_500KB
	devlink_pool_size_thtype_set 5 dynamic $_500KB

	# Burst soak pools.
	devlink_pool_size_thtype_set 2 static $POOL_SIZE
	devlink_pool_size_thtype_set 6 static $POOL_SIZE

	# $swp1
	# -----

	ip link set dev $swp1 up
	mtu_set $swp1 10000
	vlan_create $swp1 111
	ip link set dev $swp1.111 type vlan ingress-qos-map 0:0 1:1

	devlink_port_pool_th_set $swp1 1 16
	devlink_tc_bind_pool_th_set $swp1 1 ingress 1 16

	# Configure qdisc...
	tc qdisc replace dev $swp1 root handle 1: \
	   ets bands 8 strict 8 priomap 7 6
	# ... so that we can assign prio1 traffic to PG1.
	dcb buffer set dev $swp1 prio-buffer all:0 1:1

	# $swp2
	# -----

	ip link set dev $swp2 up
	mtu_set $swp2 10000
	ethtool -s $swp2 speed 1000 autoneg off
	vlan_create $swp2 111
	ip link set dev $swp2.111 type vlan egress-qos-map 0:0 1:1

	devlink_port_pool_th_set $swp2 6 $POOL_SIZE
	devlink_tc_bind_pool_th_set $swp2 1 egress 6 $POOL_SIZE

	# prio 0->TC0 (band 7), 1->TC1 (band 6)
	tc qdisc replace dev $swp2 root handle 1: \
	   ets bands 8 strict 8 priomap 7 6

	# $swp3
	# -----

	ip link set dev $swp3 up
	mtu_set $swp3 10000
	ethtool -s $swp3 speed 1000 autoneg off
	vlan_create $swp3 111
	ip link set dev $swp3.111 type vlan egress-qos-map 0:0 1:1

	devlink_port_pool_th_set $swp3 5 16
	devlink_tc_bind_pool_th_set $swp3 1 egress 5 16

	# prio 0->TC0 (band 7), 1->TC1 (band 6)
	tc qdisc replace dev $swp3 root handle 1: \
	   ets bands 8 strict 8 priomap 7 6

	# $swp4
	# -----

	ip link set dev $swp4 up
	mtu_set $swp4 10000
	ethtool -s $swp4 speed 1000 autoneg off
	vlan_create $swp4 111
	ip link set dev $swp4.111 type vlan ingress-qos-map 0:0 1:1

	devlink_port_pool_th_set $swp4 2 $POOL_SIZE
	devlink_tc_bind_pool_th_set $swp4 1 ingress 2 $POOL_SIZE

	# Configure qdisc...
	tc qdisc replace dev $swp4 root handle 1: \
	   ets bands 8 strict 8 priomap 7 6
	# ... so that we can assign prio1 traffic to PG1.
	dcb buffer set dev $swp4 prio-buffer all:0 1:1

	# $swp5
	# -----

	ip link set dev $swp5 up
	mtu_set $swp5 10000
	vlan_create $swp5 111
	ip link set dev $swp5.111 type vlan ingress-qos-map 0:0 1:1

	devlink_port_pool_th_set $swp5 2 $POOL_SIZE
	devlink_tc_bind_pool_th_set $swp5 1 ingress 2 $POOL_SIZE

	# Configure qdisc...
	tc qdisc replace dev $swp5 root handle 1: \
	   ets bands 8 strict 8 priomap 7 6
	# ... so that we can assign prio1 traffic to PG1.
	dcb buffer set dev $swp5 prio-buffer all:0 1:1

	# bridges
	# -------

	ip link add name br1 type bridge vlan_filtering 0
	ip link set dev $swp1.111 master br1
	ip link set dev $swp3.111 master br1
	ip link set dev br1 up

	ip link add name br2 type bridge vlan_filtering 0
	ip link set dev $swp2.111 master br2
	ip link set dev $swp4.111 master br2
	ip link set dev $swp5.111 master br2
	ip link set dev br2 up
}

switch_destroy()
{
	# Do this first so that we can reset the limits to values that are only
	# valid for the original static / dynamic setting.
	devlink_pool_size_thtype_restore 6
	devlink_pool_size_thtype_restore 5
	devlink_pool_size_thtype_restore 4
	devlink_pool_size_thtype_restore 2
	devlink_pool_size_thtype_restore 1
	devlink_pool_size_thtype_restore 0

	# bridges
	# -------

	ip link set dev br2 down
	ip link set dev $swp5.111 nomaster
	ip link set dev $swp4.111 nomaster
	ip link set dev $swp2.111 nomaster
	ip link del dev br2

	ip link set dev br1 down
	ip link set dev $swp3.111 nomaster
	ip link set dev $swp1.111 nomaster
	ip link del dev br1

	# $swp5
	# -----

	dcb buffer set dev $swp5 prio-buffer all:0
	tc qdisc del dev $swp5 root

	devlink_tc_bind_pool_th_restore $swp5 1 ingress
	devlink_port_pool_th_restore $swp5 2

	vlan_destroy $swp5 111
	mtu_restore $swp5
	ip link set dev $swp5 down

	# $swp4
	# -----

	dcb buffer set dev $swp4 prio-buffer all:0
	tc qdisc del dev $swp4 root

	devlink_tc_bind_pool_th_restore $swp4 1 ingress
	devlink_port_pool_th_restore $swp4 2

	vlan_destroy $swp4 111
	ethtool -s $swp4 autoneg on
	mtu_restore $swp4
	ip link set dev $swp4 down

	# $swp3
	# -----

	tc qdisc del dev $swp3 root

	devlink_tc_bind_pool_th_restore $swp3 1 egress
	devlink_port_pool_th_restore $swp3 5

	vlan_destroy $swp3 111
	ethtool -s $swp3 autoneg on
	mtu_restore $swp3
	ip link set dev $swp3 down

	# $swp2
	# -----

	tc qdisc del dev $swp2 root

	devlink_tc_bind_pool_th_restore $swp2 1 egress
	devlink_port_pool_th_restore $swp2 6

	vlan_destroy $swp2 111
	ethtool -s $swp2 autoneg on
	mtu_restore $swp2
	ip link set dev $swp2 down

	# $swp1
	# -----

	dcb buffer set dev $swp1 prio-buffer all:0
	tc qdisc del dev $swp1 root

	devlink_tc_bind_pool_th_restore $swp1 1 ingress
	devlink_port_pool_th_restore $swp1 1

	vlan_destroy $swp1 111
	mtu_restore $swp1
	ip link set dev $swp1 down
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	swp3=${NETIFS[p5]}
	swp4=${NETIFS[p6]}

	swp5=${NETIFS[p7]}
	h3=${NETIFS[p8]}

	h2mac=$(mac_get $h2)

	vrf_prepare

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

	vrf_cleanup
}

ping_ipv4()
{
	ping_test $h1 192.0.2.34 " h1->h2"
	ping_test $h3 192.0.2.34 " h3->h2"
}

__test_qos_burst()
{
	local pktsize=$1; shift

	RET=0

	start_traffic_pktsize $pktsize $h1.111 192.0.2.33 192.0.2.34 $h2mac
	sleep 1

	local q0=$(ethtool_stats_get $swp2 tc_transmit_queue_tc_1)
	((q0 == 0))
	check_err $? "Transmit queue non-zero?"

	local d0=$(ethtool_stats_get $swp2 tc_no_buffer_discard_uc_tc_1)

	local cell_size=$(devlink_cell_size_get)
	local cells=$((BURST_SIZE / cell_size))
	# Each packet is $pktsize of payload + headers.
	local pkt_cells=$(((pktsize + 50 + cell_size - 1)  / cell_size))
	# How many packets can we admit:
	local pkts=$((cells / pkt_cells))

	$MZ $h3 -p $pktsize -Q 1:111 -A 192.0.2.35 -B 192.0.2.34 \
		-a own -b $h2mac -c $pkts -t udp -q
	sleep 1

	local d1=$(ethtool_stats_get $swp2 tc_no_buffer_discard_uc_tc_1)
	((d1 == d0))
	check_err $? "Drops seen on egress port: $d0 -> $d1 ($((d1 - d0)))"

	# Check that the queue is somewhat close to the burst size This
	# makes sure that the lack of drops above was not due to port
	# undersubscribtion.
	local q0=$(ethtool_stats_get $swp2 tc_transmit_queue_tc_1)
	local qe=$((90 * BURST_SIZE / 100))
	((q0 > qe))
	check_err $? "Queue size expected >$qe, got $q0"

	stop_traffic
	sleep 2

	log_test "Burst: absorb $pkts ${pktsize}-B packets"
}

test_8K()
{
	__test_qos_burst 8000
}

test_800()
{
	__test_qos_burst 800
}

bail_on_lldpad

trap cleanup EXIT
setup_prepare
setup_wait
tests_run

exit $EXIT_STATUS
