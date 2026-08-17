#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source ppp_common.sh

VETH_SERVER="veth-server"
VETH_CLIENT="veth-client"
OUTER_IP_SERVER="172.16.1.1"
OUTER_IP_CLIENT="172.16.1.2"

PPPOL2TP_DIR=$(mktemp -d /tmp/pppol2tp.XXXXXX)
PPPOL2TP_LOG="$PPPOL2TP_DIR/l2tp.log"

# shellcheck disable=SC2329
cleanup() {
	cleanup_all_ns
	[ -n "$SOCAT_PID" ] && kill_process "$SOCAT_PID"
	rm -rf "$PPPOL2TP_DIR"
}

trap cleanup EXIT

require_command xl2tpd
ppp_common_init
modprobe -q l2tp_ppp

# Create the veth pair
ip link add "$VETH_SERVER" type veth peer name "$VETH_CLIENT"
ip link set "$VETH_SERVER" netns "$NS_SERVER"
ip link set "$VETH_CLIENT" netns "$NS_CLIENT"
ip -netns "$NS_SERVER" link set "$VETH_SERVER" up
ip -netns "$NS_CLIENT" link set "$VETH_CLIENT" up
ip -netns "$NS_SERVER" address add dev "$VETH_SERVER" "$OUTER_IP_SERVER" peer "$OUTER_IP_CLIENT"
ip -netns "$NS_CLIENT" address add dev "$VETH_CLIENT" "$OUTER_IP_CLIENT" peer "$OUTER_IP_SERVER"

# Start socat as syslog listener
socat -v -u UNIX-RECV:/dev/log OPEN:/dev/null > "$PPPOL2TP_LOG" 2>&1 &
SOCAT_PID=$!

# Generate configuration files
cat > "$PPPOL2TP_DIR/l2tp-server.conf" <<EOF
[global]
listen-addr = $OUTER_IP_SERVER
access control = no

[lns default]
ip range = $IP_CLIENT
local ip = $IP_SERVER
require authentication = no
require chap = no
require pap = no
ppp debug = yes
pppoptfile = $(pwd)/pppoe-server-options
EOF

cat > "$PPPOL2TP_DIR/l2tp-client.conf" <<EOF
[global]
listen-addr = $OUTER_IP_CLIENT
access control = no

[lac server]
lns = $OUTER_IP_SERVER
require authentication = no
require chap = no
require pap = no
ppp debug = yes
pppoptfile = $(pwd)/pppoe-server-options
EOF

# Start the L2TP Server
ip netns exec "$NS_SERVER" xl2tpd -D -c "$PPPOL2TP_DIR/l2tp-server.conf" \
	-p "$PPPOL2TP_DIR/l2tp-server.pid" -C "$PPPOL2TP_DIR/l2tp-server.control" &

# Start the L2TP Client
ip netns exec "$NS_CLIENT" xl2tpd -D -c "$PPPOL2TP_DIR/l2tp-client.conf" \
	-p "$PPPOL2TP_DIR/l2tp-client.pid" -C "$PPPOL2TP_DIR/l2tp-client.control" &

# Wait for xl2tpd to start and open their control pipes
slowwait 2 [ -p "$PPPOL2TP_DIR/l2tp-server.control" ]
slowwait 2 [ -p "$PPPOL2TP_DIR/l2tp-client.control" ]

# Connect LAC to LNS
echo "c server" > "$PPPOL2TP_DIR/l2tp-client.control"

ppp_test_connectivity

log_test "PPPoL2TP"

# Recursion test
RET=0
# Delete route to LNS IP
ip -netns "$NS_CLIENT" route del "$OUTER_IP_SERVER"
# Add default route through ppp0
ip -netns "$NS_CLIENT" route add default dev ppp0
# ping (we expect the ping to fail but not deadlock the system)
ip netns exec "$NS_CLIENT" ping -c 1 "$IP_SERVER" -w 1
check_fail $?

log_test "PPPoL2TP Recursion"

# Dump syslog messages if the test failed
if [ "$EXIT_STATUS" -ne 0 ]; then
	while read -r _sign _date _time len _from _to
	do      len=${len##*=}
		read -n "$len" -r LINE
		echo "$LINE"
	done < "$PPPOL2TP_LOG"
fi

exit "$EXIT_STATUS"
