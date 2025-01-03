#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test tc-police action.
#
# +---------------------------------+
# | H1 (vrf)                        |
# |    + $h1                        |
# |    | 192.0.2.1/24               |
# |    |                            |
# |    |  default via 192.0.2.2     |
# +----|----------------------------+
#      |
# +----|----------------------------------------------------------------------+
# | SW |                                                                      |
# |    + $rp1                                                                 |
# |        192.0.2.2/24                                                       |
# |                                                                           |
# |        198.51.100.2/24                           203.0.113.2/24           |
# |    + $rp2                                    + $rp3                       |
# |    |                                         |                            |
# +----|-----------------------------------------|----------------------------+
#      |                                         |
# +----|----------------------------+       +----|----------------------------+
# |    |  default via 198.51.100.2  |       |    |  default via 203.0.113.2   |
# |    |                            |       |    |                            |
# |    | 198.51.100.1/24            |       |    | 203.0.113.1/24             |
# |    + $h2                        |       |    + $h3                        |
# | H2 (vrf)                        |       | H3 (vrf)                        |
# +---------------------------------+       +---------------------------------+

ALL_TESTS="
	police_rx_test
	police_tx_test
	police_shared_test
	police_rx_mirror_test
	police_tx_mirror_test
	police_pps_rx_test
	police_pps_tx_test
	police_mtu_rx_test
	police_mtu_tx_test
"
NUM_NETIFS=6
source tc_common.sh
source lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/24

	ip -4 route add default vrf v$h1 nexthop via 192.0.2.2
}

h1_destroy()
{
	ip -4 route del default vrf v$h1 nexthop via 192.0.2.2

	simple_if_fini $h1 192.0.2.1/24
}

h2_create()
{
	simple_if_init $h2 198.51.100.1/24

	ip -4 route add default vrf v$h2 nexthop via 198.51.100.2

	tc qdisc add dev $h2 clsact
}

h2_destroy()
{
	tc qdisc del dev $h2 clsact

	ip -4 route del default vrf v$h2 nexthop via 198.51.100.2

	simple_if_fini $h2 198.51.100.1/24
}

h3_create()
{
	simple_if_init $h3 203.0.113.1/24

	ip -4 route add default vrf v$h3 nexthop via 203.0.113.2

	tc qdisc add dev $h3 clsact
}

h3_destroy()
{
	tc qdisc del dev $h3 clsact

	ip -4 route del default vrf v$h3 nexthop via 203.0.113.2

	simple_if_fini $h3 203.0.113.1/24
}

router_create()
{
	ip link set dev $rp1 up
	ip link set dev $rp2 up
	ip link set dev $rp3 up

	__addr_add_del $rp1 add 192.0.2.2/24
	__addr_add_del $rp2 add 198.51.100.2/24
	__addr_add_del $rp3 add 203.0.113.2/24

	tc qdisc add dev $rp1 clsact
	tc qdisc add dev $rp2 clsact
}

router_destroy()
{
	tc qdisc del dev $rp2 clsact
	tc qdisc del dev $rp1 clsact

	__addr_add_del $rp3 del 203.0.113.2/24
	__addr_add_del $rp2 del 198.51.100.2/24
	__addr_add_del $rp1 del 192.0.2.2/24

	ip link set dev $rp3 down
	ip link set dev $rp2 down
	ip link set dev $rp1 down
}

police_common_test()
{
	local test_name=$1; shift

	RET=0

	# Rule to measure bandwidth on ingress of $h2
	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		dst_ip 198.51.100.1 ip_proto udp dst_port 54321 \
		action drop

	mausezahn $h1 -a own -b $(mac_get $rp1) -A 192.0.2.1 -B 198.51.100.1 \
		-t udp sp=12345,dp=54321 -p 1000 -c 0 -q &

	local t0=$(tc_rule_stats_get $h2 1 ingress .bytes)
	sleep 10
	local t1=$(tc_rule_stats_get $h2 1 ingress .bytes)

	local er=$((10 * 1000 * 1000))
	local nr=$(rate $t0 $t1 10)
	local nr_pct=$((100 * (nr - er) / er))
	((-10 <= nr_pct && nr_pct <= 10))
	check_err $? "Expected rate $(humanize $er), got $(humanize $nr), which is $nr_pct% off. Required accuracy is +-10%."

	log_test "$test_name"

	kill_process %%
	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower
}

police_rx_test()
{
	# Rule to police traffic destined to $h2 on ingress of $rp1
	tc filter add dev $rp1 ingress protocol ip pref 1 handle 101 flower \
		dst_ip 198.51.100.1 ip_proto udp dst_port 54321 \
		action police rate 10mbit burst 16k conform-exceed drop/ok

	police_common_test "police on rx"

	tc filter del dev $rp1 ingress protocol ip pref 1 handle 101 flower
}

