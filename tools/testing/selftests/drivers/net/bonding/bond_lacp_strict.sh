#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Testing if bond lacp_strict works
#
#          Partner (p_ns)
#  +--------------------------+
#  |          bond0           |
#  |            +             |
#  |      eth0  |  eth1       |
#  |        +---+---+         |
#  |        |       |         |
#  +--------------------------+
#  |        | eth0  | eth1    |
#  |        |       |         |
#  |(br_ns) | br0   | br1     |
#  |        |       |         |
#  |        | eth2  | eth3    |
#  +--------------------------+
#  |        |       |         |
#  |        +---+---+         |
#  |      eth0  |  eth1       |
#  |            +             |
#  |          bond0           |
#  +--------------------------+
#         Dut (d_ns)

lib_dir=$(dirname "$0")
# shellcheck disable=SC1090
source "$lib_dir"/../../../net/lib.sh

COLLECTING_DISTRIBUTING_MASK=48
COLLECTING_DISTRIBUTING=48
FAILED=0

setup_links()
{
	# shellcheck disable=SC2154
	ip -n "${p_ns}" link add eth0 type veth peer name eth0 netns "${br_ns}"
	ip -n "${p_ns}" link add eth1 type veth peer name eth1 netns "${br_ns}"
	ip -n "${d_ns}" link add eth0 type veth peer name eth2 netns "${br_ns}"
	ip -n "${d_ns}" link add eth1 type veth peer name eth3 netns "${br_ns}"

	ip -n "${br_ns}" link add br0 type bridge
	ip -n "${br_ns}" link add br1 type bridge

	ip -n "${br_ns}" link set dev br0 type bridge stp_state 0
	ip -n "${br_ns}" link set dev br1 type bridge stp_state 0

	ip -n "${br_ns}" link set eth0 master br0
	ip -n "${br_ns}" link set eth2 master br0
	ip -n "${br_ns}" link set eth1 master br1
	ip -n "${br_ns}" link set eth3 master br1

	# Allow LACP trames forwarding on bridge ports
	ip netns exec "${br_ns}" sh -c "echo 4 > /sys/class/net/br0/brif/eth0/group_fwd_mask"
	ip netns exec "${br_ns}" sh -c "echo 4 > /sys/class/net/br1/brif/eth1/group_fwd_mask"
	ip netns exec "${br_ns}" sh -c "echo 4 > /sys/class/net/br0/brif/eth2/group_fwd_mask"
	ip netns exec "${br_ns}" sh -c "echo 4 > /sys/class/net/br1/brif/eth3/group_fwd_mask"

	ip -n "${br_ns}" link set eth0 up
	ip -n "${br_ns}" link set eth2 up
	ip -n "${br_ns}" link set eth1 up
	ip -n "${br_ns}" link set eth3 up

	ip -n "${br_ns}" link set br0 up
	ip -n "${br_ns}" link set br1 up

	ip -n "${d_ns}" link add bond0 type bond mode 802.3ad miimon 100 \
		lacp_rate fast min_links 1
	ip -n "${p_ns}" link add bond0 type bond mode 802.3ad miimon 100 \
		lacp_rate fast min_links 1

	ip -n "${d_ns}" link set eth0 master bond0
	ip -n "${d_ns}" link set eth1 master bond0
	ip -n "${p_ns}" link set eth0 master bond0
	ip -n "${p_ns}" link set eth1 master bond0

	ip -n "${d_ns}" link set bond0 up
	ip -n "${p_ns}" link set bond0 up
}

test_master_carrier() {
	local expected=$1
	local mode_name=$2
	local carrier

	carrier=$(ip netns exec "${d_ns}" cat /sys/class/net/bond0/carrier)
	[ "$carrier" == "1" ] && carrier="up" || carrier="down"

	[ "$carrier" == "$expected" ] && return

	echo "FAIL: Expected carrier $expected in lacp_strict $mode_name mode, got $carrier"

	RET=1

}

compare_state() {
	local actual_state=$1
	local expected_state=$2
	local iface=$3
	local last_attempt=$4

    [ $((actual_state & COLLECTING_DISTRIBUTING_MASK)) -eq "$expected_state" ] \
		&& return 0

	[ "$last_attempt" -ne 1 ] && return 1

	printf "FAIL: Expected LACP %s actor state to " "$iface"
	if [ "$expected_state" -eq $COLLECTING_DISTRIBUTING ]; then
		echo "be in Collecting/Distributing state"
	else
		echo "have neither Collecting nor Distributing set."
	fi

	return 1
}

