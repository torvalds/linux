#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh

readonly KSFT_SKIP=4

IP4=192.0.2.1/24
TGT4=192.0.2.2
TGT4_RAW=192.0.2.3
IP6=2001:db8::1/64
TGT6=2001:db8::2
TGT6_RAW=2001:db8::3
PORT=1234
TOTAL_TESTS=0
FAILED_TESTS=0

if ! command -v jq &> /dev/null; then
    echo "SKIP cmsg_so_priroity.sh test: jq is not installed." >&2
    exit "$KSFT_SKIP"
fi

check_result() {
    ((TOTAL_TESTS++))
    if [ "$1" -ne 0 ]; then
        ((FAILED_TESTS++))
    fi
}

cleanup()
{
    cleanup_ns $NS
}

trap cleanup EXIT

setup_ns NS

create_filter() {
    local handle=$1
    local vlan_prio=$2
    local ip_type=$3
    local proto=$4
    local dst_ip=$5
    local ip_proto

    if [[ "$proto" == "u" ]]; then
        ip_proto="udp"
    elif [[ "$ip_type" == "ipv4" && "$proto" == "i" ]]; then
        ip_proto="icmp"
    elif [[ "$ip_type" == "ipv6" && "$proto" == "i" ]]; then
        ip_proto="icmpv6"
    fi

    tc -n $NS filter add dev dummy1 \
        egress pref 1 handle "$handle" proto 802.1q \
        flower vlan_prio "$vlan_prio" vlan_ethtype "$ip_type" \
        dst_ip "$dst_ip" ${ip_proto:+ip_proto $ip_proto} \
        action pass
}

ip -n $NS link set dev lo up
ip -n $NS link add name dummy1 up type dummy

ip -n $NS link add link dummy1 name dummy1.10 up type vlan id 10 \
    egress-qos-map 0:0 1:1 2:2 3:3 4:4 5:5 6:6 7:7

ip -n $NS address add $IP4 dev dummy1.10
ip -n $NS address add $IP6 dev dummy1.10 nodad

ip netns exec $NS sysctl -wq net.ipv4.ping_group_range='0 2147483647'

ip -n $NS neigh add $TGT4 lladdr 00:11:22:33:44:55 nud permanent \
    dev dummy1.10
ip -n $NS neigh add $TGT6 lladdr 00:11:22:33:44:55 nud permanent \
    dev dummy1.10
ip -n $NS neigh add $TGT4_RAW lladdr 00:11:22:33:44:66 nud permanent \
    dev dummy1.10
ip -n $NS neigh add $TGT6_RAW lladdr 00:11:22:33:44:66 nud permanent \
    dev dummy1.10

tc -n $NS qdisc add dev dummy1 clsact

FILTER_COUNTER=10

for i in 4 6; do
    for proto in u i r; do
        echo "Test IPV$i, prot: $proto"
        for priority in {0..7}; do
            if [[ $i == 4 && $proto == "r" ]]; then
                TGT=$TGT4_RAW
            elif [[ $i == 6 && $proto == "r" ]]; then
                TGT=$TGT6_RAW
            elif [ $i == 4 ]; then
                TGT=$TGT4
            else
                TGT=$TGT6
            fi

            handle="${FILTER_COUNTER}${priority}"

            create_filter $handle $priority ipv$i $proto $TGT

            pkts=$(tc -n $NS -j -s filter show dev dummy1 egress \
                | jq ".[] | select(.options.handle == ${handle}) | \
                .options.actions[0].stats.packets")

            if [[ $pkts == 0 ]]; then
                check_result 0
            else
                echo "prio $priority: expected 0, got $pkts"
                check_result 1
            fi

            ip netns exec $NS ./cmsg_sender -$i -Q $priority \
	            -p $proto $TGT $PORT

            pkts=$(tc -n $NS -j -s filter show dev dummy1 egress \
                | jq ".[] | select(.options.handle == ${handle}) | \
                .options.actions[0].stats.packets")
            if [[ $pkts == 1 ]]; then
                check_result 0
            else
                echo "prio $priority -Q: expected 1, got $pkts"
                check_result 1
            fi

            ip netns exec $NS ./cmsg_sender -$i -P $priority \
	            -p $proto $TGT $PORT

            pkts=$(tc -n $NS -j -s filter show dev dummy1 egress \
                | jq ".[] | select(.options.handle == ${handle}) | \
                .options.actions[0].stats.packets")
            if [[ $pkts == 2 ]]; then
                check_result 0
            else
                echo "prio $priority -P: expected 2, got $pkts"
                check_result 1
            fi
        done
        FILTER_COUNTER=$((FILTER_COUNTER + 10))
    done
done

if [ $FAILED_TESTS -ne 0 ]; then
    echo "FAIL - $FAILED_TESTS/$TOTAL_TESTS tests failed"
    exit 1
else
    echo "OK - All $TOTAL_TESTS tests passed"
    exit 0
fi
