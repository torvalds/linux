# SPDX-License-Identifier: GPL-2.0

# This test sends one stream of traffic from H1 through a TBF shaper, to a RED
# within TBF shaper on $swp3. The two shapers have the same configuration, and
# thus the resulting stream should fill all available bandwidth on the latter
# shaper. A second stream is sent from H2 also via $swp3, and used to inject
# additional traffic. Since all available bandwidth is taken, this traffic has
# to go to backlog.
#
# +--------------------------+                     +--------------------------+
# | H1                       |                     | H2                       |
# |     + $h1                |                     |     + $h2                |
# |     | 192.0.2.1/28       |                     |     | 192.0.2.2/28       |
# |     | TBF 10Mbps         |                     |     |                    |
# +-----|--------------------+                     +-----|--------------------+
#       |                                                |
# +-----|------------------------------------------------|--------------------+
# | SW  |                                                |                    |
# |  +--|------------------------------------------------|----------------+   |
# |  |  + $swp1                                          + $swp2          |   |
# |  |                               BR                                   |   |
# |  |                                                                    |   |
# |  |                                + $swp3                             |   |
# |  |                                | TBF 10Mbps / RED                  |   |
# |  +--------------------------------|-----------------------------------+   |
# |                                   |                                       |
# +-----------------------------------|---------------------------------------+
#                                     |
#                               +-----|--------------------+
#			        | H3  |                    |
#			        |     + $h1                |
#			        |       192.0.2.3/28       |
#			        |                          |
#			        +--------------------------+

ALL_TESTS="
	ping_ipv4
	ecn_test
	ecn_nodrop_test
	red_test
	red_qevent_test
	ecn_qevent_test
"

NUM_NETIFS=6
CHECK_TC="yes"
source lib.sh

BACKLOG=30000
PKTSZ=1400

h1_create()
{
	simple_if_init $h1 192.0.2.1/28
	mtu_set $h1 10000
	tc qdisc replace dev $h1 root handle 1: tbf \
	   rate 10Mbit burst 10K limit 1M
}

h1_destroy()
{
	tc qdisc del dev $h1 root
	mtu_restore $h1
	simple_if_fini $h1 192.0.2.1/28
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/28
	mtu_set $h2 10000
}

h2_destroy()
{
	mtu_restore $h2
	simple_if_fini $h2 192.0.2.2/28
}

h3_create()
{
	simple_if_init $h3 192.0.2.3/28
	mtu_set $h3 10000
}

h3_destroy()
{
	mtu_restore $h3
	simple_if_fini $h3 192.0.2.3/28
}

switch_create()
{
	ip link add dev br up type bridge
	ip link set dev $swp1 up master br
	ip link set dev $swp2 up master br
	ip link set dev $swp3 up master br

	mtu_set $swp1 10000
	mtu_set $swp2 10000
	mtu_set $swp3 10000

	tc qdisc replace dev $swp3 root handle 1: tbf \
	   rate 10Mbit burst 10K limit 1M
	ip link add name _drop_test up type dummy
}

switch_destroy()
{
	ip link del dev _drop_test
	tc qdisc del dev $swp3 root

	mtu_restore $h3
	mtu_restore $h2
	mtu_restore $h1

	ip link set dev $swp3 down nomaster
	ip link set dev $swp2 down nomaster
	ip link set dev $swp1 down nomaster
	ip link del dev br
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	h2=${NETIFS[p3]}
	swp2=${NETIFS[p4]}

	swp3=${NETIFS[p5]}
	h3=${NETIFS[p6]}

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
	ping_test $h1 192.0.2.3 " from host 1"
	ping_test $h2 192.0.2.3 " from host 2"
}

get_qdisc_backlog()
{
	qdisc_stats_get $swp3 11: .backlog
}

get_nmarked()
{
	qdisc_stats_get $swp3 11: .marked
}

get_qdisc_npackets()
{
	qdisc_stats_get $swp3 11: .packets
}

get_nmirrored()
{
	link_stats_get _drop_test tx packets
}

send_packets()
{
	local proto=$1; shift
	local pkts=$1; shift

	$MZ $h2 -p $PKTSZ -a own -b $h3_mac -A 192.0.2.2 -B 192.0.2.3 -t $proto -q -c $pkts "$@"
}