_test_lacp_port_state() {
	local interface=$1
	local expected=$2
	local last_attempt=$3
	local eth0_actor_state eth1_actor_state
	local ret=0

	# shellcheck disable=SC2016
	while IFS='=' read -r k v; do
		printf -v "$k" '%s' "$v"
	done < <(
		ip netns exec "${d_ns}" awk '
		/^Slave Interface: / { iface=$3 }
		/details actor lacp pdu:/ { ctx="actor" }
		/details partner lacp pdu:/ { ctx="partner" }
		/^[[:space:]]+port state: / {
			if (ctx == "actor") {
				gsub(":", "", iface)
				printf "%s_%s_state=%s\n", iface, ctx, $3
			}
		}
		' /proc/net/bonding/bond0
	)

	if [ "$interface" == "eth0" ] || [ "$interface" == "both" ]; then
		compare_state "$eth0_actor_state" "$expected" eth0 "$last_attempt" || ret=1
	fi

	if [ "$interface" == "eth1" ] || [ "$interface" == "both" ]; then
		compare_state "$eth1_actor_state" "$expected" eth1 "$last_attempt" || ret=1
	fi

	return $ret
}

test_lacp_port_state() {
	local interface=$1
	local expected=$2
	local retry=$3
	local last_attempt=0
	local attempt=1
	local ret=1

	while [ $attempt -le $((retry + 1)) ]; do
		[ $attempt -eq $((retry + 1)) ] && last_attempt=1
		_test_lacp_port_state "$interface" "$expected" "$last_attempt" && return
		((attempt++))
		sleep 1
	done

	RET=1
}


trap cleanup_all_ns EXIT
setup_ns d_ns p_ns br_ns
setup_links

# Initial state
RET=0
mode=off
test_lacp_port_state both $COLLECTING_DISTRIBUTING 3
test_master_carrier up $mode
log_test "bond LACP" "lacp_strict $mode - eth0 and eth1 up"

# partner eth0 down, eth1 up
# (replicate eth0 state to dut eth0 by shutting a bridge port)
RET=0
ip -n "${p_ns}" link set eth0 down
ip -n "${br_ns}" link set eth2 down
test_lacp_port_state eth0 $FAILED 5
test_lacp_port_state eth1 $COLLECTING_DISTRIBUTING 1
test_master_carrier up $mode
log_test "bond LACP" "lacp_strict $mode - eth0 down"

# partner eth0 and eth1 down
RET=0
ip -n "${p_ns}" link set eth1 down
ip -n "${br_ns}" link set eth3 down
test_lacp_port_state both $FAILED 5
test_master_carrier down $mode # down because of min_links
log_test "bond LACP" "lacp_strict $mode - eth0 and eth1 down"

# partner eth0 up, eth1 down
RET=0
ip -n "${p_ns}" link set eth0 up
ip -n "${br_ns}" link set eth2 up
test_lacp_port_state eth0 $COLLECTING_DISTRIBUTING 60
test_lacp_port_state eth1 $FAILED 1
test_master_carrier up $mode
log_test "bond LACP" "lacp_strict $mode - eth0 up, eth1 down"

# partner eth0 and eth1 up
RET=0
ip -n "${p_ns}" link set eth1 up
ip -n "${br_ns}" link set eth3 up
test_lacp_port_state both $COLLECTING_DISTRIBUTING 60
test_master_carrier up $mode
log_test "bond LACP" "lacp_strict $mode - eth0 and eth1 up"

# partner eth0 stops LACP and eth1 up
RET=0
ip netns exec "${br_ns}" sh -c "echo 0 > /sys/class/net/br0/brif/eth0/group_fwd_mask"
ip netns exec "${br_ns}" sh -c "echo 0 > /sys/class/net/br0/brif/eth2/group_fwd_mask"
test_lacp_port_state eth0 $FAILED 5
test_lacp_port_state eth1 $COLLECTING_DISTRIBUTING 1
test_master_carrier up $mode
log_test "bond LACP" "lacp_strict $mode - eth0 stopped sending LACP"

# partner eth0 and eth1 stop LACP
RET=0
ip netns exec "${br_ns}" sh -c "echo 0 > /sys/class/net/br1/brif/eth1/group_fwd_mask"
ip netns exec "${br_ns}" sh -c "echo 0 > /sys/class/net/br1/brif/eth3/group_fwd_mask"
test_lacp_port_state both $FAILED 5
test_master_carrier up $mode
log_test "bond LACP" "lacp_strict $mode - eth0 and eth1 stopped sending LACP"

# switch to lacp_strict on
RET=0
mode=on
ip -n "${d_ns}" link set dev bond0 type bond lacp_strict $mode
test_lacp_port_state both $FAILED 1
test_master_carrier down $mode
log_test "bond LACP" "lacp_strict $mode - eth0 and eth1 stopped sending LACP"

# switch back to lacp_strict off mode
RET=0
mode=off
ip -n "${d_ns}" link set dev bond0 type bond lacp_strict $mode
test_lacp_port_state both $FAILED 1
test_master_carrier up $mode
log_test "bond LACP" "lacp_strict $mode - eth0 and eth1 stopped sending LACP"

