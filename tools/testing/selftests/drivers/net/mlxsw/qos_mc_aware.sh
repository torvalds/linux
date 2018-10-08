#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A test for switch behavior under MC overload. An issue in Spectrum chips
# causes throughput of UC traffic to drop severely when a switch is under heavy
# MC load. This issue can be overcome by putting the switch to MC-aware mode.
# This test verifies that UC performance stays intact even as the switch is
# under MC flood, and therefore that the MC-aware mode is enabled and correctly
# configured.
#
# Because mlxsw throttles CPU port, the traffic can't actually reach userspace
# at full speed. That makes it impossible to use iperf3 to simply measure the
# throughput, because many packets (that reach $h3) don't get to the kernel at
# all even in UDP mode (the situation is even worse in TCP mode, where one can't
# hope to see more than a couple Mbps).
#
# So instead we send traffic with mausezahn and use RX ethtool counters at $h3.
# Multicast traffic is untagged, unicast traffic is tagged with PCP 1. Therefore
# each gets a different priority and we can use per-prio ethtool counters to
# measure the throughput. In order to avoid prioritizing unicast traffic, prio
# qdisc is installed on $swp3 and maps all priorities to the same band #7 (and
# thus TC 0).
#
# Mausezahn can't actually saturate the links unless it's using large frames.
# Thus we set MTU to 10K on all involved interfaces. Then both unicast and
# multicast traffic uses 8K frames.
#
# +-----------------------+                +----------------------------------+
# | H1                    |                |                               H2 |
# |                       |                |  unicast --> + $h2.111           |
# |                       |                |  traffic     | 192.0.2.129/28    |
# |          multicast    |                |              | e-qos-map 0:1     |
# |          traffic      |                |              |                   |
# | $h1 + <-----          |                |              + $h2               |
# +-----|-----------------+                +--------------|-------------------+
#       |                                                 |
# +-----|-------------------------------------------------|-------------------+
# |     + $swp1                                           + $swp2             |
# |     | >1Gbps                                          | >1Gbps            |
# | +---|----------------+                     +----------|----------------+  |
# | |   + $swp1.1        |                     |          + $swp2.111      |  |
# | |                BR1 |             SW      | BR111                     |  |
# | |   + $swp3.1        |                     |          + $swp3.111      |  |
# | +---|----------------+                     +----------|----------------+  |
# |     \_________________________________________________/                   |
# |                                    |                                      |
# |                                    + $swp3                                |
# |                                    | 1Gbps bottleneck                     |
# |                                    | prio qdisc: {0..7} -> 7              |
# +------------------------------------|--------------------------------------+
#                                      |
#                                   +--|-----------------+
#                                   |  + $h3          H3 |
#                                   |  |                 |
#                                   |  + $h3.111         |
#                                   |    192.0.2.130/28  |
#                                   +--------------------+

ALL_TESTS="
	ping_ipv4
	test_mc_aware
"

lib_dir=$(dirname $0)/../../../net/forwarding

NUM_NETIFS=6
source $lib_dir/lib.sh

h1_create()
{
	simple_if_init $h1
	mtu_set $h1 10000
}

h1_destroy()
{
	mtu_restore $h1
	simple_if_fini $h1
}

h2_create()
{
	simple_if_init $h2
	mtu_set $h2 10000

	vlan_create $h2 111 v$h2 192.0.2.129/28
	ip link set dev $h2.111 type vlan egress-qos-map 0:1
}

h2_destroy()
{
	vlan_destroy $h2 111

	mtu_restore $h2
	simple_if_fini $h2
}

h3_create()
{
	simple_if_init $h3
	mtu_set $h3 10000

	vlan_create $h3 111 v$h3 192.0.2.130/28
}

h3_destroy()
{
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

	ip link set dev $swp3 up
	mtu_set $swp3 10000

	vlan_create $swp2 111
	vlan_create $swp3 111

	ethtool -s $swp3 speed 1000 autoneg off
	tc qdisc replace dev $swp3 root handle 3: \
	   prio bands 8 priomap 7 7 7 7 7 7 7 7

	ip link add name br1 type bridge vlan_filtering 0
	ip link set dev br1 up
	ip link set dev $swp1 master br1
	ip link set dev $swp3 master br1

	ip link add name br111 type bridge vlan_filtering 0
	ip link set dev br111 up
	ip link set dev $swp2.111 master br111
	ip link set dev $swp3.111 master br111
}

