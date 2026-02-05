#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# shellcheck disable=SC2329

source ../lib.sh

ALL_TESTS="
	test_clean_hsrv0
	test_cut_link_hsrv0
	test_packet_loss_hsrv0
	test_high_packet_loss_hsrv0
	test_reordering_hsrv0

	test_clean_hsrv1
	test_cut_link_hsrv1
	test_packet_loss_hsrv1
	test_high_packet_loss_hsrv1
	test_reordering_hsrv1

	test_clean_prp
	test_cut_link_prp
	test_packet_loss_prp
	test_high_packet_loss_prp
	test_reordering_prp
"

# The tests are running ping for 5sec with a relatively short interval in
# different scenarios with faulty links (cut links, packet loss, delay,
# reordering) that should be recoverable by HSR/PRP. The ping interval (10ms)
# is short enough that the base delay (50ms) leads to a queue in the netem
# qdiscs which is needed for reordering.

setup_hsr_topo()
{
	# Three HSR nodes in a ring, every node has a LAN A interface connected
	# to the LAN B interface of the next node.
	#
	#    node1            node2
	#
	#     vethA -------- vethB
	#   hsr1                 hsr2
	#     vethB          vethA
	#         \          /
	#         vethA  vethB
	#             hsr3
	#
	#            node3

	local ver="$1"

	setup_ns node1 node2 node3

	# veth links
	# shellcheck disable=SC2154 # variables assigned by setup_ns
	ip link add vethA netns "$node1" type veth peer name vethB netns "$node2"
	# shellcheck disable=SC2154 # variables assigned by setup_ns
	ip link add vethA netns "$node2" type veth peer name vethB netns "$node3"
	ip link add vethA netns "$node3" type veth peer name vethB netns "$node1"

	# MAC addresses (not needed for HSR operation, but helps with debugging)
	ip -net "$node1" link set address 00:11:22:00:01:01 dev vethA
	ip -net "$node1" link set address 00:11:22:00:01:02 dev vethB

	ip -net "$node2" link set address 00:11:22:00:02:01 dev vethA
	ip -net "$node2" link set address 00:11:22:00:02:02 dev vethB

	ip -net "$node3" link set address 00:11:22:00:03:01 dev vethA
	ip -net "$node3" link set address 00:11:22:00:03:02 dev vethB

	# HSR interfaces
	ip -net "$node1" link add name hsr1 type hsr proto 0 version "$ver" \
		slave1 vethA slave2 vethB supervision 45
	ip -net "$node2" link add name hsr2 type hsr proto 0 version "$ver" \
		slave1 vethA slave2 vethB supervision 45
	ip -net "$node3" link add name hsr3 type hsr proto 0 version "$ver" \
		slave1 vethA slave2 vethB supervision 45

	# IP addresses
	ip -net "$node1" addr add 100.64.0.1/24 dev hsr1
	ip -net "$node2" addr add 100.64.0.2/24 dev hsr2
	ip -net "$node3" addr add 100.64.0.3/24 dev hsr3

	# Set all links up
	ip -net "$node1" link set vethA up
	ip -net "$node1" link set vethB up
	ip -net "$node1" link set hsr1 up

	ip -net "$node2" link set vethA up
	ip -net "$node2" link set vethB up
	ip -net "$node2" link set hsr2 up

	ip -net "$node3" link set vethA up
	ip -net "$node3" link set vethB up
	ip -net "$node3" link set hsr3 up
}

setup_prp_topo()
{
	# Two PRP nodes, connected by two links (treated as LAN A and LAN B).
	#
	#       vethA ----- vethA
	#     prp1             prp2
	#       vethB ----- vethB
	#
	#     node1           node2

	setup_ns node1 node2

	# veth links
	ip link add vethA netns "$node1" type veth peer name vethA netns "$node2"
	ip link add vethB netns "$node1" type veth peer name vethB netns "$node2"

	# MAC addresses will be copied from LAN A interface
	ip -net "$node1" link set address 00:11:22:00:00:01 dev vethA
	ip -net "$node2" link set address 00:11:22:00:00:02 dev vethA

	# PRP interfaces
	ip -net "$node1" link add name prp1 type hsr \
		slave1 vethA slave2 vethB supervision 45 proto 1
	ip -net "$node2" link add name prp2 type hsr \
		slave1 vethA slave2 vethB supervision 45 proto 1

	# IP addresses
	ip -net "$node1" addr add 100.64.0.1/24 dev prp1
	ip -net "$node2" addr add 100.64.0.2/24 dev prp2

	# All links up
	ip -net "$node1" link set vethA up
	ip -net "$node1" link set vethB up
	ip -net "$node1" link set prp1 up

	ip -net "$node2" link set vethA up
	ip -net "$node2" link set vethB up
	ip -net "$node2" link set prp2 up
}

