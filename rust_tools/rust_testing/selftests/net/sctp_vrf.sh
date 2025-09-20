#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Testing For SCTP VRF.
# TOPO: CLIENT_NS1 (veth1) <---> (veth1) -> vrf_s1
#                                                  SERVER_NS
#       CLIENT_NS2 (veth1) <---> (veth2) -> vrf_s2

source lib.sh
CLIENT_IP4="10.0.0.1"
CLIENT_IP6="2000::1"
CLIENT_PORT=1234

SERVER_IP4="10.0.0.2"
SERVER_IP6="2000::2"
SERVER_PORT=1234

setup() {
	modprobe sctp
	modprobe sctp_diag
	setup_ns CLIENT_NS1 CLIENT_NS2 SERVER_NS

	ip net exec $CLIENT_NS1 sysctl -w net.ipv6.conf.default.accept_dad=0 2>&1 >/dev/null
	ip net exec $CLIENT_NS2 sysctl -w net.ipv6.conf.default.accept_dad=0 2>&1 >/dev/null
	ip net exec $SERVER_NS sysctl -w net.ipv6.conf.default.accept_dad=0 2>&1 >/dev/null

	ip -n $SERVER_NS link add veth1 type veth peer name veth1 netns $CLIENT_NS1
	ip -n $SERVER_NS link add veth2 type veth peer name veth1 netns $CLIENT_NS2

	ip -n $CLIENT_NS1 link set veth1 up
	ip -n $CLIENT_NS1 addr add $CLIENT_IP4/24 dev veth1
	ip -n $CLIENT_NS1 addr add $CLIENT_IP6/24 dev veth1

	ip -n $CLIENT_NS2 link set veth1 up
	ip -n $CLIENT_NS2 addr add $CLIENT_IP4/24 dev veth1
	ip -n $CLIENT_NS2 addr add $CLIENT_IP6/24 dev veth1

	ip -n $SERVER_NS link add dummy1 type dummy
	ip -n $SERVER_NS link set dummy1 up
	ip -n $SERVER_NS link add vrf-1 type vrf table 10
	ip -n $SERVER_NS link add vrf-2 type vrf table 20
	ip -n $SERVER_NS link set vrf-1 up
	ip -n $SERVER_NS link set vrf-2 up
	ip -n $SERVER_NS link set veth1 master vrf-1
	ip -n $SERVER_NS link set veth2 master vrf-2

	ip -n $SERVER_NS addr add $SERVER_IP4/24 dev dummy1
	ip -n $SERVER_NS addr add $SERVER_IP4/24 dev veth1
	ip -n $SERVER_NS addr add $SERVER_IP4/24 dev veth2
	ip -n $SERVER_NS addr add $SERVER_IP6/24 dev dummy1
	ip -n $SERVER_NS addr add $SERVER_IP6/24 dev veth1
	ip -n $SERVER_NS addr add $SERVER_IP6/24 dev veth2

	ip -n $SERVER_NS link set veth1 up
	ip -n $SERVER_NS link set veth2 up
	ip -n $SERVER_NS route add table 10 $CLIENT_IP4 dev veth1 src $SERVER_IP4
	ip -n $SERVER_NS route add table 20 $CLIENT_IP4 dev veth2 src $SERVER_IP4
	ip -n $SERVER_NS route add $CLIENT_IP4 dev veth1 src $SERVER_IP4
	ip -n $SERVER_NS route add table 10 $CLIENT_IP6 dev veth1 src $SERVER_IP6
	ip -n $SERVER_NS route add table 20 $CLIENT_IP6 dev veth2 src $SERVER_IP6
	ip -n $SERVER_NS route add $CLIENT_IP6 dev veth1 src $SERVER_IP6
}

cleanup() {
	ip netns exec $SERVER_NS pkill sctp_hello 2>&1 >/dev/null
	cleanup_ns $CLIENT_NS1 $CLIENT_NS2 $SERVER_NS
}

wait_server() {
	local IFACE=$1
	local CNT=0

	until ip netns exec $SERVER_NS ss -lS src $SERVER_IP:$SERVER_PORT | \
		grep LISTEN | grep "$IFACE" 2>&1 >/dev/null; do
		[ $((CNT++)) = "20" ] && { RET=3; return $RET; }
		sleep 0.1
	done
}

do_test() {
	local CLIENT_NS=$1
	local IFACE=$2

	ip netns exec $SERVER_NS pkill sctp_hello 2>&1 >/dev/null
	ip netns exec $SERVER_NS ./sctp_hello server $AF $SERVER_IP \
		$SERVER_PORT $IFACE 2>&1 >/dev/null &
	disown
	wait_server $IFACE || return $RET
	timeout 3 ip netns exec $CLIENT_NS ./sctp_hello client $AF \
		$SERVER_IP $SERVER_PORT $CLIENT_IP $CLIENT_PORT 2>&1 >/dev/null
	RET=$?
	return $RET
}