# eth0 recovers LACP
RET=0
ip netns exec "${br_ns}" sh -c "echo 4 > /sys/class/net/br0/brif/eth0/group_fwd_mask"
ip netns exec "${br_ns}" sh -c "echo 4 > /sys/class/net/br0/brif/eth2/group_fwd_mask"
test_lacp_port_state eth0 $COLLECTING_DISTRIBUTING 60
test_lacp_port_state eth1 $FAILED 1
test_master_carrier up $mode
log_test "bond LACP" "lacp_strict $mode - eth0 recovered and eth1 stopped sending LACP"

# eth1 recovers LACP
RET=0
ip netns exec "${br_ns}" sh -c "echo 4 > /sys/class/net/br1/brif/eth1/group_fwd_mask"
ip netns exec "${br_ns}" sh -c "echo 4 > /sys/class/net/br1/brif/eth3/group_fwd_mask"
test_lacp_port_state both $COLLECTING_DISTRIBUTING 60
test_master_carrier up $mode
log_test "bond LACP" "lacp_strict $mode - eth0 and eth1 recovered LACP"

# switch to lacp_strict on
RET=0
mode=on
ip -n "${d_ns}" link set dev bond0 type bond lacp_strict $mode
test_lacp_port_state both $COLLECTING_DISTRIBUTING 1
test_master_carrier up $mode
log_test "bond LACP" "lacp_strict $mode - eth0 and eth1 up"

# partner eth0 down, eth1 up
RET=0
ip -n "${p_ns}" link set eth0 down
ip -n "${br_ns}" link set eth2 down
test_lacp_port_state eth0 $FAILED 5
test_lacp_port_state eth1 $COLLECTING_DISTRIBUTING 1
test_master_carrier up $mode
log_test "bond LACP" "lacp_strict $mode - eth0 down"

# partner eth0 and eth1 down
RET=0
ip -n "${p_ns}" link set eth1 down
ip -n "${br_ns}" link set eth3 down
test_lacp_port_state both $FAILED 5
test_master_carrier down $mode # down because of min_links
log_test "bond LACP" "lacp_strict $mode - eth0 and eth1 down"

# partner eth0 up, eth1 down
RET=0
ip -n "${p_ns}" link set eth0 up
ip -n "${br_ns}" link set eth2 up
test_lacp_port_state eth0 $COLLECTING_DISTRIBUTING 60
test_lacp_port_state eth1 $FAILED 1
test_master_carrier up $mode
log_test "bond LACP" "lacp_strict $mode - eth0 up, eth1 down"

# partner eth0 and eth1 up
RET=0
ip -n "${p_ns}" link set eth1 up
ip -n "${br_ns}" link set eth3 up
test_lacp_port_state both $COLLECTING_DISTRIBUTING 60
test_master_carrier up $mode
log_test "bond LACP" "lacp_strict $mode - eth0 and eth1 up"

# partner eth0 stops LACP and eth1 up
RET=0
ip netns exec "${br_ns}" sh -c "echo 0 > /sys/class/net/br0/brif/eth0/group_fwd_mask"
ip netns exec "${br_ns}" sh -c "echo 0 > /sys/class/net/br0/brif/eth2/group_fwd_mask"
test_lacp_port_state eth0 $FAILED 5
test_lacp_port_state eth1 $COLLECTING_DISTRIBUTING 1
test_master_carrier up $mode
log_test "bond LACP" "lacp_strict $mode - eth0 stopped sending LACP"

# partner eth0 and eth1 stop LACP
RET=0
ip netns exec "${br_ns}" sh -c "echo 0 > /sys/class/net/br1/brif/eth1/group_fwd_mask"
ip netns exec "${br_ns}" sh -c "echo 0 > /sys/class/net/br1/brif/eth3/group_fwd_mask"
test_lacp_port_state both $FAILED 5
test_master_carrier down $mode
log_test "bond LACP" "lacp_strict $mode - eth0 and eth1 stopped sending LACP"

# eth0 recovers LACP
RET=0
ip netns exec "${br_ns}" sh -c "echo 4 > /sys/class/net/br0/brif/eth0/group_fwd_mask"
ip netns exec "${br_ns}" sh -c "echo 4 > /sys/class/net/br0/brif/eth2/group_fwd_mask"
test_lacp_port_state eth0 $COLLECTING_DISTRIBUTING 60
test_lacp_port_state eth1 $FAILED 1
test_master_carrier up $mode
log_test "bond LACP" "lacp_strict $mode - eth0 recovered and eth1 stopped sending LACP"

# eth1 recovers LACP
# shellcheck disable=SC2034
RET=0
ip netns exec "${br_ns}" sh -c "echo 4 > /sys/class/net/br1/brif/eth1/group_fwd_mask"
ip netns exec "${br_ns}" sh -c "echo 4 > /sys/class/net/br1/brif/eth3/group_fwd_mask"
test_lacp_port_state both $COLLECTING_DISTRIBUTING 60
test_master_carrier up $mode
log_test "bond LACP" "lacp_strict $mode - eth0 and eth1 recovered LACP"

exit "${EXIT_STATUS}"
