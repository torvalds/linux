#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test that packets are sampled when tc-sample is used and that reported
# metadata is correct. Two sets of hosts (with and without LAG) are used, since
# metadata extraction in mlxsw is a bit different when LAG is involved.
#
# +---------------------------------+       +---------------------------------+
# | H1 (vrf)                        |       | H3 (vrf)                        |
# |    + $h1                        |       |    + $h3_lag                    |
# |    | 192.0.2.1/28               |       |    | 192.0.2.17/28              |
# |    |                            |       |    |                            |
# |    |  default via 192.0.2.2     |       |    |  default via 192.0.2.18    |
# +----|----------------------------+       +----|----------------------------+
#      |                                         |
# +----|-----------------------------------------|----------------------------+
# |    | 192.0.2.2/28                            | 192.0.2.18/28              |
# |    + $rp1                                    + $rp3_lag                   |
# |                                                                           |
# |    + $rp2                                    + $rp4_lag                   |
# |    | 198.51.100.2/28                         | 198.51.100.18/28           |
# +----|-----------------------------------------|----------------------------+
#      |                                         |
# +----|----------------------------+       +----|----------------------------+
# |    |  default via 198.51.100.2  |       |    |  default via 198.51.100.18 |
# |    |                            |       |    |                            |
# |    | 198.51.100.1/28            |       |    | 198.51.100.17/28           |
# |    + $h2                        |       |    + $h4_lag                    |
# | H2 (vrf)                        |       | H4 (vrf)                        |
# +---------------------------------+       +---------------------------------+

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	tc_sample_rate_test
	tc_sample_max_rate_test
	tc_sample_conflict_test
	tc_sample_group_conflict_test
	tc_sample_md_iif_test
	tc_sample_md_lag_iif_test
	tc_sample_md_oif_test
	tc_sample_md_lag_oif_test
	tc_sample_md_out_tc_test
	tc_sample_md_out_tc_occ_test
	tc_sample_md_latency_test
	tc_sample_acl_group_conflict_test
	tc_sample_acl_rate_test
	tc_sample_acl_max_rate_test
"
NUM_NETIFS=8
CAPTURE_FILE=$(mktemp)
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh
source mlxsw_lib.sh

# Available at https://github.com/Mellanox/libpsample
require_command psample

h1_create()
{
	simple_if_init $h1 192.0.2.1/28

	ip -4 route add default vrf v$h1 nexthop via 192.0.2.2
}

h1_destroy()
{
	ip -4 route del default vrf v$h1 nexthop via 192.0.2.2

	simple_if_fini $h1 192.0.2.1/28
}

h2_create()
{
	simple_if_init $h2 198.51.100.1/28

	ip -4 route add default vrf v$h2 nexthop via 198.51.100.2
}

h2_destroy()
{
	ip -4 route del default vrf v$h2 nexthop via 198.51.100.2

	simple_if_fini $h2 198.51.100.1/28
}

h3_create()
{
	ip link set dev $h3 down
	ip link add name ${h3}_bond type bond mode 802.3ad
	ip link set dev $h3 master ${h3}_bond

	simple_if_init ${h3}_bond 192.0.2.17/28

	ip -4 route add default vrf v${h3}_bond nexthop via 192.0.2.18
}

h3_destroy()
{
	ip -4 route del default vrf v${h3}_bond nexthop via 192.0.2.18

	simple_if_fini ${h3}_bond 192.0.2.17/28

	ip link set dev $h3 nomaster
	ip link del dev ${h3}_bond
}

h4_create()
{
	ip link set dev $h4 down
	ip link add name ${h4}_bond type bond mode 802.3ad
	ip link set dev $h4 master ${h4}_bond

	simple_if_init ${h4}_bond 198.51.100.17/28

	ip -4 route add default vrf v${h4}_bond nexthop via 198.51.100.18
}

h4_destroy()
{
	ip -4 route del default vrf v${h4}_bond nexthop via 198.51.100.18

	simple_if_fini ${h4}_bond 198.51.100.17/28

	ip link set dev $h4 nomaster
	ip link del dev ${h4}_bond
}

