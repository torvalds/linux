#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

NS=ns
IP6=2001:db8:1::1/64
TGT6=2001:db8:1::2

cleanup()
{
    ip netns del $NS
}

trap cleanup EXIT

NSEXE="ip netns exec $NS"

# Namespaces
ip netns add $NS

$NSEXE sysctl -w net.ipv4.ping_group_range='0 2147483647' > /dev/null

# Connectivity
ip -netns $NS link add type dummy
ip -netns $NS link set dev dummy0 up
ip -netns $NS addr add $IP6 dev dummy0

# Test
BAD=0
TOTAL=0

check_result() {
    ((TOTAL++))
    if [ $1 -ne $2 ]; then
	echo "  Case $3 returned $1, expected $2"
	((BAD++))
    fi
}

# IPV6_DONTFRAG
for ovr in setsock cmsg both diff; do
    for df in 0 1; do
	for p in u i r; do
	    [ $p == "u" ] && prot=UDP
	    [ $p == "i" ] && prot=ICMP
	    [ $p == "r" ] && prot=RAW

	    [ $ovr == "setsock" ] && m="-F $df"
	    [ $ovr == "cmsg" ]    && m="-f $df"
	    [ $ovr == "both" ]    && m="-F $df -f $df"
	    [ $ovr == "diff" ]    && m="-F $((1 - df)) -f $df"

	    $NSEXE ./cmsg_sender -s -S 2000 -6 -p $p $m $TGT6 1234
	    check_result $? $df "DONTFRAG $prot $ovr"
	done
    done
done

# Summary
if [ $BAD -ne 0 ]; then
    echo "FAIL - $BAD/$TOTAL cases failed"
    exit 1
else
    echo "OK"
    exit 0
fi
