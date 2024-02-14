#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

function pktgen {
    ../pktgen/pktgen_bench_xmit_mode_netif_receive.sh -i $IFC -s 64 \
        -m 90:e2:ba:ff:ff:ff -d 192.168.0.1 -t 4
    local dropped=`tc -s qdisc show dev $IFC | tail -3 | awk '/drop/{print $7}'`
    if [ "$dropped" == "0," ]; then
        echo "FAIL"
    else
        echo "Successfully filtered " $dropped " packets"
    fi
}

function test {
    echo -n "Loading bpf program '$2'... "
    tc qdisc add dev $IFC clsact
    tc filter add dev $IFC ingress bpf da obj $1 sec $2
    local status=$?
    if [ $status -ne 0 ]; then
        echo "FAIL"
    else
        echo "ok"
	pktgen
    fi
    tc qdisc del dev $IFC clsact
}

IFC=test_veth

ip link add name $IFC type veth peer name pair_$IFC
ip link set $IFC up
ip link set pair_$IFC up

test ./parse_simple.o simple
test ./parse_varlen.o varlen
test ./parse_ldabs.o ldabs
ip link del dev $IFC
