#!/bin/bash

# Configuration
DEV_NAME="tun0"
RXE_NAME="rxe0"
RDMA_PORT=4791

source "$(dirname "$0")/../kselftest/ktap_helpers.sh"

exec > /dev/null

# --- Cleanup Routine ---
# Ensures environment is clean even if the script hits an error
cleanup() {
    echo "Performing cleanup..."
    rdma link del $RXE_NAME 2>/dev/null
    ip link del $DEV_NAME 2>/dev/null
    modprobe -r rdma_rxe 2>/dev/null
}
trap cleanup EXIT

# 1. Dependency Check
if ! modinfo rdma_rxe >/dev/null 2>&1; then
    echo "SKIP: rdma_rxe module not found." >&2
    exit $KSFT_SKIP
fi

modprobe rdma_rxe

# 2. Setup TUN Device
echo "Creating $DEV_NAME..."
ip tuntap add mode tun "$DEV_NAME"
ip addr add 1.1.1.1/24 dev "$DEV_NAME"
ip link set "$DEV_NAME" up

# 3. Attach RXE Link
echo "Attaching RXE link $RXE_NAME to $DEV_NAME..."
rdma link add "$RXE_NAME" type rxe netdev "$DEV_NAME"

# 4. Verification: Port Check
# Use -H (no header) and -q (quiet) for cleaner scripting logic
if ! ss -Huln sport == :$RDMA_PORT | grep -q ":$RDMA_PORT"; then
    echo "Error: UDP port $RDMA_PORT is not listening."
    exit 1
fi
echo "Verified: RXE is listening on UDP $RDMA_PORT."

# 5. Trigger NETDEV_UNREGISTER
# We delete the underlying device without deleting the RDMA link first.
echo "Triggering NETDEV_UNREGISTER by deleting $DEV_NAME..."
ip link del "$DEV_NAME"

# 6. Final Verification
# The RXE link and the UDP port should be automatically cleaned up by the kernel.
if rdma link show "$RXE_NAME" 2>/dev/null; then
    echo "Error: $RXE_NAME still exists after netdev removal."
    exit 1
fi

if ss -Huln sport == :$RDMA_PORT | grep -q ":$RDMA_PORT"; then
    echo "Error: UDP port $RDMA_PORT still listening after netdev removal."
    exit 1
fi

echo "Success: NETDEV_UNREGISTER handled correctly."
