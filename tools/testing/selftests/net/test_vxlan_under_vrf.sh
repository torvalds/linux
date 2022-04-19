#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test is for checking VXLAN underlay in a non-default VRF.
#
# It simulates two hypervisors running a VM each using four network namespaces:
# two for the HVs, two for the VMs.
# A small VXLAN tunnel is made between the two hypervisors to have the two vms
# in the same virtual L2:
#
# +-------------------+                                    +-------------------+
# |                   |                                    |                   |
# |    vm-1 netns     |                                    |    vm-2 netns     |
# |                   |                                    |                   |
# |  +-------------+  |                                    |  +-------------+  |
# |  |   veth-hv   |  |                                    |  |   veth-hv   |  |
# |  | 10.0.0.1/24 |  |                                    |  | 10.0.0.2/24 |  |
# |  +-------------+  |                                    |  +-------------+  |
# |        .          |                                    |         .         |
# +-------------------+                                    +-------------------+
#          .                                                         .
#          .                                                         .
#          .                                                         .
# +-----------------------------------+   +------------------------------------+
# |        .                          |   |                          .         |
# |  +----------+                     |   |                     +----------+   |
# |  | veth-tap |                     |   |                     | veth-tap |   |
# |  +----+-----+                     |   |                     +----+-----+   |
# |       |                           |   |                          |         |
# |    +--+--+      +--------------+  |   |  +--------------+     +--+--+      |
# |    | br0 |      | vrf-underlay |  |   |  | vrf-underlay |     | br0 |      |
# |    +--+--+      +-------+------+  |   |  +------+-------+     +--+--+      |
# |       |                 |         |   |         |                |         |
# |   +---+----+    +-------+-------+ |   | +-------+-------+    +---+----+    |
# |   | vxlan0 |....|     veth0     |.|...|.|     veth0     |....| vxlan0 |    |
# |   +--------+    | 172.16.0.1/24 | |   | | 172.16.0.2/24 |    +--------+    |
# |                 +---------------+ |   | +---------------+                  |
# |                                   |   |                                    |
# |             hv-1 netns            |   |           hv-2 netns               |
# |                                   |   |                                    |
# +-----------------------------------+   +------------------------------------+
#
# This tests both the connectivity between vm-1 and vm-2, and that the underlay
# can be moved in and out of the vrf by unsetting and setting veth0's master.

set -e

cleanup() {
    ip link del veth-hv-1 2>/dev/null || true
    ip link del veth-tap 2>/dev/null || true

    for ns in hv-1 hv-2 vm-1 vm-2; do
        ip netns del $ns 2>/dev/null || true
    done
}

# Clean start
cleanup &> /dev/null

[[ $1 == "clean" ]] && exit 0

trap cleanup EXIT

# Setup "Hypervisors" simulated with netns
ip link add veth-hv-1 type veth peer name veth-hv-2
setup-hv-networking() {
    hv=$1

    ip netns add hv-$hv
    ip link set veth-hv-$hv netns hv-$hv
    ip -netns hv-$hv link set veth-hv-$hv name veth0

    ip -netns hv-$hv link add vrf-underlay type vrf table 1
    ip -netns hv-$hv link set vrf-underlay up
    ip -netns hv-$hv addr add 172.16.0.$hv/24 dev veth0
    ip -netns hv-$hv link set veth0 up

    ip -netns hv-$hv link add br0 type bridge
    ip -netns hv-$hv link set br0 up

    ip -netns hv-$hv link add vxlan0 type vxlan id 10 local 172.16.0.$hv dev veth0 dstport 4789
    ip -netns hv-$hv link set vxlan0 master br0
    ip -netns hv-$hv link set vxlan0 up
}
setup-hv-networking 1
setup-hv-networking 2

# Check connectivity between HVs by pinging hv-2 from hv-1
echo -n "Checking HV connectivity                                           "
ip netns exec hv-1 ping -c 1 -W 1 172.16.0.2 &> /dev/null || (echo "[FAIL]"; false)
echo "[ OK ]"

# Setups a "VM" simulated by a netns an a veth pair
setup-vm() {
    id=$1

    ip netns add vm-$id
    ip link add veth-tap type veth peer name veth-hv

    ip link set veth-tap netns hv-$id
    ip -netns hv-$id link set veth-tap master br0
    ip -netns hv-$id link set veth-tap up

    ip link set veth-hv address 02:1d:8d:dd:0c:6$id

    ip link set veth-hv netns vm-$id
    ip -netns vm-$id addr add 10.0.0.$id/24 dev veth-hv
    ip -netns vm-$id link set veth-hv up
}
setup-vm 1
setup-vm 2

# Setup VTEP routes to make ARP work
bridge -netns hv-1 fdb add 00:00:00:00:00:00 dev vxlan0 dst 172.16.0.2 self permanent
bridge -netns hv-2 fdb add 00:00:00:00:00:00 dev vxlan0 dst 172.16.0.1 self permanent

echo -n "Check VM connectivity through VXLAN (underlay in the default VRF)  "
ip netns exec vm-1 ping -c 1 -W 1 10.0.0.2 &> /dev/null || (echo "[FAIL]"; false)
echo "[ OK ]"

# Move the underlay to a non-default VRF
ip -netns hv-1 link set veth0 vrf vrf-underlay
ip -netns hv-1 link set vxlan0 down
ip -netns hv-1 link set vxlan0 up
ip -netns hv-2 link set veth0 vrf vrf-underlay
ip -netns hv-2 link set vxlan0 down
ip -netns hv-2 link set vxlan0 up

echo -n "Check VM connectivity through VXLAN (underlay in a VRF)            "
ip netns exec vm-1 ping -c 1 -W 1 10.0.0.2 &> /dev/null || (echo "[FAIL]"; false)
echo "[ OK ]"
