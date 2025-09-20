#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test if a bond interface works with lacp_active=off.

# shellcheck disable=SC2034
REQUIRE_MZ=no
NUM_NETIFS=0
lib_dir=$(dirname "$0")
# shellcheck disable=SC1091
source "$lib_dir"/../../../net/forwarding/lib.sh

# shellcheck disable=SC2317
check_port_state()
{
	local netns=$1
	local port=$2
	local state=$3

	ip -n "${netns}" -d -j link show "$port" | \
		jq -e ".[].linkinfo.info_slave_data.ad_actor_oper_port_state_str | index(\"${state}\") != null" > /dev/null
}

check_pkt_count()
{
	RET=0
	local ns="$1"
	local iface="$2"

	# wait 65s, one per 30s
	slowwait_for_counter 65 2 tc_rule_handle_stats_get \
		"dev ${iface} egress" 101 ".packets" "-n ${ns}" &> /dev/null
}

setup() {
	setup_ns c_ns s_ns

	# shellcheck disable=SC2154
	ip -n "${c_ns}" link add eth0 type veth peer name eth0 netns "${s_ns}"
	ip -n "${c_ns}" link add eth1 type veth peer name eth1 netns "${s_ns}"

	# Add tc filter to count the pkts
	tc -n "${c_ns}" qdisc add dev eth0 clsact
	tc -n "${c_ns}" filter add dev eth0 egress handle 101 protocol 0x8809 matchall action pass
	tc -n "${s_ns}" qdisc add dev eth1 clsact
	tc -n "${s_ns}" filter add dev eth1 egress handle 101 protocol 0x8809 matchall action pass

	ip -n "${s_ns}" link add bond0 type bond mode 802.3ad lacp_active on lacp_rate fast
	ip -n "${s_ns}" link set eth0 master bond0
	ip -n "${s_ns}" link set eth1 master bond0

	ip -n "${c_ns}" link add bond0 type bond mode 802.3ad lacp_active off lacp_rate fast
	ip -n "${c_ns}" link set eth0 master bond0
	ip -n "${c_ns}" link set eth1 master bond0

}

trap cleanup_all_ns EXIT
setup

# The bond will send 2 lacpdu pkts during init time, let's wait at least 2s
# after interface up
ip -n "${c_ns}" link set bond0 up
sleep 2

# 1. The passive side shouldn't send LACPDU.
check_pkt_count "${c_ns}" "eth0" && RET=1
log_test "802.3ad lacp_active off" "init port"

ip -n "${s_ns}" link set bond0 up
# 2. The passive side should not have the 'active' flag.
RET=0
slowwait 2 check_port_state "${c_ns}" "eth0" "active" && RET=1
log_test "802.3ad lacp_active off" "port state active"

# 3. The active side should have the 'active' flag.
RET=0
slowwait 2 check_port_state "${s_ns}" "eth0" "active" || RET=1
log_test "802.3ad lacp_active on" "port state active"

# 4. Make sure the connection is not expired.
RET=0
slowwait 5 check_port_state "${s_ns}" "eth0" "distributing"
slowwait 10 check_port_state "${s_ns}" "eth0" "expired" && RET=1
log_test "bond 802.3ad lacp_active off" "port connection"

# After testing, disconnect one port on each side to check the state.
ip -n "${s_ns}" link set eth0 nomaster
ip -n "${s_ns}" link set eth0 up
ip -n "${c_ns}" link set eth1 nomaster
ip -n "${c_ns}" link set eth1 up
# Due to Periodic Machine and Rx Machine state change, the bond will still
# send lacpdu pkts in a few seconds. sleep at lease 5s to make sure
# negotiation finished
sleep 5

# 5. The active side should keep sending LACPDU.
check_pkt_count "${s_ns}" "eth1" || RET=1
log_test "bond 802.3ad lacp_active on" "port pkt after disconnect"

# 6. The passive side shouldn't send LACPDU anymore.
check_pkt_count "${c_ns}" "eth0" && RET=1
log_test "bond 802.3ad lacp_active off" "port pkt after disconnect"

exit "$EXIT_STATUS"
