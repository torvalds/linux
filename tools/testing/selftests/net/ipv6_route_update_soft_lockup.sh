#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Testing for potential kernel soft lockup during IPv6 routing table
# refresh under heavy outgoing IPv6 traffic. If a kernel soft lockup
# occurs, a kernel panic will be triggered to prevent associated issues.
#
#
#                            Test Environment Layout
#
# ┌----------------┐                                         ┌----------------┐
# |     SOURCE_NS  |                                         |     SINK_NS    |
# |    NAMESPACE   |                                         |    NAMESPACE   |
# |(iperf3 clients)|                                         |(iperf3 servers)|
# |                |                                         |                |
# |                |                                         |                |
# |    ┌-----------|                             nexthops    |---------┐      |
# |    |veth_source|<--------------------------------------->|veth_sink|<┐    |
# |    └-----------|2001:0DB8:1::0:1/96  2001:0DB8:1::1:1/96 |---------┘ |    |
# |                |         ^           2001:0DB8:1::1:2/96 |           |    |
# |                |         .                   .           |       fwd |    |
# |  ┌---------┐   |         .                   .           |           |    |
# |  |   IPv6  |   |         .                   .           |           V    |
# |  | routing |   |         .           2001:0DB8:1::1:80/96|        ┌-----┐ |
# |  |  table  |   |         .                               |        | lo  | |
# |  | nexthop |   |         .                               └--------┴-----┴-┘
# |  | update  |   |         ............................> 2001:0DB8:2::1:1/128
# |  └-------- ┘   |
# └----------------┘
#
# The test script sets up two network namespaces, source_ns and sink_ns,
# connected via a veth link. Within source_ns, it continuously updates the
# IPv6 routing table by flushing and inserting IPV6_NEXTHOP_ADDR_COUNT nexthop
# IPs destined for SINK_LOOPBACK_IP_ADDR in sink_ns. This refresh occurs at a
# rate of 1/ROUTING_TABLE_REFRESH_PERIOD per second for TEST_DURATION seconds.
#
# Simultaneously, multiple iperf3 clients within source_ns generate heavy
# outgoing IPv6 traffic. Each client is assigned a unique port number starting
# at 5000 and incrementing sequentially. Each client targets a unique iperf3
# server running in sink_ns, connected to the SINK_LOOPBACK_IFACE interface
# using the same port number.
#
# The number of iperf3 servers and clients is set to half of the total
# available cores on each machine.
#
# NOTE: We have tested this script on machines with various CPU specifications,
# ranging from lower to higher performance as listed below. The test script
# effectively triggered a kernel soft lockup on machines running an unpatched
# kernel in under a minute:
#
# - 1x Intel Xeon E-2278G 8-Core Processor @ 3.40GHz
# - 1x Intel Xeon E-2378G Processor 8-Core @ 2.80GHz
# - 1x AMD EPYC 7401P 24-Core Processor @ 2.00GHz
# - 1x AMD EPYC 7402P 24-Core Processor @ 2.80GHz
# - 2x Intel Xeon Gold 5120 14-Core Processor @ 2.20GHz
# - 1x Ampere Altra Q80-30 80-Core Processor @ 3.00GHz
# - 2x Intel Xeon Gold 5120 14-Core Processor @ 2.20GHz
# - 2x Intel Xeon Silver 4214 24-Core Processor @ 2.20GHz
# - 1x AMD EPYC 7502P 32-Core @ 2.50GHz
# - 1x Intel Xeon Gold 6314U 32-Core Processor @ 2.30GHz
# - 2x Intel Xeon Gold 6338 32-Core Processor @ 2.00GHz
#
# On less performant machines, you may need to increase the TEST_DURATION
# parameter to enhance the likelihood of encountering a race condition leading
# to a kernel soft lockup and avoid a false negative result.
#
# NOTE: The test may not produce the expected result in virtualized
# environments (e.g., qemu) due to differences in timing and CPU handling,
# which can affect the conditions needed to trigger a soft lockup.

source lib.sh
source net_helper.sh

TEST_DURATION=300
ROUTING_TABLE_REFRESH_PERIOD=0.01

IPERF3_BITRATE="300m"


IPV6_NEXTHOP_ADDR_COUNT="128"
IPV6_NEXTHOP_ADDR_MASK="96"
IPV6_NEXTHOP_PREFIX="2001:0DB8:1"


SOURCE_TEST_IFACE="veth_source"
SOURCE_TEST_IP_ADDR="2001:0DB8:1::0:1/96"

