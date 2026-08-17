#!/bin/bash

# Configuration
NS="test1"
VETH_A="veth-a"
VETH_B="veth-b"
IP_A="1.1.1.1"
IP_B="1.1.1.2"
PORT=4791

source "$(dirname "$0")/../kselftest/ktap_helpers.sh"

exec > /dev/null

# --- Cleanup Routine ---
cleanup() {
    echo "Cleaning up resources..."
    rdma link del rxe1 2>/dev/null
    ip netns exec "$NS" rdma link del rxe0 2>/dev/null
    ip link delete "$VETH_B" 2>/dev/null
    ip netns del "$NS" 2>/dev/null
    modprobe -r rdma_rxe 2>/dev/null
}
trap cleanup EXIT

# --- Prerequisite Checks ---
if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root"
   exit 1
fi

if ! modinfo rdma_rxe >/dev/null 2>&1; then
    echo "SKIP: Kernel module 'rdma_rxe' not found." >&2
    exit $KSFT_SKIP
fi

modprobe rdma_rxe || { echo "Failed to load rdma_rxe"; exit 1; }

# --- Setup Network Topology ---
echo "Setting up network namespace and veth pair..."
ip netns add "$NS"
ip link add "$VETH_A" type veth peer name "$VETH_B"
ip link set "$VETH_A" netns "$NS"

# Configure Namespace side (test1)
ip netns exec "$NS" ip addr add "$IP_A/24" dev "$VETH_A"
ip netns exec "$NS" ip link set "$VETH_A" up
ip netns exec "$NS" ip link set lo up

# Configure Host side
ip addr add "$IP_B/24" dev "$VETH_B"
ip link set "$VETH_B" up

# --- RXE Link Creation ---
echo "Creating RDMA links..."
ip netns exec "$NS" rdma link add rxe0 type rxe netdev "$VETH_A"
rdma link add rxe1 type rxe netdev "$VETH_B"

# Verify UDP 4791 is listening
check_port() {
    local target=$1 # "host" or "ns"
    if [ "$target" == "ns" ]; then
        ip netns exec "$NS" ss -Huln sport == :$PORT | grep -q ":$PORT"
    else
        ss -Huln sport == :$PORT | grep -q ":$PORT"
    fi
}

check_port "ns" || { echo "Error: RXE port not listening in namespace"; exit 1; }
check_port "host" || { echo "Error: RXE port not listening on host"; exit 1; }

# --- Connectivity Test ---
echo "Testing connectivity with rping..."
ping -c 2 -W 1 "$IP_A" > /dev/null || { echo "Ping failed"; exit 1; }

# Start rping server in background
ip netns exec "$NS" rping -s -a "$IP_A" -v > /dev/null 2>&1 &
RPING_PID=$!
sleep 1 # Allow server to bind

# Run rping client
rping -c -a "$IP_A" -d -v -C 3
RESULT=$?

kill $RPING_PID 2>/dev/null

if [ $RESULT -eq 0 ]; then
    echo "SUCCESS: RDMA traffic verified."
else
    echo "FAILURE: rping failed."
    exit 1
fi
