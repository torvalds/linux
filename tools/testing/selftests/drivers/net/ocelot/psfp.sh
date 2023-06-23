#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright 2021-2022 NXP

# Note: On LS1028A, in lack of enough user ports, this setup requires patching
# the device tree to use the second CPU port as a user port

WAIT_TIME=1
NUM_NETIFS=4
STABLE_MAC_ADDRS=yes
NETIF_CREATE=no
lib_dir=$(dirname $0)/../../../net/forwarding
source $lib_dir/tc_common.sh
source $lib_dir/lib.sh
source $lib_dir/tsn_lib.sh

UDS_ADDRESS_H1="/var/run/ptp4l_h1"
UDS_ADDRESS_SWP1="/var/run/ptp4l_swp1"

# Tunables
NUM_PKTS=1000
STREAM_VID=100
STREAM_PRIO=6
# Use a conservative cycle of 10 ms to allow the test to still pass when the
# kernel has some extra overhead like lockdep etc
CYCLE_TIME_NS=10000000
# Create two Gate Control List entries, one OPEN and one CLOSE, of equal
# durations
GATE_DURATION_NS=$((${CYCLE_TIME_NS} / 2))
# Give 2/3 of the cycle time to user space and 1/3 to the kernel
FUDGE_FACTOR=$((${CYCLE_TIME_NS} / 3))
# Shift the isochron base time by half the gate time, so that packets are
# always received by swp1 close to the middle of the time slot, to minimize
# inaccuracies due to network sync
SHIFT_TIME_NS=$((${GATE_DURATION_NS} / 2))

h1=${NETIFS[p1]}
swp1=${NETIFS[p2]}
swp2=${NETIFS[p3]}
h2=${NETIFS[p4]}

H1_IPV4="192.0.2.1"
H2_IPV4="192.0.2.2"
H1_IPV6="2001:db8:1::1"
H2_IPV6="2001:db8:1::2"

# Chain number exported by the ocelot driver for
# Per-Stream Filtering and Policing filters
PSFP()
{
	echo 30000
}

psfp_chain_create()
{
	local if_name=$1

	tc qdisc add dev $if_name clsact

	tc filter add dev $if_name ingress chain 0 pref 49152 flower \
		skip_sw action goto chain $(PSFP)
}

psfp_chain_destroy()
{
	local if_name=$1

	tc qdisc del dev $if_name clsact
}

psfp_filter_check()
{
	local expected=$1
	local packets=""
	local drops=""
	local stats=""

	stats=$(tc -j -s filter show dev ${swp1} ingress chain $(PSFP) pref 1)
	packets=$(echo ${stats} | jq ".[1].options.actions[].stats.packets")
	drops=$(echo ${stats} | jq ".[1].options.actions[].stats.drops")

	if ! [ "${packets}" = "${expected}" ]; then
		printf "Expected filter to match on %d packets but matched on %d instead\n" \
			"${expected}" "${packets}"
	fi

	echo "Hardware filter reports ${drops} drops"
}

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

	ip link set ${swp1} up
	ip link set ${swp2} up

	ip link add br0 type bridge vlan_filtering 1
	ip link set ${swp1} master br0
	ip link set ${swp2} master br0
	ip link set br0 up

	bridge vlan add dev ${swp2} vid ${STREAM_VID}
	bridge vlan add dev ${swp1} vid ${STREAM_VID}
	# PSFP on Ocelot requires the filter to also be added to the bridge
	# FDB, and not be removed
	bridge fdb add dev ${swp2} \
		${h2_mac_addr} vlan ${STREAM_VID} static master

	psfp_chain_create ${swp1}

	tc filter add dev ${swp1} ingress chain $(PSFP) pref 1 \
		protocol 802.1Q flower skip_sw \
		dst_mac ${h2_mac_addr} vlan_id ${STREAM_VID} \
		action gate base-time 0.000000000 \
		sched-entry OPEN  ${GATE_DURATION_NS} -1 -1 \
		sched-entry CLOSE ${GATE_DURATION_NS} -1 -1
}

switch_destroy()
{
	psfp_chain_destroy ${swp1}
	ip link del br0
}

