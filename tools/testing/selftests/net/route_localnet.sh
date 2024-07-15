#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Run a couple of tests when route_localnet = 1.

readonly PEER_NS="ns-peer-$(mktemp -u XXXXXX)"

setup() {
    ip netns add "${PEER_NS}"
    ip -netns "${PEER_NS}" link set dev lo up
    ip link add name veth0 type veth peer name veth1
    ip link set dev veth0 up
    ip link set dev veth1 netns "${PEER_NS}"

    # Enable route_localnet and delete useless route 127.0.0.0/8.
    sysctl -w net.ipv4.conf.veth0.route_localnet=1
    ip netns exec "${PEER_NS}" sysctl -w net.ipv4.conf.veth1.route_localnet=1
    ip route del 127.0.0.0/8 dev lo table local
    ip netns exec "${PEER_NS}" ip route del 127.0.0.0/8 dev lo table local

    ip address add 127.25.3.4/24 dev veth0
    ip link set dev veth0 up
    ip netns exec "${PEER_NS}" ip address add 127.25.3.14/24 dev veth1
    ip netns exec "${PEER_NS}" ip link set dev veth1 up

    ip route flush cache
    ip netns exec "${PEER_NS}" ip route flush cache
}

cleanup() {
    ip link del veth0
    ip route add local 127.0.0.0/8 dev lo proto kernel scope host src 127.0.0.1
    local -r ns="$(ip netns list|grep $PEER_NS)"
    [ -n "$ns" ] && ip netns del $ns 2>/dev/null
}

# Run test when arp_announce = 2.
run_arp_announce_test() {
    echo "run arp_announce test"
    setup

    sysctl -w net.ipv4.conf.veth0.arp_announce=2
    ip netns exec "${PEER_NS}" sysctl -w net.ipv4.conf.veth1.arp_announce=2
    ping -c5 -I veth0 127.25.3.14
    if [ $? -ne 0 ];then
        echo "failed"
    else
        echo "ok"
    fi

    cleanup
}

# Run test when arp_ignore = 3.
run_arp_ignore_test() {
    echo "run arp_ignore test"
    setup

    sysctl -w net.ipv4.conf.veth0.arp_ignore=3
    ip netns exec "${PEER_NS}" sysctl -w net.ipv4.conf.veth1.arp_ignore=3
    ping -c5 -I veth0 127.25.3.14
    if [ $? -ne 0 ];then
        echo "failed"
    else
        echo "ok"
    fi

    cleanup
}

run_all_tests() {
    run_arp_announce_test
    run_arp_ignore_test
}

run_all_tests
