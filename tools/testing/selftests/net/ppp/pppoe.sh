#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source ppp_common.sh

VETH_SERVER="veth-server"
VETH_CLIENT="veth-client"
PPPOE_LOG=$(mktemp /tmp/pppoe.XXXXXX)

# shellcheck disable=SC2329
cleanup() {
	cleanup_all_ns
	[ -n "$SOCAT_PID" ] && kill_process "$SOCAT_PID"
	rm -f "$PPPOE_LOG"
}

trap cleanup EXIT

require_command pppoe-server
ppp_common_init
modprobe -q pppoe

# Try to locate pppoe.so plugin
PPPOE_PLUGIN=$(find /usr/{lib,lib64,lib32}/pppd/ -name pppoe.so -type f -print -quit)
if [ -z "$PPPOE_PLUGIN" ]; then
	log_test_skip "PPPoE: pppoe.so plugin not found"
	exit "$EXIT_STATUS"
fi

# Create the veth pair
ip link add "$VETH_SERVER" type veth peer name "$VETH_CLIENT"
ip link set "$VETH_SERVER" netns "$NS_SERVER"
ip link set "$VETH_CLIENT" netns "$NS_CLIENT"
ip -netns "$NS_SERVER" link set "$VETH_SERVER" up
ip -netns "$NS_CLIENT" link set "$VETH_CLIENT" up

# Start socat as syslog listener
socat -v -u UNIX-RECV:/dev/log OPEN:/dev/null > "$PPPOE_LOG" 2>&1 &
SOCAT_PID=$!

# Start the PPP Server. Note that versions before 4.0 ignore -g option and
# instead use a hardcoded plugin path, so they may fail to find the plugin.
ip netns exec "$NS_SERVER" pppoe-server -I "$VETH_SERVER" \
	-L "$IP_SERVER" -R "$IP_CLIENT" -N 1 -q "$(command -v pppd)" \
	-k -O "$(pwd)/pppoe-server-options" -g "$PPPOE_PLUGIN"

# Start the PPP Client
ip netns exec "$NS_CLIENT" pppd \
	local debug updetach noipdefault noauth nodefaultroute \
	plugin "$PPPOE_PLUGIN" nic-"$VETH_CLIENT"

ppp_test_connectivity

log_test "PPPoE"

# Dump syslog messages if the test failed
if [ "$RET" -ne 0 ]; then
	while read -r _sign _date _time len _from _to
	do      len=${len##*=}
		read -n "$len" -r LINE
		echo "$LINE"
	done < "$PPPOE_LOG"
fi

exit "$EXIT_STATUS"
