#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# These tests verify the basic failover capability of the team driver via the
# `enabled` team driver option across different team driver modes. This does not
# rely on teamd, and instead just uses teamnl to set the `enabled` option
# directly.
#
# Topology:
#
#  +-------------------------+  NS1
#  |        test_team1       |
#  |            +            |
#  |      eth0  |  eth1      |
#  |        +---+---+        |
#  |        |       |        |
#  +-------------------------+
#           |       |
#  +-------------------------+  NS2
#  |        |       |        |
#  |        +-------+        |
#  |      eth0  |  eth1      |
#  |            +            |
#  |        test_team2       |
#  +-------------------------+

export ALL_TESTS="team_test_failover"

test_dir="$(dirname "$0")"
# shellcheck disable=SC1091
source "${test_dir}/../../../net/lib.sh"
# shellcheck disable=SC1091
source "${test_dir}/team_lib.sh"

NS1=""
NS2=""
export NODAD="nodad"
PREFIX_LENGTH="64"
NS1_IP="fd00::1"
NS2_IP="fd00::2"
NS1_IP4="192.168.0.1"
NS2_IP4="192.168.0.2"
MEMBERS=("eth0" "eth1")

while getopts "4" opt; do
	case $opt in
		4)
			echo "IPv4 mode selected."
			export NODAD=
			PREFIX_LENGTH="24"
			NS1_IP="${NS1_IP4}"
			NS2_IP="${NS2_IP4}"
			;;
		\?)
			echo "Invalid option: -$OPTARG" >&2
			exit 1
			;;
	esac
done

# Create the network namespaces, veth pair, and team devices in the specified
# mode.
# Globals:
#   RET - Used by test infra, set by `check_err` functions.
# Arguments:
#   mode - The team driver mode to use for the team devices.
environment_create()
{
	trap cleanup_all_ns EXIT
	setup_ns ns1 ns2
	NS1="${NS_LIST[0]}"
	NS2="${NS_LIST[1]}"

	# Create the interfaces.
	ip -n "${NS1}" link add eth0 type veth peer name eth0 netns "${NS2}"
	ip -n "${NS1}" link add eth1 type veth peer name eth1 netns "${NS2}"
	ip -n "${NS1}" link add test_team1 type team
	ip -n "${NS2}" link add test_team2 type team

	# Set up the receiving network namespace's team interface.
	setup_team "${NS2}" test_team2 roundrobin "${NS2_IP}" \
			"${PREFIX_LENGTH}" "${MEMBERS[@]}"
}


# Check that failover works for a specific team driver mode.
# Globals:
#   RET - Used by test infra, set by `check_err` functions.
# Arguments:
#   mode - The mode to set the team interfaces to.
team_test_mode_failover()
{
	local mode="$1"
	export RET=0

	# Set up the sender team with the correct mode.
	setup_team "${NS1}" test_team1 "${mode}" "${NS1_IP}" \
			"${PREFIX_LENGTH}" "${MEMBERS[@]}"
	check_err $? "Failed to set up sender team"

	start_listening_and_sending

	### Scenario 1: All interfaces initially enabled.
	save_tcpdump_outputs "${NS2}" "${MEMBERS[@]}"
	did_interface_receive eth0 "${NS2_IP}"
	check_err $? "eth0 not transmitting when both links enabled"
	did_interface_receive eth1 "${NS2_IP}"
	check_err $? "eth1 not transmitting when both links enabled"
	clear_tcpdump_outputs "${MEMBERS[@]}"

	### Scenario 2: One tx-side interface disabled.
	ip netns exec "${NS1}" teamnl test_team1 setoption enabled false \
			--port=eth1
	slowwait 2 bash -c "ip netns exec ${NS1} teamnl test_team1 getoption \
			enabled --port=eth1 | grep -q false"

	save_tcpdump_outputs "${NS2}" "${MEMBERS[@]}"
	did_interface_receive eth0 "${NS2_IP}"
	check_err $? "eth0 not transmitting when enabled"
	did_interface_receive eth1 "${NS2_IP}"
	check_fail $? "eth1 IS transmitting when disabled"
	clear_tcpdump_outputs "${MEMBERS[@]}"

	### Scenario 3: The interface is re-enabled.
	ip netns exec "${NS1}" teamnl test_team1 setoption enabled true \
			--port=eth1
	slowwait 2 bash -c "ip netns exec ${NS1} teamnl test_team1 getoption \
			enabled --port=eth1 | grep -q true"

	save_tcpdump_outputs "${NS2}" "${MEMBERS[@]}"
	did_interface_receive eth0 "${NS2_IP}"
	check_err $? "eth0 not transmitting when both links enabled"
	did_interface_receive eth1 "${NS2_IP}"
	check_err $? "eth1 not transmitting when both links enabled"
	clear_tcpdump_outputs "${MEMBERS[@]}"

	log_test "Failover of '${mode}' test"

	# Clean up
	stop_sending_and_listening
}

team_test_failover()
{
	team_test_mode_failover broadcast
	team_test_mode_failover roundrobin
	team_test_mode_failover random
	# Don't test `activebackup` or `loadbalance` modes, since they are too
	# complicated for just setting `enabled` to work. They use more than
	# the `enabled` option for transmit.
}

require_command teamnl
require_command iperf3
require_command tcpdump
environment_create
tests_run
exit "${EXIT_STATUS}"
