#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source ppp_common.sh

# Temporary files for PTY symlinks
TTY_DIR=$(mktemp -d /tmp/ppp.XXXXXX)
TTY_SERVER="$TTY_DIR"/server
TTY_CLIENT="$TTY_DIR"/client

# shellcheck disable=SC2329
cleanup() {
	cleanup_all_ns
	[ -n "$SOCAT_PID" ] && kill_process "$SOCAT_PID"
	rm -fr "$TTY_DIR"
}

trap cleanup EXIT

ppp_common_init
modprobe -q ppp_async

# Create the virtual serial device
socat -d PTY,link="$TTY_SERVER",rawer PTY,link="$TTY_CLIENT",rawer &
SOCAT_PID=$!

# Wait for symlinks to be created
slowwait 5 [ -L "$TTY_SERVER" ]

# Start the PPP Server
ip netns exec "$NS_SERVER" pppd "$TTY_SERVER" 115200 \
	"$IP_SERVER":"$IP_CLIENT" \
	local noauth nodefaultroute debug

# Start the PPP Client
ip netns exec "$NS_CLIENT" pppd "$TTY_CLIENT" 115200 \
	local noauth updetach nodefaultroute debug

ppp_test_connectivity

log_test "PPP async"

exit "$EXIT_STATUS"
