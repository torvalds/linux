#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

# Regression Test:
#   Verify LACPDUs get transmitted after setting the MAC address of
#   the bond.
#
# https://bugzilla.redhat.com/show_bug.cgi?id=2020773
#
#       +---------+
#       | fab-br0 |
#       +---------+
#            |
#       +---------+
#       |  fbond  |
#       +---------+
#        |       |
#    +------+ +------+
#    |veth1 | |veth2 |
#    +------+ +------+
#
# We use veths instead of physical interfaces
REQUIRE_MZ=no
NUM_NETIFS=0
lib_dir=$(dirname "$0")
source "$lib_dir"/../../../net/forwarding/lib.sh

set -e
cleanup() {
	ip link del fab-br0 >/dev/null 2>&1 || :
	ip link del fbond  >/dev/null 2>&1 || :
	ip link del veth1-bond  >/dev/null 2>&1 || :
	ip link del veth2-bond  >/dev/null 2>&1 || :
}

trap cleanup 0 1 2
cleanup

# create the bridge
ip link add fab-br0 address 52:54:00:3B:7C:A6 mtu 1500 type bridge \
	forward_delay 15

# create the bond
ip link add fbond type bond mode 4 miimon 200 xmit_hash_policy 1 \
	ad_actor_sys_prio 65535 lacp_rate fast

# set bond address
ip link set fbond address 52:54:00:3B:7C:A6
ip link set fbond up

# set again bond sysfs parameters
ip link set fbond type bond ad_actor_sys_prio 65535

# create veths
ip link add name veth1-bond type veth peer name veth1-end
ip link add name veth2-bond type veth peer name veth2-end

# add ports
ip link set fbond master fab-br0
ip link set veth1-bond master fbond
ip link set veth2-bond master fbond

# bring up
ip link set veth1-end up
ip link set veth2-end up
ip link set fab-br0 up
ip link set fbond up
ip addr add dev fab-br0 10.0.0.3

rc=0
tc qdisc add dev veth1-end clsact
tc filter add dev veth1-end ingress protocol 0x8809 pref 1 handle 101 flower skip_hw action pass
if slowwait_for_counter 15 2 \
	tc_rule_handle_stats_get "dev veth1-end ingress" 101 ".packets" "" &> /dev/null; then
	echo "PASS, captured 2"
else
	echo "FAIL"
	rc=1
fi
exit $rc