police_tx_test()
{
	# Rule to police traffic destined to $h2 on egress of $rp2
	tc filter add dev $rp2 egress protocol ip pref 1 handle 101 flower \
		dst_ip 198.51.100.1 ip_proto udp dst_port 54321 \
		action police rate 10mbit burst 16k conform-exceed drop/ok

	police_common_test "police on tx"

	tc filter del dev $rp2 egress protocol ip pref 1 handle 101 flower
}

police_shared_common_test()
{
	local dport=$1; shift
	local test_name=$1; shift

	RET=0

	mausezahn $h1 -a own -b $(mac_get $rp1) -A 192.0.2.1 -B 198.51.100.1 \
		-t udp sp=12345,dp=$dport -p 1000 -c 0 -q &

	local t0=$(tc_rule_stats_get $h2 1 ingress .bytes)
	sleep 10
	local t1=$(tc_rule_stats_get $h2 1 ingress .bytes)

	local er=$((10 * 1000 * 1000))
	local nr=$(rate $t0 $t1 10)
	local nr_pct=$((100 * (nr - er) / er))
	((-10 <= nr_pct && nr_pct <= 10))
	check_err $? "Expected rate $(humanize $er), got $(humanize $nr), which is $nr_pct% off. Required accuracy is +-10%."

	log_test "$test_name"

	kill_process %%
}

police_shared_test()
{
	# Rule to measure bandwidth on ingress of $h2
	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		dst_ip 198.51.100.1 ip_proto udp src_port 12345 \
		action drop

	# Rule to police traffic destined to $h2 on ingress of $rp1
	tc filter add dev $rp1 ingress protocol ip pref 1 handle 101 flower \
		dst_ip 198.51.100.1 ip_proto udp dst_port 54321 \
		action police rate 10mbit burst 16k conform-exceed drop/ok \
		index 10

	# Rule to police a different flow destined to $h2 on egress of $rp2
	# using same policer
	tc filter add dev $rp2 egress protocol ip pref 1 handle 101 flower \
		dst_ip 198.51.100.1 ip_proto udp dst_port 22222 \
		action police index 10

	police_shared_common_test 54321 "police with shared policer - rx"

	police_shared_common_test 22222 "police with shared policer - tx"

	tc filter del dev $rp2 egress protocol ip pref 1 handle 101 flower
	tc filter del dev $rp1 ingress protocol ip pref 1 handle 101 flower
	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower
}

police_mirror_common_test()
{
	local pol_if=$1; shift
	local dir=$1; shift
	local test_name=$1; shift

	RET=0

	# Rule to measure bandwidth on ingress of $h2
	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		dst_ip 198.51.100.1 ip_proto udp dst_port 54321 \
		action drop

	# Rule to measure bandwidth of mirrored traffic on ingress of $h3
	tc filter add dev $h3 ingress protocol ip pref 1 handle 101 flower \
		dst_ip 198.51.100.1 ip_proto udp dst_port 54321 \
		action drop

	# Rule to police traffic destined to $h2 and mirror to $h3
	tc filter add dev $pol_if $dir protocol ip pref 1 handle 101 flower \
		dst_ip 198.51.100.1 ip_proto udp dst_port 54321 \
		action police rate 10mbit burst 16k conform-exceed drop/pipe \
		action mirred egress mirror dev $rp3

	mausezahn $h1 -a own -b $(mac_get $rp1) -A 192.0.2.1 -B 198.51.100.1 \
		-t udp sp=12345,dp=54321 -p 1000 -c 0 -q &

	local t0=$(tc_rule_stats_get $h2 1 ingress .bytes)
	sleep 10
	local t1=$(tc_rule_stats_get $h2 1 ingress .bytes)

	local er=$((10 * 1000 * 1000))
	local nr=$(rate $t0 $t1 10)
	local nr_pct=$((100 * (nr - er) / er))
	((-10 <= nr_pct && nr_pct <= 10))
	check_err $? "Expected rate $(humanize $er), got $(humanize $nr), which is $nr_pct% off. Required accuracy is +-10%."

	local t0=$(tc_rule_stats_get $h3 1 ingress .bytes)
	sleep 10
	local t1=$(tc_rule_stats_get $h3 1 ingress .bytes)

	local er=$((10 * 1000 * 1000))
	local nr=$(rate $t0 $t1 10)
	local nr_pct=$((100 * (nr - er) / er))
	((-10 <= nr_pct && nr_pct <= 10))
	check_err $? "Expected rate $(humanize $er), got $(humanize $nr), which is $nr_pct% off. Required accuracy is +-10%."

	log_test "$test_name"

	kill_process %%
	tc filter del dev $pol_if $dir protocol ip pref 1 handle 101 flower
	tc filter del dev $h3 ingress protocol ip pref 1 handle 101 flower
	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower
}

