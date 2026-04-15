#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# These tests verify that teamd is able to enable and disable ports via the
# active backup runner.
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

export ALL_TESTS="teamd_test_active_backup"

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
NS1_TEAMD_CONF=""
NS2_TEAMD_CONF=""
NS1_TEAMD_PID=""
NS2_TEAMD_PID=""

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
			echo "Invalid option: -${OPTARG}" >&2
			exit 1
			;;
	esac
done

teamd_config_create()
{
	local runner=$1
	local dev=$2
	local conf

	conf=$(mktemp)

	cat > "${conf}" <<-EOF
	{
		"device": "${dev}",
		"runner": {"name": "${runner}"},
		"ports": {
			"eth0": {},
			"eth1": {}
		}
	}
	EOF
	echo "${conf}"
}

# Create the network namespaces, veth pair, and team devices in the specified
# runner.
# Globals:
#   RET - Used by test infra, set by `check_err` functions.
# Arguments:
#   runner - The Teamd runner to use for the Team devices.
environment_create()
{
	local runner=$1

	echo "Setting up two-link aggregation for runner ${runner}"
	echo "Teamd version is: $(teamd --version)"
	trap environment_destroy EXIT

	setup_ns ns1 ns2
	NS1="${NS_LIST[0]}"
	NS2="${NS_LIST[1]}"

	for link in $(seq 0 1); do
		ip -n "${NS1}" link add "eth${link}" type veth peer name \
				"eth${link}" netns "${NS2}"
		check_err $? "Failed to create veth pair"
	done

	NS1_TEAMD_CONF=$(teamd_config_create "${runner}" "test_team1")
	NS2_TEAMD_CONF=$(teamd_config_create "${runner}" "test_team2")
	echo "Conf files are ${NS1_TEAMD_CONF} and ${NS2_TEAMD_CONF}"

	ip netns exec "${NS1}" teamd -d -f "${NS1_TEAMD_CONF}"
	check_err $? "Failed to create team device in ${NS1}"
	NS1_TEAMD_PID=$(pgrep -f "teamd -d -f ${NS1_TEAMD_CONF}")

	ip netns exec "${NS2}" teamd -d -f "${NS2_TEAMD_CONF}"
	check_err $? "Failed to create team device in ${NS2}"
	NS2_TEAMD_PID=$(pgrep -f "teamd -d -f ${NS2_TEAMD_CONF}")

	echo "Created team devices"
	echo "Teamd PIDs are ${NS1_TEAMD_PID} and ${NS2_TEAMD_PID}"

	ip -n "${NS1}" link set test_team1 up
	check_err $? "Failed to set test_team1 up in ${NS1}"
	ip -n "${NS2}" link set test_team2 up
	check_err $? "Failed to set test_team2 up in ${NS2}"

	ip -n "${NS1}" addr add "${NS1_IP}/${PREFIX_LENGTH}" "${NODAD}" dev \
			test_team1
	check_err $? "Failed to add address to team device in ${NS1}"
	ip -n "${NS2}" addr add "${NS2_IP}/${PREFIX_LENGTH}" "${NODAD}" dev \
			test_team2
	check_err $? "Failed to add address to team device in ${NS2}"

	slowwait 2 timeout 0.5 ip netns exec "${NS1}" ping -W 1 -c 1 "${NS2_IP}"
}

# Tear down the environment: kill teamd and delete network namespaces.
environment_destroy()
{
	echo "Tearing down two-link aggregation"

	rm "${NS1_TEAMD_CONF}"
	rm "${NS2_TEAMD_CONF}"

	# First, try graceful teamd teardown.
	ip netns exec "${NS1}" teamd -k -t test_team1
	ip netns exec "${NS2}" teamd -k -t test_team2

	# If teamd can't be killed gracefully, then sigkill.
	if kill -0 "${NS1_TEAMD_PID}" 2>/dev/null; then
		echo "Sending sigkill to teamd for test_team1"
		kill -9 "${NS1_TEAMD_PID}"
		rm -f /var/run/teamd/test_team1.{pid,sock}
	fi
	if kill -0 "${NS2_TEAMD_PID}" 2>/dev/null; then
		echo "Sending sigkill to teamd for test_team2"
		kill -9 "${NS2_TEAMD_PID}"
		rm -f /var/run/teamd/test_team2.{pid,sock}
	fi
	cleanup_all_ns
}

# Change the active port for an active-backup mode team.
# Arguments:
#   namespace - The network namespace that the team is in.
#   team - The name of the team.
#   active_port - The port to make active.
set_active_port()
{
	local namespace=$1
	local team=$2
	local active_port=$3

	ip netns exec "${namespace}" teamdctl "${team}" state item set \
			runner.active_port "${active_port}"
	slowwait 2 bash -c "ip netns exec ${namespace} teamdctl ${team} state \
			item get runner.active_port | grep -q ${active_port}"
}

# Wait for an interface to stop receiving traffic. If it keeps receiving traffic
# for the duration of the timeout, then return an error.
# Arguments:
#   - namespace - The network namespace that the interface is in.
#   - interface - The name of the interface.
wait_to_stop_receiving()
{
	local namespace=$1
	local interface=$2

	echo "Waiting for ${interface} in ${namespace} to stop receiving"
	slowwait 10 check_no_traffic "${interface}" "${NS2_IP}" \
			"${namespace}"
}

# Test that active backup runner can change active ports.
# Globals:
#   RET - Used by test infra, set by `check_err` functions.
teamd_test_active_backup()
{
	export RET=0

	start_listening_and_sending

	### Scenario 1: Don't manually set active port, just make sure team
	# works.
	save_tcpdump_outputs "${NS2}" test_team2
	did_interface_receive test_team2 "${NS2_IP}"
	check_err $? "Traffic did not reach team interface in NS2."
	clear_tcpdump_outputs test_team2

	### Scenario 2: Choose active port.
	set_active_port "${NS1}" test_team1 eth1
	set_active_port "${NS2}" test_team2 eth1

	wait_to_stop_receiving "${NS2}" eth0
	save_tcpdump_outputs "${NS2}" eth0 eth1
	did_interface_receive eth0 "${NS2_IP}"
	check_fail $? "eth0 IS transmitting when inactive"
	did_interface_receive eth1 "${NS2_IP}"
	check_err $? "eth1 not transmitting when active"
	clear_tcpdump_outputs eth0 eth1

	### Scenario 3: Change active port.
	set_active_port "${NS1}" test_team1 eth0
	set_active_port "${NS2}" test_team2 eth0

	wait_to_stop_receiving "${NS2}" eth1
	save_tcpdump_outputs "${NS2}" eth0 eth1
	did_interface_receive eth0 "${NS2_IP}"
	check_err $? "eth0 not transmitting when active"
	did_interface_receive eth1 "${NS2_IP}"
	check_fail $? "eth1 IS transmitting when inactive"
	clear_tcpdump_outputs eth0 eth1

	log_test "teamd active backup runner test"

	stop_sending_and_listening
}

require_command teamd
require_command teamdctl
require_command iperf3
require_command tcpdump
environment_create activebackup
tests_run
exit "${EXIT_STATUS}"