# This sends traffic in an attempt to build a backlog of $size. Returns 0 on
# success. After 10 failed attempts it bails out and returns 1. It dumps the
# backlog size to stdout.
build_backlog()
{
	local size=$1; shift
	local proto=$1; shift

	local i=0

	while :; do
		local cur=$(get_qdisc_backlog)
		local diff=$((size - cur))
		local pkts=$(((diff + PKTSZ - 1) / PKTSZ))

		if ((cur >= size)); then
			echo $cur
			return 0
		elif ((i++ > 10)); then
			echo $cur
			return 1
		fi

		send_packets $proto $pkts "$@"
		sleep 1
	done
}

check_marking()
{
	local cond=$1; shift

	local npackets_0=$(get_qdisc_npackets)
	local nmarked_0=$(get_nmarked)
	sleep 5
	local npackets_1=$(get_qdisc_npackets)
	local nmarked_1=$(get_nmarked)

	local nmarked_d=$((nmarked_1 - nmarked_0))
	local npackets_d=$((npackets_1 - npackets_0))
	local pct=$((100 * nmarked_d / npackets_d))

	echo $pct
	((pct $cond))
}

check_mirroring()
{
	local cond=$1; shift

	local npackets_0=$(get_qdisc_npackets)
	local nmirrored_0=$(get_nmirrored)
	sleep 5
	local npackets_1=$(get_qdisc_npackets)
	local nmirrored_1=$(get_nmirrored)

	local nmirrored_d=$((nmirrored_1 - nmirrored_0))
	local npackets_d=$((npackets_1 - npackets_0))
	local pct=$((100 * nmirrored_d / npackets_d))

	echo $pct
	((pct $cond))
}

ecn_test_common()
{
	local name=$1; shift
	local limit=$1; shift
	local backlog
	local pct

	# Build the below-the-limit backlog using UDP. We could use TCP just
	# fine, but this way we get a proof that UDP is accepted when queue
	# length is below the limit. The main stream is using TCP, and if the
	# limit is misconfigured, we would see this traffic being ECN marked.
	RET=0
	backlog=$(build_backlog $((2 * limit / 3)) udp)
	check_err $? "Could not build the requested backlog"
	pct=$(check_marking "== 0")
	check_err $? "backlog $backlog / $limit Got $pct% marked packets, expected == 0."
	log_test "$name backlog < limit"

	# Now push TCP, because non-TCP traffic would be early-dropped after the
	# backlog crosses the limit, and we want to make sure that the backlog
	# is above the limit.
	RET=0
	backlog=$(build_backlog $((3 * limit / 2)) tcp tos=0x01)
	check_err $? "Could not build the requested backlog"
	pct=$(check_marking ">= 95")
	check_err $? "backlog $backlog / $limit Got $pct% marked packets, expected >= 95."
	log_test "$name backlog > limit"
}

do_ecn_test()
{
	local limit=$1; shift
	local name=ECN

	$MZ $h1 -p $PKTSZ -A 192.0.2.1 -B 192.0.2.3 -c 0 \
		-a own -b $h3_mac -t tcp -q tos=0x01 &
	sleep 1

	ecn_test_common "$name" $limit

	# Up there we saw that UDP gets accepted when backlog is below the
	# limit. Now that it is above, it should all get dropped, and backlog
	# building should fail.
	RET=0
	build_backlog $((2 * limit)) udp >/dev/null
	check_fail $? "UDP traffic went into backlog instead of being early-dropped"
	log_test "$name backlog > limit: UDP early-dropped"

	stop_traffic
	sleep 1
}

do_ecn_nodrop_test()
{
	local limit=$1; shift
	local name="ECN nodrop"

	$MZ $h1 -p $PKTSZ -A 192.0.2.1 -B 192.0.2.3 -c 0 \
		-a own -b $h3_mac -t tcp -q tos=0x01 &
	sleep 1

	ecn_test_common "$name" $limit

	# Up there we saw that UDP gets accepted when backlog is below the
	# limit. Now that it is above, in nodrop mode, make sure it goes to
	# backlog as well.
	RET=0
	build_backlog $((2 * limit)) udp >/dev/null
	check_err $? "UDP traffic was early-dropped instead of getting into backlog"
	log_test "$name backlog > limit: UDP not dropped"

	stop_traffic
	sleep 1
}