txtime_setup()
{
	local if_name=$1

	tc qdisc add dev ${if_name} clsact
	# Classify PTP on TC 7 and isochron on TC 6
	tc filter add dev ${if_name} egress protocol 0x88f7 \
		flower action skbedit priority 7
	tc filter add dev ${if_name} egress protocol 802.1Q \
		flower vlan_ethtype 0xdead action skbedit priority 6
	tc qdisc add dev ${if_name} handle 100: parent root mqprio num_tc 8 \
		queues 1@0 1@1 1@2 1@3 1@4 1@5 1@6 1@7 \
		map 0 1 2 3 4 5 6 7 \
		hw 1
	# Set up TC 6 for SO_TXTIME. tc-mqprio queues count from 1.
	tc qdisc replace dev ${if_name} parent 100:$((${STREAM_PRIO} + 1)) etf \
		clockid CLOCK_TAI offload delta ${FUDGE_FACTOR}
}

txtime_cleanup()
{
	local if_name=$1

	tc qdisc del dev ${if_name} root
	tc qdisc del dev ${if_name} clsact
}

setup_prepare()
{
	vrf_prepare

	h1_create
	h2_create
	switch_create

	txtime_setup ${h1}

	# Set up swp1 as a master PHC for h1, synchronized to the local
	# CLOCK_REALTIME.
	phc2sys_start ${UDS_ADDRESS_SWP1}

	# Assumption true for LS1028A: h1 and h2 use the same PHC. So by
	# synchronizing h1 to swp1 via PTP, h2 is also implicitly synchronized
	# to swp1 (and both to CLOCK_REALTIME).
	ptp4l_start ${h1} true ${UDS_ADDRESS_H1}
	ptp4l_start ${swp1} false ${UDS_ADDRESS_SWP1}

	# Make sure there are no filter matches at the beginning of the test
	psfp_filter_check 0
}

cleanup()
{
	pre_cleanup

	ptp4l_stop ${swp1}
	ptp4l_stop ${h1}
	phc2sys_stop
	isochron_recv_stop

	txtime_cleanup ${h1}

	h2_destroy
	h1_destroy
	switch_destroy

	vrf_cleanup
}

debug_incorrectly_dropped_packets()
{
	local isochron_dat=$1
	local dropped_seqids
	local seqid

	echo "Packets incorrectly dropped:"

	dropped_seqids=$(isochron report \
		--input-file "${isochron_dat}" \
		--printf-format "%u RX hw %T\n" \
		--printf-args "qR" | \
		grep 'RX hw 0.000000000' | \
		awk '{print $1}')

	for seqid in ${dropped_seqids}; do
		isochron report \
			--input-file "${isochron_dat}" \
			--start ${seqid} --stop ${seqid} \
			--printf-format "seqid %u scheduled for %T, HW TX timestamp %T\n" \
			--printf-args "qST"
	done
}

debug_incorrectly_received_packets()
{
	local isochron_dat=$1

	echo "Packets incorrectly received:"

	isochron report \
		--input-file "${isochron_dat}" \
		--printf-format "seqid %u scheduled for %T, HW TX timestamp %T, HW RX timestamp %T\n" \
		--printf-args "qSTR" |
		grep -v 'HW RX timestamp 0.000000000'
}

run_test()
{
	local base_time=$1
	local expected=$2
	local test_name=$3
	local debug=$4
	local isochron_dat="$(mktemp)"
	local extra_args=""
	local received

	isochron_do \
		"${h1}" \
		"${h2}" \
		"${UDS_ADDRESS_H1}" \
		"" \
		"${base_time}" \
		"${CYCLE_TIME_NS}" \
		"${SHIFT_TIME_NS}" \
		"${NUM_PKTS}" \
		"${STREAM_VID}" \
		"${STREAM_PRIO}" \
		"" \
		"${isochron_dat}"

	# Count all received packets by looking at the non-zero RX timestamps
	received=$(isochron report \
		--input-file "${isochron_dat}" \
		--printf-format "%u\n" --printf-args "R" | \
		grep -w -v '0' | wc -l)

	if [ "${received}" = "${expected}" ]; then
		RET=0
	else
		RET=1
		echo "Expected isochron to receive ${expected} packets but received ${received}"
	fi

	log_test "${test_name}"

	if [ "$RET" = "1" ]; then
		${debug} "${isochron_dat}"
	fi

	rm ${isochron_dat} 2> /dev/null
}

test_gate_in_band()
{
	# Send packets in-band with the OPEN gate entry
	run_test 0.000000000 ${NUM_PKTS} "In band" \
		debug_incorrectly_dropped_packets

	psfp_filter_check ${NUM_PKTS}
}

test_gate_out_of_band()
{
	# Send packets in-band with the CLOSE gate entry
	run_test 0.005000000 0 "Out of band" \
		debug_incorrectly_received_packets

	psfp_filter_check $((2 * ${NUM_PKTS}))
}

trap cleanup EXIT

ALL_TESTS="
	test_gate_in_band
	test_gate_out_of_band
"

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
