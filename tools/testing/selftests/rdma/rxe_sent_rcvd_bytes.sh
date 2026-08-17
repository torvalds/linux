#!/bin/bash

# Configuration
PORT=4791
MODS=("tun" "rdma_rxe")

exec > /dev/null

# --- Helper: Cleanup Routine ---
cleanup() {
    echo "Cleaning up resources..."
    rdma link del rxe0 2>/dev/null
    ip link del tun0 2>/dev/null
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

orig_s=`cat /sys/class/infiniband/rxe0/ports/1/counters/port_xmit_data`
orig_r=`cat /sys/class/infiniband/rxe0/ports/1/counters/port_rcv_data`

rping -s -a 1.1.1.1 -C 3 -v &
sleep 1
rping -c -a 1.1.1.1 -C 3 -d -v

new_s=`cat /sys/class/infiniband/rxe0/ports/1/counters/port_xmit_data`
new_r=`cat /sys/class/infiniband/rxe0/ports/1/counters/port_rcv_data`

echo sent $new_s $orig_s
echo rcvd $new_r $orig_r

result0=$((new_s - orig_s))
result1=$((new_r - orig_r))

if [ $result0 != $result1 ]; then
       echo "Error: sent and rcvd bytes different"
       echo $result0
       echo $result1
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