do_red_test()
{
	local limit=$1; shift
	local backlog
	local pct

	# Use ECN-capable TCP to verify there's no marking even though the queue
	# is above limit.
	$MZ $h1 -p $PKTSZ -A 192.0.2.1 -B 192.0.2.3 -c 0 \
		-a own -b $h3_mac -t tcp -q tos=0x01 &

	# Pushing below the queue limit should work.
	RET=0
	backlog=$(build_backlog $((2 * limit / 3)) tcp tos=0x01)
	check_err $? "Could not build the requested backlog"
	pct=$(check_marking "== 0")
	check_err $? "backlog $backlog / $limit Got $pct% marked packets, expected == 0."
	log_test "RED backlog < limit"

	# Pushing above should not.
	RET=0
	backlog=$(build_backlog $((3 * limit / 2)) tcp tos=0x01)
	check_fail $? "Traffic went into backlog instead of being early-dropped"
	pct=$(check_marking "== 0")
	check_err $? "backlog $backlog / $limit Got $pct% marked packets, expected == 0."
	log_test "RED backlog > limit"

	stop_traffic
	sleep 1
}

do_red_qevent_test()
{
	local limit=$1; shift
	local backlog
	local base
	local now
	local pct

	RET=0

	$MZ $h1 -p $PKTSZ -A 192.0.2.1 -B 192.0.2.3 -c 0 \
		-a own -b $h3_mac -t udp -q &
	sleep 1

	tc filter add block 10 pref 1234 handle 102 matchall skip_hw \
	   action mirred egress mirror dev _drop_test

	# Push to the queue until it's at the limit. The configured limit is
	# rounded by the qdisc, so this is the best we can do to get to the real
	# limit.
	build_backlog $((3 * limit / 2)) udp >/dev/null

	base=$(get_nmirrored)
	send_packets udp 100
	sleep 1
	now=$(get_nmirrored)
	((now >= base + 100))
	check_err $? "Dropped packets not observed: 100 expected, $((now - base)) seen"

	tc filter del block 10 pref 1234 handle 102 matchall

	base=$(get_nmirrored)
	send_packets udp 100
	sleep 1
	now=$(get_nmirrored)
	((now == base))
	check_err $? "Dropped packets still observed: 0 expected, $((now - base)) seen"

	log_test "RED early_dropped packets mirrored"

	stop_traffic
	sleep 1
}

do_ecn_qevent_test()
{
	local limit=$1; shift
	local name=ECN

	RET=0

	$MZ $h1 -p $PKTSZ -A 192.0.2.1 -B 192.0.2.3 -c 0 \
		-a own -b $h3_mac -t tcp -q tos=0x01 &
	sleep 1

	tc filter add block 10 pref 1234 handle 102 matchall skip_hw \
	   action mirred egress mirror dev _drop_test

	backlog=$(build_backlog $((2 * limit / 3)) tcp tos=0x01)
	check_err $? "Could not build the requested backlog"
	pct=$(check_mirroring "== 0")
	check_err $? "backlog $backlog / $limit Got $pct% mirrored packets, expected == 0."

	backlog=$(build_backlog $((3 * limit / 2)) tcp tos=0x01)
	check_err $? "Could not build the requested backlog"
	pct=$(check_mirroring ">= 95")
	check_err $? "backlog $backlog / $limit Got $pct% mirrored packets, expected >= 95."

	tc filter del block 10 pref 1234 handle 102 matchall

	log_test "ECN marked packets mirrored"

	stop_traffic
	sleep 1
}

install_qdisc()
{
	local -a args=("$@")

	tc qdisc replace dev $swp3 parent 1:1 handle 11: red \
	   limit 1M avpkt $PKTSZ probability 1 \
	   min $BACKLOG max $((BACKLOG + 1)) burst 38 "${args[@]}"
	sleep 1
}

uninstall_qdisc()
{
	tc qdisc del dev $swp3 parent 1:1
}

ecn_test()
{
	install_qdisc ecn
	do_ecn_test $BACKLOG
	uninstall_qdisc
}

ecn_nodrop_test()
{
	install_qdisc ecn nodrop
	do_ecn_nodrop_test $BACKLOG
	uninstall_qdisc
}

red_test()
{
	install_qdisc
	do_red_test $BACKLOG
	uninstall_qdisc
}

red_qevent_test()
{
	install_qdisc qevent early_drop block 10
	do_red_qevent_test $BACKLOG
	uninstall_qdisc
}

ecn_qevent_test()
{
	install_qdisc ecn qevent mark block 10
	do_ecn_qevent_test $BACKLOG
	uninstall_qdisc
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
