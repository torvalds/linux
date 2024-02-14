#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A test for strict prioritization of traffic in the switch. Run two streams of
# traffic, each through a different ingress port, one tagged with PCP of 1, the
# other with PCP of 2. Both streams converge at one egress port, where they are
# assigned TC of, respectively, 1 and 2, with strict priority configured between
# them. In H3, we expect to see (almost) exclusively the high-priority traffic.
#
# Please see qos_mc_aware.sh for an explanation of why we use mausezahn and
# counters instead of just running iperf3.
#
# +---------------------------+                 +-----------------------------+
# | H1                        |                 |                          H2 |
# |         $h1.111 +         |                 |         + $h2.222           |
# |   192.0.2.33/28 |         |                 |         | 192.0.2.65/28     |
# |   e-qos-map 0:1 |         |                 |         | e-qos-map 0:2     |
# |                 |         |                 |         |                   |
# |             $h1 +         |                 |         + $h2               |
# +-----------------|---------+                 +---------|-------------------+
#                   |                                     |
# +-----------------|-------------------------------------|-------------------+
# |           $swp1 +                                     + $swp2             |
# |          >1Gbps |                                     | >1Gbps            |
# | +---------------|-----------+              +----------|----------------+  |
# | |     $swp1.111 +           |              |          + $swp2.222      |  |
# | |                     BR111 |       SW     | BR222                     |  |
# | |     $swp3.111 +           |              |          + $swp3.222      |  |
# | +---------------|-----------+              +----------|----------------+  |
# |                 \_____________________________________/                   |
# |                                    |                                      |
# |                                    + $swp3                                |
# |                                    | 1Gbps bottleneck                     |
# |                                    | ETS: (up n->tc n for n in 0..7)      |
# |                                    |      strict priority                 |
# +------------------------------------|--------------------------------------+
#                                      |
#                 +--------------------|--------------------+
#                 |                    + $h3             H3 |
#                 |                   / \                   |
#                 |                  /   \                  |
#                 |         $h3.111 +     + $h3.222         |
#                 |  192.0.2.34/28          192.0.2.66/28   |
#                 +-----------------------------------------+

ALL_TESTS="
	ping_ipv4
	test_ets_strict
"

lib_dir=$(dirname $0)/../../../net/forwarding

NUM_NETIFS=6
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh
source qos_lib.sh

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

	vlan_create $h2 222 v$h2 192.0.2.65/28
	ip link set dev $h2.222 type vlan egress-qos-map 0:2
}

h2_destroy()
{
	vlan_destroy $h2 222

	mtu_restore $h2
	simple_if_fini $h2
}

h3_create()
{
	simple_if_init $h3
	mtu_set $h3 10000

	vlan_create $h3 111 v$h3 192.0.2.34/28
	vlan_create $h3 222 v$h3 192.0.2.66/28
}

h3_destroy()
{
	vlan_destroy $h3 222
	vlan_destroy $h3 111

	mtu_restore $h3
	simple_if_fini $h3
}

switch_create()
{
	ip link set dev $swp1 up
	mtu_set $swp1 10000

	ip link set dev $swp2 up
	mtu_set $swp2 10000

	# prio n -> TC n, strict scheduling
	lldptool -T -i $swp3 -V ETS-CFG up2tc=0:0,1:1,2:2,3:3,4:4,5:5,6:6,7:7
	lldptool -T -i $swp3 -V ETS-CFG tsa=$(
			)"0:strict,"$(
			)"1:strict,"$(
			)"2:strict,"$(
			)"3:strict,"$(
			)"4:strict,"$(
			)"5:strict,"$(
			)"6:strict,"$(
			)"7:strict"
	sleep 1

	ip link set dev $swp3 up
	mtu_set $swp3 10000
	tc qdisc replace dev $swp3 root handle 101: tbf rate 1gbit \
		burst 128K limit 1G

	vlan_create $swp1 111
	vlan_create $swp2 222
	vlan_create $swp3 111
	vlan_create $swp3 222

	ip link add name br111 up type bridge vlan_filtering 0
	ip link set dev $swp1.111 master br111
	ip link set dev $swp3.111 master br111

	ip link add name br222 up type bridge vlan_filtering 0
	ip link set dev $swp2.222 master br222
	ip link set dev $swp3.222 master br222

	# Make sure that ingress quotas are smaller than egress so that there is
	# room for both streams of traffic to be admitted to shared buffer.
	devlink_pool_size_thtype_save 0
	devlink_pool_size_thtype_set 0 dynamic 10000000
	devlink_pool_size_thtype_save 4
	devlink_pool_size_thtype_set 4 dynamic 10000000

	devlink_port_pool_th_save $swp1 0
	devlink_port_pool_th_set $swp1 0 6
	devlink_tc_bind_pool_th_save $swp1 1 ingress
	devlink_tc_bind_pool_th_set $swp1 1 ingress 0 6

	devlink_port_pool_th_save $swp2 0
	devlink_port_pool_th_set $swp2 0 6
	devlink_tc_bind_pool_th_save $swp2 2 ingress
	devlink_tc_bind_pool_th_set $swp2 2 ingress 0 6

	devlink_tc_bind_pool_th_save $swp3 1 egress
	devlink_tc_bind_pool_th_set $swp3 1 egress 4 7
	devlink_tc_bind_pool_th_save $swp3 2 egress
	devlink_tc_bind_pool_th_set $swp3 2 egress 4 7
	devlink_port_pool_th_save $swp3 4
	devlink_port_pool_th_set $swp3 4 7
}

