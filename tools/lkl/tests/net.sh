#!/bin/bash -e

echo "== TAP (LKL net) tests =="
if [ -c /dev/net/tun ]; then
    sudo ip link set dev lkl_ptt1 down || true
    sudo ip tuntap del dev lkl_ptt1 mode tap || true
    sudo ip tuntap add dev lkl_ptt1 mode tap user $USER
    sudo ip link set dev lkl_ptt1 up
    sudo ip addr add dev lkl_ptt1 192.168.14.1/24

    ./net-test tap lkl_ptt1 192.168.14.2 24 192.168.14.1

    sudo ip link set dev lkl_ptt1 down
    sudo ip tuntap del dev lkl_ptt1 mode tap
fi

# we disabled this DPDK test because it's unlikely possible to describe
# a generic set of commands for all environments to test with DPDK.  users
# may customize those test commands for your host.
if false ; then
    echo "== DPDK (LKL net) tests =="
    sudo ./net-test dpdk dpdk0 192.168.15.2 24 192.168.15.1
fi