wait_for_hsr_node_table()
{
	log_info "Wait for node table entries to be merged."
	WAIT=5
	while [ "${WAIT}" -gt 0 ]; do
		nts=$(cat /sys/kernel/debug/hsr/hsr*/node_table)

		# We need entries in the node tables, and they need to be merged
		if (echo "$nts" | grep -qE "^([0-9a-f]{2}:){5}") && \
		    ! (echo "$nts" | grep -q "00:00:00:00:00:00"); then
			return
		fi

		sleep 1
		((WAIT--))
	done
	check_err 1 "Failed to wait for merged node table entries"
}

setup_topo()
{
	local proto="$1"

	if [ "$proto" = "HSRv0" ]; then
		setup_hsr_topo 0
		wait_for_hsr_node_table
	elif [ "$proto" = "HSRv1" ]; then
		setup_hsr_topo 1
		wait_for_hsr_node_table
	elif [ "$proto" = "PRP" ]; then
		setup_prp_topo
	else
		check_err 1 "Unknown protocol (${proto})"
	fi
}

check_ping()
{
	local node="$1"
	local dst="$2"
	local accepted_dups="$3"
	local ping_args="-q -i 0.01 -c 400"

	log_info "Running ping $node -> $dst"
	# shellcheck disable=SC2086
	output=$(ip netns exec "$node" ping $ping_args "$dst" | \
		grep "packets transmitted")
	log_info "$output"

	dups=0
	loss=0

	if [[ "$output" =~ \+([0-9]+)" duplicates" ]]; then
		dups="${BASH_REMATCH[1]}"
	fi
	if [[ "$output" =~ ([0-9\.]+\%)" packet loss" ]]; then
		loss="${BASH_REMATCH[1]}"
	fi

	if [ "$dups" -gt "$accepted_dups" ]; then
		check_err 1 "Unexpected duplicate packets (${dups})"
	fi
	if [ "$loss" != "0%" ]; then
		check_err 1 "Unexpected packet loss (${loss})"
	fi
}

test_clean()
{
	local proto="$1"

	RET=0
	tname="${FUNCNAME[0]} - ${proto}"

	setup_topo "$proto"
	if ((RET != ksft_pass)); then
		log_test "${tname} setup"
		return
	fi

	check_ping "$node1" "100.64.0.2" 0

	log_test "${tname}"
}

test_clean_hsrv0()
{
	test_clean "HSRv0"
}

test_clean_hsrv1()
{
	test_clean "HSRv1"
}

test_clean_prp()
{
	test_clean "PRP"
}

test_cut_link()
{
	local proto="$1"

	RET=0
	tname="${FUNCNAME[0]} - ${proto}"

	setup_topo "$proto"
	if ((RET != ksft_pass)); then
		log_test "${tname} setup"
		return
	fi

	# Cutting link from subshell, so check_ping can run in the normal shell
	# with access to global variables from the test harness.
	(
		sleep 2
		log_info "Cutting link"
		ip -net "$node1" link set vethB down
	) &
	check_ping "$node1" "100.64.0.2" 0

	wait
	log_test "${tname}"
}


test_cut_link_hsrv0()
{
	test_cut_link "HSRv0"
}

test_cut_link_hsrv1()
{
	test_cut_link "HSRv1"
}

test_cut_link_prp()
{
	test_cut_link "PRP"
}

test_packet_loss()
{
	local proto="$1"
	local loss="$2"

	RET=0
	tname="${FUNCNAME[0]} - ${proto}, ${loss}"

	setup_topo "$proto"
	if ((RET != ksft_pass)); then
		log_test "${tname} setup"
		return
	fi

	# Packet loss with lower delay makes sure the packets on the lossy link
	# arrive first.
	tc -net "$node1" qdisc add dev vethA root netem delay 50ms
	tc -net "$node1" qdisc add dev vethB root netem delay 20ms loss "$loss"

	check_ping "$node1" "100.64.0.2" 40

	log_test "${tname}"
}

test_packet_loss_hsrv0()
{
	test_packet_loss "HSRv0" "20%"
}

test_packet_loss_hsrv1()
{
	test_packet_loss "HSRv1" "20%"
}

test_packet_loss_prp()
{
	test_packet_loss "PRP" "20%"
}

test_high_packet_loss_hsrv0()
{
	test_packet_loss "HSRv0" "80%"
}

test_high_packet_loss_hsrv1()
{
	test_packet_loss "HSRv1" "80%"
}

test_high_packet_loss_prp()
{
	test_packet_loss "PRP" "80%"
}

test_reordering()
{
	local proto="$1"

	RET=0
	tname="${FUNCNAME[0]} - ${proto}"

	setup_topo "$proto"
	if ((RET != ksft_pass)); then
		log_test "${tname} setup"
		return
	fi

	tc -net "$node1" qdisc add dev vethA root netem delay 50ms
	tc -net "$node1" qdisc add dev vethB root netem delay 50ms reorder 20%

	check_ping "$node1" "100.64.0.2" 40

	log_test "${tname}"
}

test_reordering_hsrv0()
{
	test_reordering "HSRv0"
}

test_reordering_hsrv1()
{
	test_reordering "HSRv1"
}

test_reordering_prp()
{
	test_reordering "PRP"
}

cleanup()
{
	cleanup_all_ns
}

trap cleanup EXIT

tests_run

exit $EXIT_STATUS