router_create()
{
	ip link set dev $rp1 up
	__addr_add_del $rp1 add 192.0.2.2/28
	tc qdisc add dev $rp1 clsact

	ip link set dev $rp2 up
	__addr_add_del $rp2 add 198.51.100.2/28
	tc qdisc add dev $rp2 clsact

	ip link add name ${rp3}_bond type bond mode 802.3ad
	ip link set dev $rp3 master ${rp3}_bond
	__addr_add_del ${rp3}_bond add 192.0.2.18/28
	tc qdisc add dev $rp3 clsact
	ip link set dev ${rp3}_bond up

	ip link add name ${rp4}_bond type bond mode 802.3ad
	ip link set dev $rp4 master ${rp4}_bond
	__addr_add_del ${rp4}_bond add 198.51.100.18/28
	tc qdisc add dev $rp4 clsact
	ip link set dev ${rp4}_bond up
}

router_destroy()
{
	ip link set dev ${rp4}_bond down
	tc qdisc del dev $rp4 clsact
	__addr_add_del ${rp4}_bond del 198.51.100.18/28
	ip link set dev $rp4 nomaster
	ip link del dev ${rp4}_bond

	ip link set dev ${rp3}_bond down
	tc qdisc del dev $rp3 clsact
	__addr_add_del ${rp3}_bond del 192.0.2.18/28
	ip link set dev $rp3 nomaster
	ip link del dev ${rp3}_bond

	tc qdisc del dev $rp2 clsact
	__addr_add_del $rp2 del 198.51.100.2/28
	ip link set dev $rp2 down

	tc qdisc del dev $rp1 clsact
	__addr_add_del $rp1 del 192.0.2.2/28
	ip link set dev $rp1 down
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	rp1=${NETIFS[p2]}
	rp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}
	h3=${NETIFS[p5]}
	rp3=${NETIFS[p6]}
	h4=${NETIFS[p7]}
	rp4=${NETIFS[p8]}

	vrf_prepare

	h1_create
	h2_create
	h3_create
	h4_create
	router_create
}

cleanup()
{
	pre_cleanup

	rm -f $CAPTURE_FILE

	router_destroy
	h4_destroy
	h3_destroy
	h2_destroy
	h1_destroy

	vrf_cleanup
}

psample_capture_start()
{
	rm -f $CAPTURE_FILE

	psample &> $CAPTURE_FILE &

	sleep 1
}

psample_capture_stop()
{
	{ kill %% && wait %%; } 2>/dev/null
}

__tc_sample_rate_test()
{
	local desc=$1; shift
	local dip=$1; shift
	local pkts pct

	RET=0

	tc filter add dev $rp1 ingress protocol all pref 1 handle 101 matchall \
		skip_sw action sample rate 32 group 1
	check_err $? "Failed to configure sampling rule"

	psample_capture_start

	ip vrf exec v$h1 $MZ $h1 -c 320000 -d 100usec -p 64 -A 192.0.2.1 \
		-B $dip -t udp dp=52768,sp=42768 -q

	psample_capture_stop

	pkts=$(grep -e "group 1 " $CAPTURE_FILE | wc -l)
	pct=$((100 * (pkts - 10000) / 10000))
	(( -25 <= pct && pct <= 25))
	check_err $? "Expected 10000 packets, got $pkts packets, which is $pct% off. Required accuracy is +-25%"

	log_test "tc sample rate ($desc)"

	tc filter del dev $rp1 ingress protocol all pref 1 handle 101 matchall
}

tc_sample_rate_test()
{
	__tc_sample_rate_test "forward" 198.51.100.1
	__tc_sample_rate_test "local receive" 192.0.2.2
}

tc_sample_max_rate_test()
{
	RET=0

	tc filter add dev $rp1 ingress protocol all pref 1 handle 101 matchall \
		skip_sw action sample rate $((35 * 10 ** 8)) group 1
	check_err $? "Failed to configure sampling rule with max rate"

	tc filter del dev $rp1 ingress protocol all pref 1 handle 101 matchall

	tc filter add dev $rp1 ingress protocol all pref 1 handle 101 matchall \
		skip_sw action sample rate $((35 * 10 ** 8 + 1)) \
		group 1 &> /dev/null
	check_fail $? "Managed to configure sampling rate above maximum"

	log_test "tc sample maximum rate"
}

