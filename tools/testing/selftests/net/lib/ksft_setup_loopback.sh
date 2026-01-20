#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Setup script for running ksft tests over a real interface in loopback mode.
# This scripts replaces the historical setup_loopback.sh. It puts
# a (presumably) real hardware interface into loopback mode, creates macvlan
# interfaces on top and places them in a network namespace for isolation.
#
# NETIF env variable must be exported to indicate the real target device.
# Note that the test will override NETIF with one of the macvlans, the
# actual ksft test will only see the macvlans.
#
# Example use:
#   export NETIF=eth0
#   ./net/lib/ksft_setup_loopback.sh ./drivers/net/gro.py

if [ -z "$NETIF" ]; then
    echo "Error: NETIF variable not set"
    exit 1
fi
if ! [ -d "/sys/class/net/$NETIF" ]; then
    echo "Error: Can't find $NETIF, invalid netdevice"
    exit 1
fi

# Save original settings for cleanup
readonly FLUSH_PATH="/sys/class/net/${NETIF}/gro_flush_timeout"
readonly IRQ_PATH="/sys/class/net/${NETIF}/napi_defer_hard_irqs"
FLUSH_TIMEOUT="$(< "${FLUSH_PATH}")"
readonly FLUSH_TIMEOUT
HARD_IRQS="$(< "${IRQ_PATH}")"
readonly HARD_IRQS

SERVER_NS=$(mktemp -u server-XXXXXXXX)
readonly SERVER_NS
CLIENT_NS=$(mktemp -u client-XXXXXXXX)
readonly CLIENT_NS
readonly SERVER_MAC="aa:00:00:00:00:02"
readonly CLIENT_MAC="aa:00:00:00:00:01"

# ksft expects addresses to communicate with remote
export  LOCAL_V6=2001:db8:1::1
export REMOTE_V6=2001:db8:1::2

cleanup() {
    local exit_code=$?

    echo "Cleaning up..."

    # Remove macvlan interfaces and namespaces
    ip -netns "${SERVER_NS}" link del dev server 2>/dev/null || true
    ip netns del "${SERVER_NS}" 2>/dev/null || true
    ip -netns "${CLIENT_NS}" link del dev client 2>/dev/null || true
    ip netns del "${CLIENT_NS}" 2>/dev/null || true

    # Disable loopback
    ethtool -K "${NETIF}" loopback off 2>/dev/null || true
    sleep 1

    echo "${FLUSH_TIMEOUT}" >"${FLUSH_PATH}"
    echo "${HARD_IRQS}" >"${IRQ_PATH}"

    exit $exit_code
}

trap cleanup EXIT INT TERM

# Enable loopback mode
echo "Enabling loopback on ${NETIF}..."
ethtool -K "${NETIF}" loopback on || {
    echo "Failed to enable loopback mode"
    exit 1
}
# The interface may need time to get carrier back, but selftests
# will wait for carrier, so no need to wait / sleep here.

# Use timer on  host to trigger the network stack
# Also disable device interrupt to not depend on NIC interrupt
# Reduce test flakiness caused by unexpected interrupts
echo 100000 >"${FLUSH_PATH}"
echo 50 >"${IRQ_PATH}"

# Create server namespace with macvlan
ip netns add "${SERVER_NS}"
ip link add link "${NETIF}" dev server address "${SERVER_MAC}" type macvlan
ip link set dev server netns "${SERVER_NS}"
ip -netns "${SERVER_NS}" link set dev server up
ip -netns "${SERVER_NS}" addr add $LOCAL_V6/64 dev server
ip -netns "${SERVER_NS}" link set dev lo up

# Create client namespace with macvlan
ip netns add "${CLIENT_NS}"
ip link add link "${NETIF}" dev client address "${CLIENT_MAC}" type macvlan
ip link set dev client netns "${CLIENT_NS}"
ip -netns "${CLIENT_NS}" link set dev client up
ip -netns "${CLIENT_NS}" addr add $REMOTE_V6/64 dev client
ip -netns "${CLIENT_NS}" link set dev lo up

echo "Setup complete!"
echo "  Device: ${NETIF}"
echo "  Server NS: ${SERVER_NS}"
echo "  Client NS: ${CLIENT_NS}"
echo ""

# Setup environment variables for tests
export NETIF=server
export REMOTE_TYPE=netns
export REMOTE_ARGS="${CLIENT_NS}"

# Run the command
ip netns exec "${SERVER_NS}" "$@"