switch_destroy()
{
	ip link del dev br111
	ip link del dev br1

	tc qdisc del dev $swp3 root handle 3:
	ethtool -s $swp3 autoneg on

	vlan_destroy $swp3 111
	vlan_destroy $swp2 111

	mtu_restore $swp3
	ip link set dev $swp3 down

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
	ping_test $h2 192.0.2.130
}

humanize()
{
	local speed=$1; shift

	for unit in bps Kbps Mbps Gbps; do
		if (($(echo "$speed < 1024" | bc))); then
			break
		fi

		speed=$(echo "scale=1; $speed / 1024" | bc)
	done

	echo "$speed${unit}"
}

rate()
{
	local t0=$1; shift
	local t1=$1; shift
	local interval=$1; shift

	echo $((8 * (t1 - t0) / interval))
}

check_rate()
{
	local rate=$1; shift
	local min=$1; shift
	local what=$1; shift

	if ((rate > min)); then
		return 0
	fi

	echo "$what $(humanize $ir) < $(humanize $min_ingress)" > /dev/stderr
	return 1
}

measure_uc_rate()
{
	local what=$1; shift

	local interval=10
	local i
	local ret=0

	# Dips in performance might cause momentary ingress rate to drop below
	# 1Gbps. That wouldn't saturate egress and MC would thus get through,
	# seemingly winning bandwidth on account of UC. Demand at least 2Gbps
	# average ingress rate to somewhat mitigate this.
	local min_ingress=2147483648

	mausezahn $h2.111 -p 8000 -A 192.0.2.129 -B 192.0.2.130 -c 0 \
		-a own -b $h3mac -t udp -q &
	sleep 1

	for i in {5..0}; do
		local t0=$(ethtool_stats_get $h3 rx_octets_prio_1)
		local u0=$(ethtool_stats_get $swp2 rx_octets_prio_1)
		sleep $interval
		local t1=$(ethtool_stats_get $h3 rx_octets_prio_1)
		local u1=$(ethtool_stats_get $swp2 rx_octets_prio_1)

		local ir=$(rate $u0 $u1 $interval)
		local er=$(rate $t0 $t1 $interval)

		if check_rate $ir $min_ingress "$what ingress rate"; then
			break
		fi

		# Fail the test if we can't get the throughput.
		if ((i == 0)); then
			ret=1
		fi
	done

	# Suppress noise from killing mausezahn.
	{ kill %% && wait; } 2>/dev/null

	echo $ir $er
	exit $ret
}

test_mc_aware()
{
	RET=0

	local -a uc_rate
	uc_rate=($(measure_uc_rate "UC-only"))
	check_err $? "Could not get high enough UC-only ingress rate"
	local ucth1=${uc_rate[1]}

	mausezahn $h1 -p 8000 -c 0 -a own -b bc -t udp -q &

	local d0=$(date +%s)
	local t0=$(ethtool_stats_get $h3 rx_octets_prio_0)
	local u0=$(ethtool_stats_get $swp1 rx_octets_prio_0)

	local -a uc_rate_2
	uc_rate_2=($(measure_uc_rate "UC+MC"))
	check_err $? "Could not get high enough UC+MC ingress rate"
	local ucth2=${uc_rate_2[1]}

	local d1=$(date +%s)
	local t1=$(ethtool_stats_get $h3 rx_octets_prio_0)
	local u1=$(ethtool_stats_get $swp1 rx_octets_prio_0)

	local deg=$(bc <<< "
			scale=2
			ret = 100 * ($ucth1 - $ucth2) / $ucth1
			if (ret > 0) { ret } else { 0 }
		    ")
	check_err $(bc <<< "$deg > 10")

	local interval=$((d1 - d0))
	local mc_ir=$(rate $u0 $u1 $interval)
	local mc_er=$(rate $t0 $t1 $interval)

	# Suppress noise from killing mausezahn.
	{ kill %% && wait; } 2>/dev/null

	log_test "UC performace under MC overload"

	echo "UC-only throughput  $(humanize $ucth1)"
	echo "UC+MC throughput    $(humanize $ucth2)"
	echo "Degradation         $deg %"
	echo
	echo "Full report:"
	echo "  UC only:"
	echo "    ingress UC throughput $(humanize ${uc_rate[0]})"
	echo "    egress UC throughput  $(humanize ${uc_rate[1]})"
	echo "  UC+MC:"
	echo "    ingress UC throughput $(humanize ${uc_rate_2[0]})"
	echo "    egress UC throughput  $(humanize ${uc_rate_2[1]})"
	echo "    ingress MC throughput $(humanize $mc_ir)"
	echo "    egress MC throughput  $(humanize $mc_er)"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
