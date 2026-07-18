#!/bin/bash

# Configuration
PORT=4791
MODS=("tun" "rdma_rxe")

source "$(dirname "$0")/../kselftest/ktap_helpers.sh"

exec > /dev/null

# --- Helper: Cleanup Routine ---
cleanup() {
    echo "Cleaning up resources..."
    rdma link del rxe1 2>/dev/null
    rdma link del rxe0 2>/dev/null
    ip link del tun0 2>/dev/null
    ip link del tun1 2>/dev/null
    for m in "${MODS[@]}"; do modprobe -r "$m" 2>/dev/null; done
}

# Ensure cleanup runs on script exit or interrupt
trap cleanup EXIT

# --- Phase 1: Environment Check ---
if [[ $EUID -ne 0 ]]; then
   echo "Error: This script must be run as root."
   exit 1
fi

for m in "${MODS[@]}"; do
    if ! modinfo "$m" >/dev/null 2>&1; then
        echo "SKIP: Kernel module '$m' not found." >&2
        exit $KSFT_SKIP
    fi
    modprobe "$m" || { echo "Error: Failed to load $m"; exit 1; }
done

# --- Phase 2: Create Interfaces & RXE Links ---
echo "Creating tun0 (1.1.1.1) and rxe0..."
ip tuntap add mode tun tun0
ip addr add 1.1.1.1/24 dev tun0
ip link set tun0 up
rdma link add rxe0 type rxe netdev tun0

# Verify port 4791 is listening
if ! ss -Huln sport = :$PORT | grep -q ":$PORT"; then
    echo "Error: UDP port $PORT not found after rxe0 creation"
    exit 1
fi

echo "Creating tun1 (2.2.2.2) and rxe1..."
ip tuntap add mode tun tun1
ip addr add 2.2.2.2/24 dev tun1
ip link set tun1 up
rdma link add rxe1 type rxe netdev tun1

# Verify port 4791 is still listening
if ! ss -Huln sport = :$PORT | grep -q ":$PORT"; then
    echo "Error: UDP port $PORT missing after rxe1 creation"
    exit 1
fi

# --- Phase 3: Targeted Deletion ---
echo "Deleting rxe1..."
rdma link del rxe1

# Port should still be active because rxe0 is still alive
if ! ss -Huln sport = :$PORT | grep -q ":$PORT"; then
    echo "Error: UDP port $PORT closed prematurely"
    exit 1
fi

echo "Deleting rxe0..."
rdma link del rxe0

# Port should now be gone
if ss -Huln sport = :$PORT | grep -q ":$PORT"; then
    echo "Error: UDP port $PORT still exists after all links deleted"
    exit 1
fi

echo "Test passed successfully."
