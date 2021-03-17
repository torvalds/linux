# SPDX-License-Identifier: GPL-2.0

# This test sends a >1Gbps stream of traffic from H1, to the switch, which
# forwards it to a 1Gbps port. This 1Gbps stream is then looped back to the
# switch and forwarded to the port under test $swp3, which is also 1Gbps.
#
# This way, $swp3 should be 100% filled with traffic without any of it spilling
# to the backlog. Any extra packets sent should almost 1:1 go to backlog. That
# is what H2 is used for--it sends the extra traffic to create backlog.
#
# A RED Qdisc is installed on $swp3. The configuration is such that the minimum
# and maximum size are 1 byte apart, so there is a very clear border under which
# no marking or dropping takes place, and above which everything is marked or
# dropped.
#
# The test uses the buffer build-up behavior to test the installed RED.
#
# In order to test WRED, $swp3 actually contains RED under PRIO, with two
# different configurations. Traffic is prioritized using 802.1p and relies on
# the implicit mlxsw configuration, where packet priority is taken 1:1 from the
# 802.1p marking.
#
# +--------------------------+                     +--------------------------+
# | H1                       |                     | H2                       |
# |     + $h1.10             |                     |     + $h2.10             |
# |     | 192.0.2.1/28       |                     |     | 192.0.2.2/28       |
# |     |                    |                     |     |                    |
# |     |         $h1.11 +   |                     |     |         $h2.11 +   |
# |     |  192.0.2.17/28 |   |                     |     |  192.0.2.18/28 |   |
# |     |                |   |                     |     |                |   |
# |     \______    ______/   |                     |     \______    ______/   |
# |            \ /           |                     |            \ /           |
# |             + $h1        |                     |             + $h2        |
# +-------------|------------+                     +-------------|------------+
#               | >1Gbps                                         |
# +-------------|------------------------------------------------|------------+
# | SW          + $swp1                                          + $swp2      |
# |     _______/ \___________                        ___________/ \_______    |
# |    /                     \                      /                     \   |
# |  +-|-----------------+   |                    +-|-----------------+   |   |
# |  | + $swp1.10        |   |                    | + $swp2.10        |   |   |
# |  |                   |   |        .-------------+ $swp5.10        |   |   |
# |  |     BR1_10        |   |        |           |                   |   |   |
# |  |                   |   |        |           |     BR2_10        |   |   |
# |  | + $swp2.10        |   |        |           |                   |   |   |
# |  +-|-----------------+   |        |           | + $swp3.10        |   |   |
# |    |                     |        |           +-|-----------------+   |   |
# |    |   +-----------------|-+      |             |   +-----------------|-+ |
# |    |   |        $swp1.11 + |      |             |   |        $swp2.11 + | |
# |    |   |                   |      | .-----------------+ $swp5.11        | |
# |    |   |      BR1_11       |      | |           |   |                   | |
# |    |   |                   |      | |           |   |      BR2_11       | |
# |    |   |        $swp2.11 + |      | |           |   |                   | |
# |    |   +-----------------|-+      | |           |   |        $swp3.11 + | |
# |    |                     |        | |           |   +-----------------|-+ |
# |    \_______   ___________/        | |           \___________   _______/   |
# |            \ /                    \ /                       \ /           |
# |             + $swp4                + $swp5                   + $swp3      |
# +-------------|----------------------|-------------------------|------------+
#               |                      |                         | 1Gbps
#               \________1Gbps_________/                         |
#                                   +----------------------------|------------+
#                                   | H3                         + $h3        |
#                                   |      _____________________/ \_______    |
#                                   |     /                               \   |
#                                   |     |                               |   |
#                                   |     + $h3.10                 $h3.11 +   |
#                                   |       192.0.2.3/28    192.0.2.19/28     |
#                                   +-----------------------------------------+

NUM_NETIFS=8
CHECK_TC="yes"
lib_dir=$(dirname $0)/../../../net/forwarding
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh
source qos_lib.sh

ipaddr()
{
	local host=$1; shift
	local vlan=$1; shift

	echo 192.0.2.$((16 * (vlan - 10) + host))
}