switch_destroy()
{
	devlink_port_pool_th_restore $swp3 4
	devlink_tc_bind_pool_th_restore $swp3 2 egress
	devlink_tc_bind_pool_th_restore $swp3 1 egress

	devlink_tc_bind_pool_th_restore $swp2 2 ingress
	devlink_port_pool_th_restore $swp2 0

	devlink_tc_bind_pool_th_restore $swp1 1 ingress
	devlink_port_pool_th_restore $swp1 0

	devlink_pool_size_thtype_restore 4
	devlink_pool_size_thtype_restore 0

	ip link del dev br222
	ip link del dev br111

	vlan_destroy $swp3 222
	vlan_destroy $swp3 111
	vlan_destroy $swp2 222
	vlan_destroy $swp1 111

	tc qdisc del dev $swp3 root handle 101:
	mtu_restore $swp3
	ip link set dev $swp3 down
	lldptool -T -i $swp3 -V ETS-CFG up2tc=0:0,1:0,2:0,3:0,4:0,5:0,6:0,7:0

	mtu_restore $swp2
	ip link set dev $swp2 down

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
	h3=${NETIFS[p6]}

	h3mac=$(mac_get $h3)

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
	ping_test $h1 192.0.2.34 " from H1"
	ping_test $h2 192.0.2.66 " from H2"
}

rel()
{
	local old=$1; shift
	local new=$1; shift

	bc <<< "
	    scale=2
	    ret = 100 * $new / $old
	    if (ret > 0) { ret } else { 0 }
	"
}

test_ets_strict()
{
	RET=0

	# Run high-prio traffic on its own.
	start_traffic $h2.222 192.0.2.65 192.0.2.66 $h3mac
	local -a rate_2
	rate_2=($(measure_rate $swp2 $h3 rx_octets_prio_2 "prio 2"))
	check_err $? "Could not get high enough prio-2 ingress rate"
	local rate_2_in=${rate_2[0]}
	local rate_2_eg=${rate_2[1]}
	stop_traffic # $h2.222

	# Start low-prio stream.
	start_traffic $h1.111 192.0.2.33 192.0.2.34 $h3mac

	local -a rate_1
	rate_1=($(measure_rate $swp1 $h3 rx_octets_prio_1 "prio 1"))
	check_err $? "Could not get high enough prio-1 ingress rate"
	local rate_1_in=${rate_1[0]}
	local rate_1_eg=${rate_1[1]}

	# High-prio and low-prio on their own should have about the same
	# throughput.
	local rel21=$(rel $rate_1_eg $rate_2_eg)
	check_err $(bc <<< "$rel21 < 95")
	check_err $(bc <<< "$rel21 > 105")

	# Start the high-prio stream--now both streams run.
	start_traffic $h2.222 192.0.2.65 192.0.2.66 $h3mac
	rate_3=($(measure_rate $swp2 $h3 rx_octets_prio_2 "prio 2 w/ 1"))
	check_err $? "Could not get high enough prio-2 ingress rate with prio-1"
	local rate_3_in=${rate_3[0]}
	local rate_3_eg=${rate_3[1]}
	stop_traffic # $h2.222

	stop_traffic # $h1.111

	# High-prio should have about the same throughput whether or not
	# low-prio is in the system.
	local rel32=$(rel $rate_2_eg $rate_3_eg)
	check_err $(bc <<< "$rel32 < 95")

	log_test "strict priority"
	echo "Ingress to switch:"
	echo "  p1 in rate            $(humanize $rate_1_in)"
	echo "  p2 in rate            $(humanize $rate_2_in)"
	echo "  p2 in rate w/ p1      $(humanize $rate_3_in)"
	echo "Egress from switch:"
	echo "  p1 eg rate            $(humanize $rate_1_eg)"
	echo "  p2 eg rate            $(humanize $rate_2_eg) ($rel21% of p1)"
	echo "  p2 eg rate w/ p1      $(humanize $rate_3_eg) ($rel32% of p2)"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
