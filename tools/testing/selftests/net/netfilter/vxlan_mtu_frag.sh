#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh

if ! modprobe -q -n br_netfilter 2>&1; then
        echo "SKIP: Test needs br_netfilter kernel module"
        exit $ksft_skip
fi

cleanup()
{
        cleanup_all_ns
}

trap cleanup EXIT

setup_ns host vtep router

create_topology()
{
    ip link add host-eth0 netns "$host" type veth peer name vtep-host netns "$vtep"
    ip link add vtep-router netns "$vtep" type veth peer name router-vtep netns "$router"
}

setup_host()
{
    # bring ports up
    ip -n "$host" addr add 10.0.0.1/24 dev host-eth0
    ip -n "$host" link set host-eth0 up

    # Add VLAN 10,20
    for vid in 10 20; do
        ip -n "$host" link add link host-eth0 name host-eth0.$vid type vlan id $vid
        ip -n "$host" addr add 10.0.$vid.1/24 dev host-eth0.$vid
        ip -n "$host" link set host-eth0.$vid up
    done
}

setup_vtep()
{
    # create bridge on vtep
    ip -n "$vtep" link add name br0 type bridge
    ip -n "$vtep" link set br0 type bridge vlan_filtering 1

    # VLAN 10 is untagged PVID
    ip -n "$vtep" link set dev vtep-host master br0
    bridge -n "$vtep" vlan add dev vtep-host vid 10 pvid untagged

    # VLAN 20 as other VID
    ip -n "$vtep" link set dev vtep-host master br0
    bridge -n "$vtep" vlan add dev vtep-host vid 20

    # single-vxlan device on vtep
    ip -n "$vtep" address add dev vtep-router 60.0.0.1/24
    ip -n "$vtep" link add dev vxd type vxlan external \
        vnifilter local 60.0.0.1 remote 60.0.0.2 dstport 4789 ttl 64
    ip -n "$vtep" link set vxd master br0

    # Add VLAN-VNI 1-1 mappings
    bridge -n "$vtep" link set dev vxd vlan_tunnel on
    for vid in 10 20; do
        bridge -n "$vtep" vlan add dev vxd vid $vid
        bridge -n "$vtep" vlan add dev vxd vid $vid tunnel_info id $vid
        bridge -n "$vtep" vni add dev vxd vni $vid
    done

    # bring ports up
    ip -n "$vtep" link set vxd up
    ip -n "$vtep" link set vtep-router up
    ip -n "$vtep" link set vtep-host up
    ip -n "$vtep" link set dev br0 up
}

setup_router()
{
    # bring ports up
    ip -n "$router" link set router-vtep up
}

setup()
{
    modprobe -q br_netfilter
    create_topology
    setup_host
    setup_vtep
    setup_router
}

test_large_mtu_untagged_traffic()
{
    ip -n "$vtep" link set vxd mtu 1000
    ip -n "$host" neigh add 10.0.0.2 lladdr ca:fe:ba:be:00:01 dev host-eth0
    ip netns exec "$host" \
        ping -q 10.0.0.2 -I host-eth0 -c 1 -W 0.5 -s2000 > /dev/null 2>&1
    return 0
}

test_large_mtu_tagged_traffic()
{
    for vid in 10 20; do
        ip -n "$vtep" link set vxd mtu 1000
        ip -n "$host" neigh add 10.0.$vid.2 lladdr ca:fe:ba:be:00:01 dev host-eth0.$vid
        ip netns exec "$host" \
            ping -q 10.0.$vid.2 -I host-eth0.$vid -c 1 -W 0.5 -s2000 > /dev/null 2>&1
    done
    return 0
}

do_test()
{
    # Frames will be dropped so ping will not succeed
    # If it doesn't panic, it passes
    test_large_mtu_tagged_traffic
    test_large_mtu_untagged_traffic
}

setup && \
echo "Test for VxLAN fragmentation with large MTU in br_netfilter:" && \
do_test && echo "PASS!"
exit $?