tc_sample_conflict_test()
{
	RET=0

	# Test that two sampling rules cannot be configured on the same port,
	# even when they share the same parameters.

	tc filter add dev $rp1 ingress protocol all pref 1 handle 101 matchall \
		skip_sw action sample rate 1024 group 1
	check_err $? "Failed to configure sampling rule"

	tc filter add dev $rp1 ingress protocol all pref 2 handle 102 matchall \
		skip_sw action sample rate 1024 group 1 &> /dev/null
	check_fail $? "Managed to configure second sampling rule"

	# Delete the first rule and make sure the second rule can now be
	# configured.

	tc filter del dev $rp1 ingress protocol all pref 1 handle 101 matchall

	tc filter add dev $rp1 ingress protocol all pref 2 handle 102 matchall \
		skip_sw action sample rate 1024 group 1
	check_err $? "Failed to configure sampling rule after deletion"

	log_test "tc sample conflict test"

	tc filter del dev $rp1 ingress protocol all pref 2 handle 102 matchall
}

tc_sample_group_conflict_test()
{
	RET=0

	# Test that two sampling rules cannot be configured on the same port
	# with different groups.

	tc filter add dev $rp1 ingress protocol all pref 1 handle 101 matchall \
		skip_sw action sample rate 1024 group 1
	check_err $? "Failed to configure sampling rule"

	tc filter add dev $rp1 ingress protocol all pref 2 handle 102 matchall \
		skip_sw action sample rate 1024 group 2 &> /dev/null
	check_fail $? "Managed to configure sampling rule with conflicting group"

	log_test "tc sample group conflict test"

	tc filter del dev $rp1 ingress protocol all pref 1 handle 101 matchall
}

tc_sample_md_iif_test()
{
	local rp1_ifindex

	RET=0

	tc filter add dev $rp1 ingress protocol all pref 1 handle 101 matchall \
		skip_sw action sample rate 5 group 1
	check_err $? "Failed to configure sampling rule"

	psample_capture_start

	ip vrf exec v$h1 $MZ $h1 -c 3200 -d 1msec -p 64 -A 192.0.2.1 \
		-B 198.51.100.1 -t udp dp=52768,sp=42768 -q

	psample_capture_stop

	rp1_ifindex=$(ip -j -p link show dev $rp1 | jq '.[]["ifindex"]')
	grep -q -e "in-ifindex $rp1_ifindex " $CAPTURE_FILE
	check_err $? "Sampled packets do not have expected in-ifindex"

	log_test "tc sample iif"

	tc filter del dev $rp1 ingress protocol all pref 1 handle 101 matchall
}

tc_sample_md_lag_iif_test()
{
	local rp3_ifindex

	RET=0

	tc filter add dev $rp3 ingress protocol all pref 1 handle 101 matchall \
		skip_sw action sample rate 5 group 1
	check_err $? "Failed to configure sampling rule"

	psample_capture_start

	ip vrf exec v${h3}_bond $MZ ${h3}_bond -c 3200 -d 1msec -p 64 \
		-A 192.0.2.17 -B 198.51.100.17 -t udp dp=52768,sp=42768 -q

	psample_capture_stop

	rp3_ifindex=$(ip -j -p link show dev $rp3 | jq '.[]["ifindex"]')
	grep -q -e "in-ifindex $rp3_ifindex " $CAPTURE_FILE
	check_err $? "Sampled packets do not have expected in-ifindex"

	log_test "tc sample lag iif"

	tc filter del dev $rp3 ingress protocol all pref 1 handle 101 matchall
}

tc_sample_md_oif_test()
{
	local rp2_ifindex

	RET=0

	tc filter add dev $rp1 ingress protocol all pref 1 handle 101 matchall \
		skip_sw action sample rate 5 group 1
	check_err $? "Failed to configure sampling rule"

	psample_capture_start

	ip vrf exec v$h1 $MZ $h1 -c 3200 -d 1msec -p 64 -A 192.0.2.1 \
		-B 198.51.100.1 -t udp dp=52768,sp=42768 -q

	psample_capture_stop

	rp2_ifindex=$(ip -j -p link show dev $rp2 | jq '.[]["ifindex"]')
	grep -q -e "out-ifindex $rp2_ifindex " $CAPTURE_FILE
	check_err $? "Sampled packets do not have expected out-ifindex"

	log_test "tc sample oif"

	tc filter del dev $rp1 ingress protocol all pref 1 handle 101 matchall
}