police_rx_mirror_test()
{
	police_mirror_common_test $rp1 ingress "police rx and mirror"
}

police_tx_mirror_test()
{
	police_mirror_common_test $rp2 egress "police tx and mirror"
}

police_pps_common_test()
{
	local test_name=$1; shift

	RET=0

	# Rule to measure bandwidth on ingress of $h2
	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		dst_ip 198.51.100.1 ip_proto udp dst_port 54321 \
		action drop

	mausezahn $h1 -a own -b $(mac_get $rp1) -A 192.0.2.1 -B 198.51.100.1 \
		-t udp sp=12345,dp=54321 -p 1000 -c 0 -q &

	local t0=$(tc_rule_stats_get $h2 1 ingress .packets)
	sleep 10
	local t1=$(tc_rule_stats_get $h2 1 ingress .packets)

	local er=$((2000))
	local nr=$(packets_rate $t0 $t1 10)
	local nr_pct=$((100 * (nr - er) / er))
	((-10 <= nr_pct && nr_pct <= 10))
	check_err $? "Expected rate $(humanize $er), got $(humanize $nr), which is $nr_pct% off. Required accuracy is +-10%."

	log_test "$test_name"

	kill_process %%
	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower
}

police_pps_rx_test()
{
	# Rule to police traffic destined to $h2 on ingress of $rp1
	tc filter add dev $rp1 ingress protocol ip pref 1 handle 101 flower \
		dst_ip 198.51.100.1 ip_proto udp dst_port 54321 \
		action police pkts_rate 2000 pkts_burst 400 conform-exceed drop/ok

	police_pps_common_test "police pps on rx"

	tc filter del dev $rp1 ingress protocol ip pref 1 handle 101 flower
}

police_pps_tx_test()
{
	# Rule to police traffic destined to $h2 on egress of $rp2
	tc filter add dev $rp2 egress protocol ip pref 1 handle 101 flower \
		dst_ip 198.51.100.1 ip_proto udp dst_port 54321 \
		action police pkts_rate 2000 pkts_burst 400 conform-exceed drop/ok

	police_pps_common_test "police pps on tx"

	tc filter del dev $rp2 egress protocol ip pref 1 handle 101 flower
}

police_mtu_common_test() {
	RET=0

	local test_name=$1; shift
	local dev=$1; shift
	local direction=$1; shift

	tc filter add dev $dev $direction protocol ip pref 1 handle 101 flower \
		dst_ip 198.51.100.1 ip_proto udp dst_port 54321 \
		action police mtu 1042 conform-exceed drop/ok

	# to count "conform" packets
	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		dst_ip 198.51.100.1 ip_proto udp dst_port 54321 \
		action drop

	mausezahn $h1 -a own -b $(mac_get $rp1) -A 192.0.2.1 -B 198.51.100.1 \
		-t udp sp=12345,dp=54321 -p 1001 -c 10 -q

	mausezahn $h1 -a own -b $(mac_get $rp1) -A 192.0.2.1 -B 198.51.100.1 \
		-t udp sp=12345,dp=54321 -p 1000 -c 3 -q

	tc_check_packets "dev $dev $direction" 101 13
	check_err $? "wrong packet counter"

	# "exceed" packets
	local overlimits_t0=$(tc_rule_stats_get ${dev} 1 ${direction} .overlimits)
	test ${overlimits_t0} = 10
	check_err $? "wrong overlimits, expected 10 got ${overlimits_t0}"

	# "conform" packets
	tc_check_packets "dev $h2 ingress" 101 3
	check_err $? "forwarding error"

	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower
	tc filter del dev $dev $direction protocol ip pref 1 handle 101 flower

	log_test "$test_name"
}

police_mtu_rx_test()
{
	police_mtu_common_test "police mtu (rx)" $rp1 ingress
}

police_mtu_tx_test()
{
	police_mtu_common_test "police mtu (tx)" $rp2 egress
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	rp1=${NETIFS[p2]}

	rp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	rp3=${NETIFS[p5]}
	h3=${NETIFS[p6]}

	vrf_prepare
	forwarding_enable

	h1_create
	h2_create
	h3_create
	router_create
}

cleanup()
{
	pre_cleanup

	router_destroy
	h3_destroy
	h2_destroy
	h1_destroy

	forwarding_restore
	vrf_cleanup
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