do_testx() {
	local IFACE1=$1
	local IFACE2=$2

	ip netns exec $SERVER_NS pkill sctp_hello 2>&1 >/dev/null
	ip netns exec $SERVER_NS ./sctp_hello server $AF $SERVER_IP \
		$SERVER_PORT $IFACE1 2>&1 >/dev/null &
	disown
	wait_server $IFACE1 || return $RET
	ip netns exec $SERVER_NS ./sctp_hello server $AF $SERVER_IP \
		$SERVER_PORT $IFACE2 2>&1 >/dev/null &
	disown
	wait_server $IFACE2 || return $RET
	timeout 3 ip netns exec $CLIENT_NS1 ./sctp_hello client $AF \
		$SERVER_IP $SERVER_PORT $CLIENT_IP $CLIENT_PORT 2>&1 >/dev/null && \
	timeout 3 ip netns exec $CLIENT_NS2 ./sctp_hello client $AF \
		$SERVER_IP $SERVER_PORT $CLIENT_IP $CLIENT_PORT 2>&1 >/dev/null
	RET=$?
	return $RET
}

testup() {
	ip netns exec $SERVER_NS sysctl -w net.sctp.l3mdev_accept=1 2>&1 >/dev/null
	echo -n "TEST 01: nobind, connect from client 1, l3mdev_accept=1, Y "
	do_test $CLIENT_NS1 || { echo "[FAIL]"; return $RET; }
	echo "[PASS]"

	echo -n "TEST 02: nobind, connect from client 2, l3mdev_accept=1, N "
	do_test $CLIENT_NS2 && { echo "[FAIL]"; return $RET; }
	echo "[PASS]"

	ip netns exec $SERVER_NS sysctl -w net.sctp.l3mdev_accept=0 2>&1 >/dev/null
	echo -n "TEST 03: nobind, connect from client 1, l3mdev_accept=0, N "
	do_test $CLIENT_NS1 && { echo "[FAIL]"; return $RET; }
	echo "[PASS]"

	echo -n "TEST 04: nobind, connect from client 2, l3mdev_accept=0, N "
	do_test $CLIENT_NS2 && { echo "[FAIL]"; return $RET; }
	echo "[PASS]"

	echo -n "TEST 05: bind veth2 in server, connect from client 1, N "
	do_test $CLIENT_NS1 veth2 && { echo "[FAIL]"; return $RET; }
	echo "[PASS]"

	echo -n "TEST 06: bind veth1 in server, connect from client 1, Y "
	do_test $CLIENT_NS1 veth1 || { echo "[FAIL]"; return $RET; }
	echo "[PASS]"

	echo -n "TEST 07: bind vrf-1 in server, connect from client 1, Y "
	do_test $CLIENT_NS1 vrf-1 || { echo "[FAIL]"; return $RET; }
	echo "[PASS]"

	echo -n "TEST 08: bind vrf-2 in server, connect from client 1, N "
	do_test $CLIENT_NS1 vrf-2 && { echo "[FAIL]"; return $RET; }
	echo "[PASS]"

	echo -n "TEST 09: bind vrf-2 in server, connect from client 2, Y "
	do_test $CLIENT_NS2 vrf-2 || { echo "[FAIL]"; return $RET; }
	echo "[PASS]"

	echo -n "TEST 10: bind vrf-1 in server, connect from client 2, N "
	do_test $CLIENT_NS2 vrf-1 && { echo "[FAIL]"; return $RET; }
	echo "[PASS]"

	echo -n "TEST 11: bind vrf-1 & 2 in server, connect from client 1 & 2, Y "
	do_testx vrf-1 vrf-2 || { echo "[FAIL]"; return $RET; }
	echo "[PASS]"

	echo -n "TEST 12: bind vrf-2 & 1 in server, connect from client 1 & 2, N "
	do_testx vrf-2 vrf-1 || { echo "[FAIL]"; return $RET; }
	echo "[PASS]"
}

trap cleanup EXIT
setup && echo "Testing For SCTP VRF:" && \
CLIENT_IP=$CLIENT_IP4 SERVER_IP=$SERVER_IP4 AF="-4" testup && echo "***v4 Tests Done***" &&
CLIENT_IP=$CLIENT_IP6 SERVER_IP=$SERVER_IP6 AF="-6" testup && echo "***v6 Tests Done***"
exit $?