host_create()
{
	local dev=$1; shift
	local host=$1; shift

	simple_if_init $dev
	mtu_set $dev 10000

	vlan_create $dev 10 v$dev $(ipaddr $host 10)/28
	ip link set dev $dev.10 type vlan egress 0:0

	vlan_create $dev 11 v$dev $(ipaddr $host 11)/28
	ip link set dev $dev.11 type vlan egress 0:1
}

host_destroy()
{
	local dev=$1; shift

	vlan_destroy $dev 11
	vlan_destroy $dev 10
	mtu_restore $dev
	simple_if_fini $dev
}

h1_create()
{
	host_create $h1 1
}

h1_destroy()
{
	host_destroy $h1
}

h2_create()
{
	host_create $h2 2
	tc qdisc add dev $h2 clsact

	# Some of the tests in this suite use multicast traffic. As this traffic
	# enters BR2_10 resp. BR2_11, it is flooded to all other ports. Thus
	# e.g. traffic ingressing through $swp2 is flooded to $swp3 (the
	# intended destination) and $swp5 (which is intended as ingress for
	# another stream of traffic).
	#
	# This is generally not a problem, but if the $swp5 throughput is lower
	# than $swp2 throughput, there will be a build-up at $swp5. That may
	# cause packets to fail to queue up at $swp3 due to shared buffer
	# quotas, and the test to spuriously fail.
	#
	# Prevent this by setting the speed of $h2 to 1Gbps.

	ethtool -s $h2 speed 1000 autoneg off
}

h2_destroy()
{
	ethtool -s $h2 autoneg on
	tc qdisc del dev $h2 clsact
	host_destroy $h2
}

h3_create()
{
	host_create $h3 3
	ethtool -s $h3 speed 1000 autoneg off
}

h3_destroy()
{
	ethtool -s $h3 autoneg on
	host_destroy $h3
}

switch_create()
{
	local intf
	local vlan

	ip link add dev br1_10 type bridge
	ip link add dev br1_11 type bridge

	ip link add dev br2_10 type bridge
	ip link add dev br2_11 type bridge

	for intf in $swp1 $swp2 $swp3 $swp4 $swp5; do
		ip link set dev $intf up
		mtu_set $intf 10000
	done

	for intf in $swp1 $swp4; do
		for vlan in 10 11; do
			vlan_create $intf $vlan
			ip link set dev $intf.$vlan master br1_$vlan
			ip link set dev $intf.$vlan up
		done
	done

	for intf in $swp2 $swp3 $swp5; do
		for vlan in 10 11; do
			vlan_create $intf $vlan
			ip link set dev $intf.$vlan master br2_$vlan
			ip link set dev $intf.$vlan up
		done
	done

	ip link set dev $swp4.10 type vlan egress 0:0
	ip link set dev $swp4.11 type vlan egress 0:1
	for intf in $swp1 $swp2 $swp5; do
		for vlan in 10 11; do
			ip link set dev $intf.$vlan type vlan ingress 0:0 1:1
		done
	done

	for intf in $swp2 $swp3 $swp4 $swp5; do
		ethtool -s $intf speed 1000 autoneg off
	done

	ip link set dev br1_10 up
	ip link set dev br1_11 up
	ip link set dev br2_10 up
	ip link set dev br2_11 up

	local size=$(devlink_pool_size_thtype 0 | cut -d' ' -f 1)
	devlink_port_pool_th_save $swp3 8
	devlink_port_pool_th_set $swp3 8 $size
}

