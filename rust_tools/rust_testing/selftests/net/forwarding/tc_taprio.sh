#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS=" \
	test_clock_jump_backward \
	test_taprio_after_ptp \
	test_max_sdu \
	test_clock_jump_backward_forward \
"
NUM_NETIFS=4
source tc_common.sh
source lib.sh
source tsn_lib.sh

require_command python3

# The test assumes the usual topology from the README, where h1 is connected to
# swp1, h2 to swp2, and swp1 and swp2 are together in a bridge.
# Additional assumption: h1 and h2 use the same PHC, and so do swp1 and swp2.
# By synchronizing h1 to swp1 via PTP, h2 is also implicitly synchronized to
# swp1 (and both to CLOCK_REALTIME).
h1=${NETIFS[p1]}
swp1=${NETIFS[p2]}
swp2=${NETIFS[p3]}
h2=${NETIFS[p4]}

UDS_ADDRESS_H1="/var/run/ptp4l_h1"
UDS_ADDRESS_SWP1="/var/run/ptp4l_swp1"

H1_IPV4="192.0.2.1"
H2_IPV4="192.0.2.2"
H1_IPV6="2001:db8:1::1"
H2_IPV6="2001:db8:1::2"

# Tunables
NUM_PKTS=100
STREAM_VID=10
STREAM_PRIO_1=6
STREAM_PRIO_2=5
STREAM_PRIO_3=4
# PTP uses TC 0
ALL_GATES=$((1 << 0 | 1 << STREAM_PRIO_1 | 1 << STREAM_PRIO_2))
# Use a conservative cycle of 10 ms to allow the test to still pass when the
# kernel has some extra overhead like lockdep etc
CYCLE_TIME_NS=10000000
# Create two Gate Control List entries, one OPEN and one CLOSE, of equal
# durations
GATE_DURATION_NS=$((CYCLE_TIME_NS / 2))
# Give 2/3 of the cycle time to user space and 1/3 to the kernel
FUDGE_FACTOR=$((CYCLE_TIME_NS / 3))
# Shift the isochron base time by half the gate time, so that packets are
# always received by swp1 close to the middle of the time slot, to minimize
# inaccuracies due to network sync
SHIFT_TIME_NS=$((GATE_DURATION_NS / 2))

path_delay=

h1_create()
{
	simple_if_init $h1 $H1_IPV4/24 $H1_IPV6/64
}

h1_destroy()
{
	simple_if_fini $h1 $H1_IPV4/24 $H1_IPV6/64
}

h2_create()
{
	simple_if_init $h2 $H2_IPV4/24 $H2_IPV6/64
}

h2_destroy()
{
	simple_if_fini $h2 $H2_IPV4/24 $H2_IPV6/64
}

switch_create()
{
	local h2_mac_addr=$(mac_get $h2)

	ip link set $swp1 up
	ip link set $swp2 up

	ip link add br0 type bridge vlan_filtering 1
	ip link set $swp1 master br0
	ip link set $swp2 master br0
	ip link set br0 up

	bridge vlan add dev $swp2 vid $STREAM_VID
	bridge vlan add dev $swp1 vid $STREAM_VID
	bridge fdb add dev $swp2 \
		$h2_mac_addr vlan $STREAM_VID static master
}

switch_destroy()
{
	ip link del br0
}

ptp_setup()
{
	# Set up swp1 as a master PHC for h1, synchronized to the local
	# CLOCK_REALTIME.
	phc2sys_start $UDS_ADDRESS_SWP1
	ptp4l_start $h1 true $UDS_ADDRESS_H1
	ptp4l_start $swp1 false $UDS_ADDRESS_SWP1
}

ptp_cleanup()
{
	ptp4l_stop $swp1
	ptp4l_stop $h1
	phc2sys_stop
}

txtime_setup()
{
	local if_name=$1

	tc qdisc add dev $if_name clsact
	# Classify PTP on TC 7 and isochron on TC 6
	tc filter add dev $if_name egress protocol 0x88f7 \
		flower action skbedit priority 7
	tc filter add dev $if_name egress protocol 802.1Q \
		flower vlan_ethtype 0xdead action skbedit priority 6
	tc qdisc add dev $if_name handle 100: parent root mqprio num_tc 8 \
		queues 1@0 1@1 1@2 1@3 1@4 1@5 1@6 1@7 \
		map 0 1 2 3 4 5 6 7 \
		hw 1
	# Set up TC 5, 6, 7 for SO_TXTIME. tc-mqprio queues count from 1.
	tc qdisc replace dev $if_name parent 100:$((STREAM_PRIO_1 + 1)) etf \
		clockid CLOCK_TAI offload delta $FUDGE_FACTOR
	tc qdisc replace dev $if_name parent 100:$((STREAM_PRIO_2 + 1)) etf \
		clockid CLOCK_TAI offload delta $FUDGE_FACTOR
	tc qdisc replace dev $if_name parent 100:$((STREAM_PRIO_3 + 1)) etf \
		clockid CLOCK_TAI offload delta $FUDGE_FACTOR
}