SINK_TEST_IFACE="veth_sink"
# ${SINK_TEST_IFACE} is populated with the following range of IPv6 addresses:
# 2001:0DB8:1::1:1  to 2001:0DB8:1::1:${IPV6_NEXTHOP_ADDR_COUNT}
SINK_LOOPBACK_IFACE="lo"
SINK_LOOPBACK_IP_MASK="128"
SINK_LOOPBACK_IP_ADDR="2001:0DB8:2::1:1"

nexthop_ip_list=""
termination_signal=""
kernel_softlokup_panic_prev_val=""

terminate_ns_processes_by_pattern() {
	local ns=$1
	local pattern=$2

	for pid in $(ip netns pids ${ns}); do
		[ -e /proc/$pid/cmdline ] && grep -qe "${pattern}" /proc/$pid/cmdline && kill -9 $pid
	done
}

cleanup() {
	echo "info: cleaning up namespaces and terminating all processes within them..."


	# Terminate iperf3 instances running in the source_ns. To avoid race
	# conditions, first iterate over the PIDs and terminate those
	# associated with the bash shells running the
	# `while true; do iperf3 -c ...; done` loops. In a second iteration,
	# terminate the individual `iperf3 -c ...` instances.
	terminate_ns_processes_by_pattern ${source_ns} while
	terminate_ns_processes_by_pattern ${source_ns} iperf3

	# Repeat the same process for sink_ns
	terminate_ns_processes_by_pattern ${sink_ns} while
	terminate_ns_processes_by_pattern ${sink_ns} iperf3

	# Check if any iperf3 instances are still running. This could happen
	# if a core has entered an infinite loop and the timeout for detecting
	# the soft lockup has not expired, but either the test interval has
	# already elapsed or the test was terminated manually (e.g., with ^C)
	for pid in $(ip netns pids ${source_ns}); do
		if [ -e /proc/$pid/cmdline ] && grep -qe 'iperf3' /proc/$pid/cmdline; then
			echo "FAIL: unable to terminate some iperf3 instances. Soft lockup is underway. A kernel panic is on the way!"
			exit ${ksft_fail}
		fi
	done

	if [ "$termination_signal" == "SIGINT" ]; then
		echo "SKIP: Termination due to ^C (SIGINT)"
	elif [ "$termination_signal" == "SIGALRM" ]; then
		echo "PASS: No kernel soft lockup occurred during this ${TEST_DURATION} second test"
	fi

	cleanup_ns ${source_ns} ${sink_ns}

	sysctl -qw kernel.softlockup_panic=${kernel_softlokup_panic_prev_val}
}

setup_prepare() {
	setup_ns source_ns sink_ns

	ip -n ${source_ns} link add name ${SOURCE_TEST_IFACE} type veth peer name ${SINK_TEST_IFACE} netns ${sink_ns}

	# Setting up the Source namespace
	ip -n ${source_ns} addr add ${SOURCE_TEST_IP_ADDR} dev ${SOURCE_TEST_IFACE}
	ip -n ${source_ns} link set dev ${SOURCE_TEST_IFACE} qlen 10000
	ip -n ${source_ns} link set dev ${SOURCE_TEST_IFACE} up
	ip netns exec ${source_ns} sysctl -qw net.ipv6.fib_multipath_hash_policy=1

	# Setting up the Sink namespace
	ip -n ${sink_ns} addr add ${SINK_LOOPBACK_IP_ADDR}/${SINK_LOOPBACK_IP_MASK} dev ${SINK_LOOPBACK_IFACE}
	ip -n ${sink_ns} link set dev ${SINK_LOOPBACK_IFACE} up
	ip netns exec ${sink_ns} sysctl -qw net.ipv6.conf.${SINK_LOOPBACK_IFACE}.forwarding=1

	ip -n ${sink_ns} link set ${SINK_TEST_IFACE} up
	ip netns exec ${sink_ns} sysctl -qw net.ipv6.conf.${SINK_TEST_IFACE}.forwarding=1


	# Populate nexthop IPv6 addresses on the test interface in the sink_ns
	echo "info: populating ${IPV6_NEXTHOP_ADDR_COUNT} IPv6 addresses on the ${SINK_TEST_IFACE} interface ..."
	for IP in $(seq 1 ${IPV6_NEXTHOP_ADDR_COUNT}); do
		ip -n ${sink_ns} addr add ${IPV6_NEXTHOP_PREFIX}::$(printf "1:%x" "${IP}")/${IPV6_NEXTHOP_ADDR_MASK} dev ${SINK_TEST_IFACE};
	done

	# Preparing list of nexthops
	for IP in $(seq 1 ${IPV6_NEXTHOP_ADDR_COUNT}); do
		nexthop_ip_list=$nexthop_ip_list" nexthop via ${IPV6_NEXTHOP_PREFIX}::$(printf "1:%x" $IP) dev ${SOURCE_TEST_IFACE} weight 1"
	done
}