switch_destroy()
{
	local intf
	local vlan

	devlink_port_pool_th_restore $swp3 8

	tc qdisc del dev $swp3 root 2>/dev/null

	ip link set dev br2_11 down
	ip link set dev br2_10 down
	ip link set dev br1_11 down
	ip link set dev br1_10 down

	for intf in $swp5 $swp4 $swp3 $swp2; do
		ethtool -s $intf autoneg on
	done

	for intf in $swp5 $swp3 $swp2 $swp4 $swp1; do
		for vlan in 11 10; do
			ip link set dev $intf.$vlan down
			ip link set dev $intf.$vlan nomaster
			vlan_destroy $intf $vlan
		done

		mtu_restore $intf
		ip link set dev $intf down
	done

	ip link del dev br2_11
	ip link del dev br2_10
	ip link del dev br1_11
	ip link del dev br1_10
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	swp3=${NETIFS[p5]}
	h3=${NETIFS[p6]}

	swp4=${NETIFS[p7]}
	swp5=${NETIFS[p8]}

	h3_mac=$(mac_get $h3)

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
	ping_test $h1.10 $(ipaddr 3 10) " from host 1, vlan 10"
	ping_test $h1.11 $(ipaddr 3 11) " from host 1, vlan 11"
	ping_test $h2.10 $(ipaddr 3 10) " from host 2, vlan 10"
	ping_test $h2.11 $(ipaddr 3 11) " from host 2, vlan 11"
}

get_tc()
{
	local vlan=$1; shift

	echo $((vlan - 10))
}

get_qdisc_handle()
{
	local vlan=$1; shift

	local tc=$(get_tc $vlan)
	local band=$((8 - tc))

	# Handle is 107: for TC1, 108: for TC0.
	echo "10$band:"
}

get_qdisc_backlog()
{
	local vlan=$1; shift

	qdisc_stats_get $swp3 $(get_qdisc_handle $vlan) .backlog
}

get_mc_transmit_queue()
{
	local vlan=$1; shift

	local tc=$(($(get_tc $vlan) + 8))
	ethtool_stats_get $swp3 tc_transmit_queue_tc_$tc
}

get_nmarked()
{
	local vlan=$1; shift

	ethtool_stats_get $swp3 ecn_marked
}

get_qdisc_npackets()
{
	local vlan=$1; shift

	busywait_for_counter 1100 +1 \
		qdisc_stats_get $swp3 $(get_qdisc_handle $vlan) .packets
}

send_packets()
{
	local vlan=$1; shift
	local proto=$1; shift
	local pkts=$1; shift

	$MZ $h2.$vlan -p 8000 -a own -b $h3_mac \
	    -A $(ipaddr 2 $vlan) -B $(ipaddr 3 $vlan) \
	    -t $proto -q -c $pkts "$@"
}

# This sends traffic in an attempt to build a backlog of $size. Returns 0 on
# success. After 10 failed attempts it bails out and returns 1. It dumps the
# backlog size to stdout.
build_backlog()
{
	local vlan=$1; shift
	local size=$1; shift
	local proto=$1; shift

	local tc=$((vlan - 10))
	local band=$((8 - tc))
	local cur=-1
	local i=0

	while :; do
		local cur=$(busywait 1100 until_counter_is "> $cur" \
					    get_qdisc_backlog $vlan)
		local diff=$((size - cur))
		local pkts=$(((diff + 7999) / 8000))

		if ((cur >= size)); then
			echo $cur
			return 0
		elif ((i++ > 10)); then
			echo $cur
			return 1
		fi

		send_packets $vlan $proto $pkts "$@"
	done
}

check_marking()
{
	local vlan=$1; shift
	local cond=$1; shift

	local npackets_0=$(get_qdisc_npackets $vlan)
	local nmarked_0=$(get_nmarked $vlan)
	sleep 5
	local npackets_1=$(get_qdisc_npackets $vlan)
	local nmarked_1=$(get_nmarked $vlan)

	local nmarked_d=$((nmarked_1 - nmarked_0))
	local npackets_d=$((npackets_1 - npackets_0))
	local pct=$((100 * nmarked_d / npackets_d))

	echo $pct
	((pct $cond))
}