tc_sample_md_lag_oif_test()
{
	local rp4_ifindex

	RET=0

	tc filter add dev $rp3 ingress protocol all pref 1 handle 101 matchall \
		skip_sw action sample rate 5 group 1
	check_err $? "Failed to configure sampling rule"

	psample_capture_start

	ip vrf exec v${h3}_bond $MZ ${h3}_bond -c 3200 -d 1msec -p 64 \
		-A 192.0.2.17 -B 198.51.100.17 -t udp dp=52768,sp=42768 -q

	psample_capture_stop

	rp4_ifindex=$(ip -j -p link show dev $rp4 | jq '.[]["ifindex"]')
	grep -q -e "out-ifindex $rp4_ifindex " $CAPTURE_FILE
	check_err $? "Sampled packets do not have expected out-ifindex"

	log_test "tc sample lag oif"

	tc filter del dev $rp3 ingress protocol all pref 1 handle 101 matchall
}

tc_sample_md_out_tc_test()
{
	RET=0

	# Output traffic class is not supported on Spectrum-1.
	mlxsw_only_on_spectrum 2+ || return

	tc filter add dev $rp1 ingress protocol all pref 1 handle 101 matchall \
		skip_sw action sample rate 5 group 1
	check_err $? "Failed to configure sampling rule"

	# By default, all the packets should go to the same traffic class (0).

	psample_capture_start

	ip vrf exec v$h1 $MZ $h1 -c 3200 -d 1msec -p 64 -A 192.0.2.1 \
		-B 198.51.100.1 -t udp dp=52768,sp=42768 -q

	psample_capture_stop

	grep -q -e "out-tc 0 " $CAPTURE_FILE
	check_err $? "Sampled packets do not have expected out-tc (0)"

	# Map all priorities to highest traffic class (7) and check reported
	# out-tc.
	tc qdisc replace dev $rp2 root handle 1: \
		prio bands 3 priomap 0 0 0 0 0 0 0 0

	psample_capture_start

	ip vrf exec v$h1 $MZ $h1 -c 3200 -d 1msec -p 64 -A 192.0.2.1 \
		-B 198.51.100.1 -t udp dp=52768,sp=42768 -q

	psample_capture_stop

	grep -q -e "out-tc 7 " $CAPTURE_FILE
	check_err $? "Sampled packets do not have expected out-tc (7)"

	log_test "tc sample out-tc"

	tc qdisc del dev $rp2 root handle 1:
	tc filter del dev $rp1 ingress protocol all pref 1 handle 101 matchall
}

tc_sample_md_out_tc_occ_test()
{
	local backlog pct occ

	RET=0

	# Output traffic class occupancy is not supported on Spectrum-1.
	mlxsw_only_on_spectrum 2+ || return

	tc filter add dev $rp1 ingress protocol all pref 1 handle 101 matchall \
		skip_sw action sample rate 1024 group 1
	check_err $? "Failed to configure sampling rule"

	# Configure a shaper on egress to create congestion.
	tc qdisc replace dev $rp2 root handle 1: \
		tbf rate 1Mbit burst 256k limit 1M

	psample_capture_start

	ip vrf exec v$h1 $MZ $h1 -c 0 -d 1usec -p 1400 -A 192.0.2.1 \
		-B 198.51.100.1 -t udp dp=52768,sp=42768 -q &

	# Allow congestion to reach steady state.
	sleep 10

	backlog=$(tc -j -p -s qdisc show dev $rp2 | jq '.[0]["backlog"]')

	# Kill mausezahn.
	{ kill %% && wait %%; } 2>/dev/null

	psample_capture_stop

	# Record last congestion sample.
	occ=$(grep -e "out-tc-occ " $CAPTURE_FILE | tail -n 1 | \
		cut -d ' ' -f 16)

	pct=$((100 * (occ - backlog) / backlog))
	(( -1 <= pct && pct <= 1))
	check_err $? "Recorded a congestion of $backlog bytes, but sampled congestion is $occ bytes, which is $pct% off. Required accuracy is +-5%"

	log_test "tc sample out-tc-occ"

	tc qdisc del dev $rp2 root handle 1:
	tc filter del dev $rp1 ingress protocol all pref 1 handle 101 matchall
}