txtime_cleanup()
{
	local if_name=$1

	tc qdisc del dev $if_name clsact
	tc qdisc del dev $if_name root
}

taprio_replace()
{
	local if_name="$1"; shift
	local extra_args="$1"; shift

	# STREAM_PRIO_1 always has an open gate.
	# STREAM_PRIO_2 has a gate open for GATE_DURATION_NS (half the cycle time)
	# STREAM_PRIO_3 always has a closed gate.
	tc qdisc replace dev $if_name root stab overhead 24 taprio num_tc 8 \
		queues 1@0 1@1 1@2 1@3 1@4 1@5 1@6 1@7 \
		map 0 1 2 3 4 5 6 7 \
		sched-entry S $(printf "%x" $ALL_GATES) $GATE_DURATION_NS \
		sched-entry S $(printf "%x" $((ALL_GATES & ~(1 << STREAM_PRIO_2)))) $GATE_DURATION_NS \
		base-time 0 flags 0x2 $extra_args
	taprio_wait_for_admin $if_name
}

taprio_cleanup()
{
	local if_name=$1

	tc qdisc del dev $if_name root
}

probe_path_delay()
{
	local isochron_dat="$(mktemp)"
	local received

	log_info "Probing path delay"

	isochron_do "$h1" "$h2" "$UDS_ADDRESS_H1" "" 0 \
		"$CYCLE_TIME_NS" "" "" "$NUM_PKTS" \
		"$STREAM_VID" "$STREAM_PRIO_1" "" "$isochron_dat"

	received=$(isochron_report_num_received "$isochron_dat")
	if [ "$received" != "$NUM_PKTS" ]; then
		echo "Cannot establish basic data path between $h1 and $h2"
		exit $ksft_fail
	fi

	printf "pdelay = {}\n" > isochron_data.py
	isochron report --input-file "$isochron_dat" \
		--printf-format "pdelay[%u] = %d - %d\n" \
		--printf-args "qRT" \
		>> isochron_data.py
	cat <<-'EOF' > isochron_postprocess.py
	#!/usr/bin/env python3

	from isochron_data import pdelay
	import numpy as np

	w = np.array(list(pdelay.values()))
	print("{}".format(np.max(w)))
	EOF
	path_delay=$(python3 ./isochron_postprocess.py)

	log_info "Path delay from $h1 to $h2 estimated at $path_delay ns"

	if [ "$path_delay" -gt "$GATE_DURATION_NS" ]; then
		echo "Path delay larger than gate duration, aborting"
		exit $ksft_fail
	fi

	rm -f ./isochron_data.py 2> /dev/null
	rm -f ./isochron_postprocess.py 2> /dev/null
	rm -f "$isochron_dat" 2> /dev/null
}

setup_prepare()
{
	vrf_prepare

	h1_create
	h2_create
	switch_create

	txtime_setup $h1

	# Temporarily set up PTP just to probe the end-to-end path delay.
	ptp_setup
	probe_path_delay
	ptp_cleanup
}

cleanup()
{
	pre_cleanup

	isochron_recv_stop
	txtime_cleanup $h1

	switch_destroy
	h2_destroy
	h1_destroy

	vrf_cleanup
}

run_test()
{
	local base_time=$1; shift
	local stream_prio=$1; shift
	local expected_delay=$1; shift
	local should_fail=$1; shift
	local test_name=$1; shift
	local isochron_dat="$(mktemp)"
	local received
	local median_delay

	RET=0

	# Set the shift time equal to the cycle time, which effectively
	# cancels the default advance time. Packets won't be sent early in
	# software, which ensures that they won't prematurely enter through
	# the open gate in __test_out_of_band(). Also, the gate is open for
	# long enough that this won't cause a problem in __test_in_band().
	isochron_do "$h1" "$h2" "$UDS_ADDRESS_H1" "" "$base_time" \
		"$CYCLE_TIME_NS" "$SHIFT_TIME_NS" "$GATE_DURATION_NS" \
		"$NUM_PKTS" "$STREAM_VID" "$stream_prio" "" "$isochron_dat"

	received=$(isochron_report_num_received "$isochron_dat")
	[ "$received" = "$NUM_PKTS" ]
	check_err_fail $should_fail $? "Reception of $NUM_PKTS packets"

	if [ $should_fail = 0 ] && [ "$received" = "$NUM_PKTS" ]; then
		printf "pdelay = {}\n" > isochron_data.py
		isochron report --input-file "$isochron_dat" \
			--printf-format "pdelay[%u] = %d - %d\n" \
			--printf-args "qRT" \
			>> isochron_data.py
		cat <<-'EOF' > isochron_postprocess.py
		#!/usr/bin/env python3

		from isochron_data import pdelay
		import numpy as np

		w = np.array(list(pdelay.values()))
		print("{}".format(int(np.median(w))))
		EOF
		median_delay=$(python3 ./isochron_postprocess.py)

		# If the condition below is true, packets were delayed by a closed gate
		[ "$median_delay" -gt $((path_delay + expected_delay)) ]
		check_fail $? "Median delay $median_delay is greater than expected delay $expected_delay plus path delay $path_delay"

		# If the condition below is true, packets were sent expecting them to
		# hit a closed gate in the switch, but were not delayed
		[ "$expected_delay" -gt 0 ] && [ "$median_delay" -lt "$expected_delay" ]
		check_fail $? "Median delay $median_delay is less than expected delay $expected_delay"
	fi

	log_test "$test_name"

	rm -f ./isochron_data.py 2> /dev/null
	rm -f ./isochron_postprocess.py 2> /dev/null
	rm -f "$isochron_dat" 2> /dev/null
}