ecn_test_common()
{
	local name=$1; shift
	local vlan=$1; shift
	local limit=$1; shift
	local backlog
	local pct

	# Build the below-the-limit backlog using UDP. We could use TCP just
	# fine, but this way we get a proof that UDP is accepted when queue
	# length is below the limit. The main stream is using TCP, and if the
	# limit is misconfigured, we would see this traffic being ECN marked.
	RET=0
	backlog=$(build_backlog $vlan $((2 * limit / 3)) udp)
	check_err $? "Could not build the requested backlog"
	pct=$(check_marking $vlan "== 0")
	check_err $? "backlog $backlog / $limit Got $pct% marked packets, expected == 0."
	log_test "TC $((vlan - 10)): $name backlog < limit"

	# Now push TCP, because non-TCP traffic would be early-dropped after the
	# backlog crosses the limit, and we want to make sure that the backlog
	# is above the limit.
	RET=0
	backlog=$(build_backlog $vlan $((3 * limit / 2)) tcp tos=0x01)
	check_err $? "Could not build the requested backlog"
	pct=$(check_marking $vlan ">= 95")
	check_err $? "backlog $backlog / $limit Got $pct% marked packets, expected >= 95."
	log_test "TC $((vlan - 10)): $name backlog > limit"
}

do_ecn_test()
{
	local vlan=$1; shift
	local limit=$1; shift
	local name=ECN

	start_tcp_traffic $h1.$vlan $(ipaddr 1 $vlan) $(ipaddr 3 $vlan) \
			  $h3_mac tos=0x01
	sleep 1

	ecn_test_common "$name" $vlan $limit

	# Up there we saw that UDP gets accepted when backlog is below the
	# limit. Now that it is above, it should all get dropped, and backlog
	# building should fail.
	RET=0
	build_backlog $vlan $((2 * limit)) udp >/dev/null
	check_fail $? "UDP traffic went into backlog instead of being early-dropped"
	log_test "TC $((vlan - 10)): $name backlog > limit: UDP early-dropped"

	stop_traffic
	sleep 1
}

do_ecn_nodrop_test()
{
	local vlan=$1; shift
	local limit=$1; shift
	local name="ECN nodrop"

	start_tcp_traffic $h1.$vlan $(ipaddr 1 $vlan) $(ipaddr 3 $vlan) \
			  $h3_mac tos=0x01
	sleep 1

	ecn_test_common "$name" $vlan $limit

	# Up there we saw that UDP gets accepted when backlog is below the
	# limit. Now that it is above, in nodrop mode, make sure it goes to
	# backlog as well.
	RET=0
	build_backlog $vlan $((2 * limit)) udp >/dev/null
	check_err $? "UDP traffic was early-dropped instead of getting into backlog"
	log_test "TC $((vlan - 10)): $name backlog > limit: UDP not dropped"

	stop_traffic
	sleep 1
}

do_red_test()
{
	local vlan=$1; shift
	local limit=$1; shift
	local backlog
	local pct

	# Use ECN-capable TCP to verify there's no marking even though the queue
	# is above limit.
	start_tcp_traffic $h1.$vlan $(ipaddr 1 $vlan) $(ipaddr 3 $vlan) \
			  $h3_mac tos=0x01

	# Pushing below the queue limit should work.
	RET=0
	backlog=$(build_backlog $vlan $((2 * limit / 3)) tcp tos=0x01)
	check_err $? "Could not build the requested backlog"
	pct=$(check_marking $vlan "== 0")
	check_err $? "backlog $backlog / $limit Got $pct% marked packets, expected == 0."
	log_test "TC $((vlan - 10)): RED backlog < limit"

	# Pushing above should not.
	RET=0
	backlog=$(build_backlog $vlan $((3 * limit / 2)) tcp tos=0x01)
	check_fail $? "Traffic went into backlog instead of being early-dropped"
	pct=$(check_marking $vlan "== 0")
	check_err $? "backlog $backlog / $limit Got $pct% marked packets, expected == 0."
	local diff=$((limit - backlog))
	pct=$((100 * diff / limit))
	((0 <= pct && pct <= 5))
	check_err $? "backlog $backlog / $limit expected <= 5% distance"
	log_test "TC $((vlan - 10)): RED backlog > limit"

	stop_traffic
	sleep 1
}