tc_sample_md_latency_test()
{
	RET=0

	# Egress sampling not supported on Spectrum-1.
	mlxsw_only_on_spectrum 2+ || return

	tc filter add dev $rp2 egress protocol all pref 1 handle 101 matchall \
		skip_sw action sample rate 5 group 1
	check_err $? "Failed to configure sampling rule"

	psample_capture_start

	ip vrf exec v$h1 $MZ $h1 -c 3200 -d 1msec -p 64 -A 192.0.2.1 \
		-B 198.51.100.1 -t udp dp=52768,sp=42768 -q

	psample_capture_stop

	grep -q -e "latency " $CAPTURE_FILE
	check_err $? "Sampled packets do not have latency attribute"

	log_test "tc sample latency"

	tc filter del dev $rp2 egress protocol all pref 1 handle 101 matchall
}

tc_sample_acl_group_conflict_test()
{
	RET=0

	# Test that two flower sampling rules cannot be configured on the same
	# port with different groups.

	# Policy-based sampling is not supported on Spectrum-1.
	mlxsw_only_on_spectrum 2+ || return

	tc filter add dev $rp1 ingress protocol ip pref 1 handle 101 flower \
		skip_sw action sample rate 1024 group 1
	check_err $? "Failed to configure sampling rule"

	tc filter add dev $rp1 ingress protocol ip pref 2 handle 102 flower \
		skip_sw action sample rate 1024 group 1
	check_err $? "Failed to configure sampling rule with same group"

	tc filter add dev $rp1 ingress protocol ip pref 3 handle 103 flower \
		skip_sw action sample rate 1024 group 2 &> /dev/null
	check_fail $? "Managed to configure sampling rule with conflicting group"

	log_test "tc sample (w/ flower) group conflict test"

	tc filter del dev $rp1 ingress protocol ip pref 2 handle 102 flower
	tc filter del dev $rp1 ingress protocol ip pref 1 handle 101 flower
}

__tc_sample_acl_rate_test()
{
	local bind=$1; shift
	local port=$1; shift
	local pkts pct

	RET=0

	# Policy-based sampling is not supported on Spectrum-1.
	mlxsw_only_on_spectrum 2+ || return

	tc filter add dev $port $bind protocol ip pref 1 handle 101 flower \
		skip_sw dst_ip 198.51.100.1 action sample rate 32 group 1
	check_err $? "Failed to configure sampling rule"

	psample_capture_start

	ip vrf exec v$h1 $MZ $h1 -c 320000 -d 100usec -p 64 -A 192.0.2.1 \
		-B 198.51.100.1 -t udp dp=52768,sp=42768 -q

	psample_capture_stop

	pkts=$(grep -e "group 1 " $CAPTURE_FILE | wc -l)
	pct=$((100 * (pkts - 10000) / 10000))
	(( -25 <= pct && pct <= 25))
	check_err $? "Expected 10000 packets, got $pkts packets, which is $pct% off. Required accuracy is +-25%"

	# Setup a filter that should not match any packet and make sure packets
	# are not sampled.
	tc filter del dev $port $bind protocol ip pref 1 handle 101 flower

	tc filter add dev $port $bind protocol ip pref 1 handle 101 flower \
		skip_sw dst_ip 198.51.100.10 action sample rate 32 group 1
	check_err $? "Failed to configure sampling rule"

	psample_capture_start

	ip vrf exec v$h1 $MZ $h1 -c 3200 -d 1msec -p 64 -A 192.0.2.1 \
		-B 198.51.100.1 -t udp dp=52768,sp=42768 -q

	psample_capture_stop

	grep -q -e "group 1 " $CAPTURE_FILE
	check_fail $? "Sampled packets when should not"

	log_test "tc sample (w/ flower) rate ($bind)"

	tc filter del dev $port $bind protocol ip pref 1 handle 101 flower
}

tc_sample_acl_rate_test()
{
	__tc_sample_acl_rate_test ingress $rp1
	__tc_sample_acl_rate_test egress $rp2
}

tc_sample_acl_max_rate_test()
{
	RET=0

	# Policy-based sampling is not supported on Spectrum-1.
	mlxsw_only_on_spectrum 2+ || return

	tc filter add dev $rp1 ingress protocol ip pref 1 handle 101 flower \
		skip_sw action sample rate $((2 ** 24 - 1)) group 1
	check_err $? "Failed to configure sampling rule with max rate"

	tc filter del dev $rp1 ingress protocol ip pref 1 handle 101 flower

	tc filter add dev $rp1 ingress protocol ip pref 1 handle 101 flower \
		skip_sw action sample rate $((2 ** 24)) \
		group 1 &> /dev/null
	check_fail $? "Managed to configure sampling rate above maximum"

	log_test "tc sample (w/ flower) maximum rate"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