test_soft_lockup_during_routing_table_refresh() {
	# Start num_of_iperf_servers iperf3 servers in the sink_ns namespace,
	# each listening on ports starting at 5001 and incrementing
	# sequentially. Since iperf3 instances may terminate unexpectedly, a
	# while loop is used to automatically restart them in such cases.
	echo "info: starting ${num_of_iperf_servers} iperf3 servers in the sink_ns namespace ..."
	for i in $(seq 1 ${num_of_iperf_servers}); do
		cmd="iperf3 --bind ${SINK_LOOPBACK_IP_ADDR} -s -p $(printf '5%03d' ${i}) --rcv-timeout 200 &>/dev/null"
		ip netns exec ${sink_ns} bash -c "while true; do ${cmd}; done &" &>/dev/null
	done

	# Wait for the iperf3 servers to be ready
	for i in $(seq ${num_of_iperf_servers}); do
		port=$(printf '5%03d' ${i});
		wait_local_port_listen ${sink_ns} ${port} tcp
	done

	# Continuously refresh the routing table in the background within
	# the source_ns namespace
	ip netns exec ${source_ns} bash -c "
		while \$(ip netns list | grep -q ${source_ns}); do
			ip -6 route add ${SINK_LOOPBACK_IP_ADDR}/${SINK_LOOPBACK_IP_MASK} ${nexthop_ip_list};
			sleep ${ROUTING_TABLE_REFRESH_PERIOD};
			ip -6 route delete ${SINK_LOOPBACK_IP_ADDR}/${SINK_LOOPBACK_IP_MASK};
		done &"

	# Start num_of_iperf_servers iperf3 clients in the source_ns namespace,
	# each sending TCP traffic on sequential ports starting at 5001.
	# Since iperf3 instances may terminate unexpectedly (e.g., if the route
	# to the server is deleted in the background during a route refresh), a
	# while loop is used to automatically restart them in such cases.
	echo "info: starting ${num_of_iperf_servers} iperf3 clients in the source_ns namespace ..."
	for i in $(seq 1 ${num_of_iperf_servers}); do
		cmd="iperf3 -c ${SINK_LOOPBACK_IP_ADDR} -p $(printf '5%03d' ${i}) --length 64 --bitrate ${IPERF3_BITRATE} -t 0 --connect-timeout 150 &>/dev/null"
		ip netns exec ${source_ns} bash -c "while true; do ${cmd}; done &" &>/dev/null
	done

	echo "info: IPv6 routing table is being updated at the rate of $(echo "1/${ROUTING_TABLE_REFRESH_PERIOD}" | bc)/s for ${TEST_DURATION} seconds ..."
	echo "info: A kernel soft lockup, if detected, results in a kernel panic!"

	wait
}

# Make sure 'iperf3' is installed, skip the test otherwise
if [ ! -x "$(command -v "iperf3")" ]; then
	echo "SKIP: 'iperf3' is not installed. Skipping the test."
	exit ${ksft_skip}
fi

# Determine the number of cores on the machine
num_of_iperf_servers=$(( $(nproc)/2 ))

# Check if we are running on a multi-core machine, skip the test otherwise
if [ "${num_of_iperf_servers}" -eq 0 ]; then
	echo "SKIP: This test is not valid on a single core machine!"
	exit ${ksft_skip}
fi

# Since the kernel soft lockup we're testing causes at least one core to enter
# an infinite loop, destabilizing the host and likely affecting subsequent
# tests, we trigger a kernel panic instead of reporting a failure and
# continuing
kernel_softlokup_panic_prev_val=$(sysctl -n kernel.softlockup_panic)
sysctl -qw kernel.softlockup_panic=1

handle_sigint() {
	termination_signal="SIGINT"
	cleanup
	exit ${ksft_skip}
}

handle_sigalrm() {
	termination_signal="SIGALRM"
	cleanup
	exit ${ksft_pass}
}

trap handle_sigint SIGINT
trap handle_sigalrm SIGALRM

(sleep ${TEST_DURATION} && kill -s SIGALRM $$)&

setup_prepare
test_soft_lockup_during_routing_table_refresh