do_mc_backlog_test()
{
	local vlan=$1; shift
	local limit=$1; shift
	local backlog
	local pct

	RET=0

	start_tcp_traffic $h1.$vlan $(ipaddr 1 $vlan) $(ipaddr 3 $vlan) bc
	start_tcp_traffic $h2.$vlan $(ipaddr 2 $vlan) $(ipaddr 3 $vlan) bc

	qbl=$(busywait 5000 until_counter_is ">= 500000" \
		       get_qdisc_backlog $vlan)
	check_err $? "Could not build MC backlog"

	# Verify that we actually see the backlog on BUM TC. Do a busywait as
	# well, performance blips might cause false fail.
	local ebl
	ebl=$(busywait 5000 until_counter_is ">= 500000" \
		       get_mc_transmit_queue $vlan)
	check_err $? "MC backlog reported by qdisc not visible in ethtool"

	stop_traffic
	stop_traffic

	log_test "TC $((vlan - 10)): Qdisc reports MC backlog"
}

do_drop_test()
{
	local vlan=$1; shift
	local limit=$1; shift
	local trigger=$1; shift
	local subtest=$1; shift
	local fetch_counter=$1; shift
	local backlog
	local base
	local now
	local pct

	RET=0

	start_traffic $h1.$vlan $(ipaddr 1 $vlan) $(ipaddr 3 $vlan) $h3_mac

	# Create a bit of a backlog and observe no mirroring due to drops.
	qevent_rule_install_$subtest
	base=$($fetch_counter)

	build_backlog $vlan $((2 * limit / 3)) udp >/dev/null

	busywait 1100 until_counter_is ">= $((base + 1))" $fetch_counter >/dev/null
	check_fail $? "Spurious packets observed without buffer pressure"

	# Push to the queue until it's at the limit. The configured limit is
	# rounded by the qdisc and then by the driver, so this is the best we
	# can do to get to the real limit of the system.
	build_backlog $vlan $((3 * limit / 2)) udp >/dev/null

	base=$($fetch_counter)
	send_packets $vlan udp 11

	now=$(busywait 1100 until_counter_is ">= $((base + 10))" $fetch_counter)
	check_err $? "Dropped packets not observed: 11 expected, $((now - base)) seen"

	# When no extra traffic is injected, there should be no mirroring.
	busywait 1100 until_counter_is ">= $((base + 20))" $fetch_counter >/dev/null
	check_fail $? "Spurious packets observed"

	# When the rule is uninstalled, there should be no mirroring.
	qevent_rule_uninstall_$subtest
	send_packets $vlan udp 11
	busywait 1100 until_counter_is ">= $((base + 20))" $fetch_counter >/dev/null
	check_fail $? "Spurious packets observed after uninstall"

	log_test "TC $((vlan - 10)): ${trigger}ped packets $subtest'd"

	stop_traffic
	sleep 1
}

qevent_rule_install_mirror()
{
	tc filter add block 10 pref 1234 handle 102 matchall skip_sw \
	   action mirred egress mirror dev $swp2 hw_stats disabled
}

qevent_rule_uninstall_mirror()
{
	tc filter del block 10 pref 1234 handle 102 matchall
}

qevent_counter_fetch_mirror()
{
	tc_rule_handle_stats_get "dev $h2 ingress" 101
}

do_drop_mirror_test()
{
	local vlan=$1; shift
	local limit=$1; shift
	local qevent_name=$1; shift

	tc filter add dev $h2 ingress pref 1 handle 101 prot ip \
	   flower skip_sw ip_proto udp \
	   action drop

	do_drop_test "$vlan" "$limit" "$qevent_name" mirror \
		     qevent_counter_fetch_mirror

	tc filter del dev $h2 ingress pref 1 handle 101 flower
}

qevent_rule_install_trap()
{
	tc filter add block 10 pref 1234 handle 102 matchall skip_sw \
	   action trap hw_stats disabled
}

qevent_rule_uninstall_trap()
{
	tc filter del block 10 pref 1234 handle 102 matchall
}

qevent_counter_fetch_trap()
{
	local trap_name=$1; shift

	devlink_trap_rx_packets_get "$trap_name"
}

do_drop_trap_test()
{
	local vlan=$1; shift
	local limit=$1; shift
	local trap_name=$1; shift

	do_drop_test "$vlan" "$limit" "$trap_name" trap \
		     "qevent_counter_fetch_trap $trap_name"
}