__test_always_open()
{
	run_test 0.000000000 $STREAM_PRIO_1 0 0 "Gate always open"
}

__test_always_closed()
{
	run_test 0.000000000 $STREAM_PRIO_3 0 1 "Gate always closed"
}

__test_in_band()
{
	# Send packets in-band with the OPEN gate entry
	run_test 0.000000000 $STREAM_PRIO_2 0 0 "In band with gate"
}

__test_out_of_band()
{
	# Send packets in-band with the CLOSE gate entry
	run_test 0.005000000 $STREAM_PRIO_2 \
		$((GATE_DURATION_NS - SHIFT_TIME_NS)) 0 \
		"Out of band with gate"
}

run_subtests()
{
	__test_always_open
	__test_always_closed
	__test_in_band
	__test_out_of_band
}

test_taprio_after_ptp()
{
	log_info "Setting up taprio after PTP"
	ptp_setup
	taprio_replace $swp2
	run_subtests
	taprio_cleanup $swp2
	ptp_cleanup
}

__test_under_max_sdu()
{
	# Limit max-sdu for STREAM_PRIO_1
	taprio_replace "$swp2" "max-sdu 0 0 0 0 0 0 100 0"
	run_test 0.000000000 $STREAM_PRIO_1 0 0 "Under maximum SDU"
}

__test_over_max_sdu()
{
	# Limit max-sdu for STREAM_PRIO_1
	taprio_replace "$swp2" "max-sdu 0 0 0 0 0 0 20 0"
	run_test 0.000000000 $STREAM_PRIO_1 0 1 "Over maximum SDU"
}

test_max_sdu()
{
	ptp_setup
	__test_under_max_sdu
	__test_over_max_sdu
	taprio_cleanup $swp2
	ptp_cleanup
}

# Perform a clock jump in the past without synchronization running, so that the
# time base remains where it was set by phc_ctl.
test_clock_jump_backward()
{
	# This is a more complex schedule specifically crafted in a way that
	# has been problematic on NXP LS1028A. Not much to test with it other
	# than the fact that it passes traffic.
	tc qdisc replace dev $swp2 root stab overhead 24 taprio num_tc 8 \
		queues 1@0 1@1 1@2 1@3 1@4 1@5 1@6 1@7 map 0 1 2 3 4 5 6 7 \
		base-time 0 sched-entry S 20 300000 sched-entry S 10 200000 \
		sched-entry S 20 300000 sched-entry S 48 200000 \
		sched-entry S 20 300000 sched-entry S 83 200000 \
		sched-entry S 40 300000 sched-entry S 00 200000 flags 2

	log_info "Forcing a backward clock jump"
	phc_ctl $swp1 set 0

	ping_test $h1 192.0.2.2
	taprio_cleanup $swp2
}

# Test that taprio tolerates clock jumps.
# Since ptp4l and phc2sys are running, it is expected for the time to
# eventually recover (through yet another clock jump). Isochron waits
# until that is the case.
test_clock_jump_backward_forward()
{
	log_info "Forcing a backward and a forward clock jump"
	taprio_replace $swp2
	phc_ctl $swp1 set 0
	ptp_setup
	ping_test $h1 192.0.2.2
	run_subtests
	ptp_cleanup
	taprio_cleanup $swp2
}

tc_offload_check
if [[ $? -ne 0 ]]; then
	log_test_skip "Could not test offloaded functionality"
	exit $EXIT_STATUS
fi

trap cleanup EXIT

setup_prepare
setup_wait
tests_run

exit $EXIT_STATUS
