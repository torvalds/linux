#!/bin/bash

# Configuration
NS_NAME="net6"
VETH_HOST="veth0"
VETH_NS="veth1"
RXE_NAME="rxe6"
PORT=4791
IP6_ADDR="2001:db8::1/64"

source "$(dirname "$0")/../kselftest/ktap_helpers.sh"

exec > /dev/null

# Cleanup function to run on exit (even on failure)
cleanup() {
    ip netns del "$NS_NAME" 2>/dev/null
    modprobe -r rdma_rxe 2>/dev/null
    echo "Done."
}
trap cleanup EXIT

# 1. Prerequisites check
for mod in tun veth rdma_rxe; do
    if ! modinfo "$mod" >/dev/null 2>&1; then
        echo "SKIP: Kernel module '$mod' not found." >&2
        exit $KSFT_SKIP
    fi
done

modprobe rdma_rxe

# 2. Setup Namespace and Networking
echo "Setting up IPv6 network namespace..."
ip netns add "$NS_NAME"
ip link add "$VETH_HOST" type veth peer name "$VETH_NS"
ip link set "$VETH_NS" netns "$NS_NAME"
ip netns exec "$NS_NAME" ip addr add "$IP6_ADDR" dev "$VETH_NS"
ip netns exec "$NS_NAME" ip link set "$VETH_NS" up
ip link set "$VETH_HOST" up

# 3. Add RDMA Link
echo "Adding RDMA RXE link..."
if ! ip netns exec "$NS_NAME" rdma link add "$RXE_NAME" type rxe netdev "$VETH_NS"; then
    echo "Error: Failed to create RXE link."
    exit 1
fi

# 4. Verification: Port should be listening
# Using -H to skip headers and -q for quiet exit codes
if ! ip netns exec "$NS_NAME" ss -Hul6n sport = :$PORT | grep -q ":$PORT"; then
    echo "Error: UDP port $PORT is NOT listening after link creation."
    exit 1
fi
echo "Verified: Port $PORT is active."

# 5. Removal and Verification
echo "Deleting RDMA link..."
ip netns exec "$NS_NAME" rdma link del "$RXE_NAME"

if ip netns exec "$NS_NAME" ss -Hul6n sport = :$PORT | grep -q ":$PORT"; then
    echo "Error: UDP port $PORT still active after link deletion."
    exit 1
fi
echo "Verified: Port $PORT closed successfully."
