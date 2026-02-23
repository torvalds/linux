#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Simple tests for ipvtap


#
# The testing environment looks this way:
#
# |------HNS-------|     |------PHY-------|
# |      veth<----------------->veth      |
# |------|--|------|     |----------------|
#        |  |
#        |  |            |-----TST0-------|
#        |  |------------|----ipvlan      |
#        |               |----------------|
#        |
#        |               |-----TST1-------|
#        |---------------|----ipvlan      |
#                        |----------------|
#

ALL_TESTS="
	test_ip_set
"

source lib.sh

DEBUG=0

VETH_HOST=vethtst.h
VETH_PHY=vethtst.p

NS_COUNT=32
IP_ITERATIONS=1024
IPSET_TIMEOUT="60s"

ns_run() {
	ns=$1
	shift
	if [[ "$ns" == "global" ]]; then
		"$@" >/dev/null
	else
		ip netns exec "$ns" "$@" >/dev/null
	fi
}

test_ip_setup_env() {
	setup_ns NS_PHY
	setup_ns HST_NS

	# setup simulated other-host (phy) and host itself
	ns_run "$HST_NS" ip link add $VETH_HOST type veth peer name $VETH_PHY \
		netns "$NS_PHY" >/dev/null
	ns_run "$HST_NS" ip link set $VETH_HOST up
	ns_run "$NS_PHY" ip link set $VETH_PHY up

	for ((i=0; i<NS_COUNT; i++)); do
		setup_ns ipvlan_ns_$i
		ns="ipvlan_ns_$i"
		if [ "$DEBUG" = "1" ]; then
			echo "created NS ${!ns}"
		fi
		if ! ns_run "$HST_NS" ip link add netns ${!ns} ipvlan0 \
		    link $VETH_HOST \
		    type ipvtap mode l2 bridge; then
			exit_error "FAIL: Failed to configure ipvlan link."
		fi
	done
}

test_ip_cleanup_env() {
	ns_run "$HST_NS" ip link del $VETH_HOST
	cleanup_all_ns
}

exit_error() {
	echo "$1"
	exit $ksft_fail
}

rnd() {
	echo $(( RANDOM % 32 + 16 ))
}

test_ip_set_thread() {
	# Here we are trying to create some IP conflicts between namespaces.
	# If just add/remove IP, nothing interesting will happen.
	# But if add random IP and then remove random IP,
	# eventually conflicts start to apear.
	ip link set ipvlan0 up
	for ((i=0; i<IP_ITERATIONS; i++)); do
		v=$(rnd)
		ip a a "172.25.0.$v/24" dev ipvlan0 2>/dev/null
		ip a a "fc00::$v/64" dev ipvlan0 2>/dev/null
		v=$(rnd)
		ip a d "172.25.0.$v/24" dev ipvlan0 2>/dev/null
		ip a d "fc00::$v/64" dev ipvlan0 2>/dev/null
	done
}

test_ip_set() {
	RET=0

	trap test_ip_cleanup_env EXIT

	test_ip_setup_env

	declare -A ns_pids
	for ((i=0; i<NS_COUNT; i++)); do
		ns="ipvlan_ns_$i"
		ns_run ${!ns} timeout "$IPSET_TIMEOUT" \
			bash -c "$0 test_ip_set_thread"&
		ns_pids[$i]=$!
	done

	for ((i=0; i<NS_COUNT; i++)); do
		wait "${ns_pids[$i]}"
	done

	declare -A all_ips
	for ((i=0; i<NS_COUNT; i++)); do
		ns="ipvlan_ns_$i"
		ip_output=$(ip netns exec ${!ns} ip a l dev ipvlan0 | grep inet)
		while IFS= read -r nsip_out; do
			if [[ -z $nsip_out ]]; then
				continue;
			fi
			nsip=$(awk '{print $2}' <<< "$nsip_out")
			if [[ -v all_ips[$nsip] ]]; then
				RET=$ksft_fail
				log_test "conflict for $nsip"
				return "$RET"
			else
				all_ips[$nsip]=$i
			fi
		done <<< "$ip_output"
	done

	if [ "$DEBUG" = "1" ]; then
		for key in "${!all_ips[@]}"; do
			echo "$key: ${all_ips[$key]}"
		done
	fi

	trap - EXIT
	test_ip_cleanup_env

	log_test "test multithreaded ip set"
}

if [[ "$1" == "-d" ]]; then
	DEBUG=1
	shift
fi

if [[ "$1" == "-t" ]]; then
	shift
	TESTS="$*"
fi

if [[ "$1" == "test_ip_set_thread" ]]; then
	test_ip_set_thread
else
	require_command ip

	tests_run
fi
