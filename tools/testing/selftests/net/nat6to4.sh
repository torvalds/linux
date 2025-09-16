#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

NS="ns-peer-$(mktemp -u XXXXXX)"

ip netns add "${NS}"
ip -netns "${NS}" link set lo up
ip -netns "${NS}" route add default via 127.0.0.2 dev lo

tc -n "${NS}" qdisc add dev lo ingress
tc -n "${NS}" filter add dev lo ingress prio 4 protocol ip \
   bpf object-file nat6to4.bpf.o section schedcls/egress4/snat4 direct-action

ip netns exec "${NS}" \
   bash -c 'echo 012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789abc | socat - UDP4-DATAGRAM:224.1.0.1:6666,ip-multicast-loop=1'
