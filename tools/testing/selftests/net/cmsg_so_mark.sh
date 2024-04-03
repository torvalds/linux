#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh

IP4=172.16.0.1/24
TGT4=172.16.0.2
IP6=2001:db8:1::1/64
TGT6=2001:db8:1::2
MARK=1000

cleanup()
{
    cleanup_ns $NS
}

trap cleanup EXIT

# Namespaces
setup_ns NS

ip netns exec $NS sysctl -w net.ipv4.ping_group_range='0 2147483647' > /dev/null

# Connectivity
ip -netns $NS link add type dummy
ip -netns $NS link set dev dummy0 up
ip -netns $NS addr add $IP4 dev dummy0
ip -netns $NS addr add $IP6 dev dummy0

ip -netns $NS rule add fwmark $MARK lookup 300
ip -6 -netns $NS rule add fwmark $MARK lookup 300
ip -netns $NS route add prohibit any table 300
ip -6 -netns $NS route add prohibit any table 300

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

for ovr in setsock cmsg both; do
    for i in 4 6; do
	[ $i == 4 ] && TGT=$TGT4 || TGT=$TGT6

	for p in u i r; do
	    [ $p == "u" ] && prot=UDP
	    [ $p == "i" ] && prot=ICMP
	    [ $p == "r" ] && prot=RAW

	    [ $ovr == "setsock" ] && m="-M"
	    [ $ovr == "cmsg" ]    && m="-m"
	    [ $ovr == "both" ]    && m="-M $MARK -m"

	    ip netns exec $NS ./cmsg_sender -$i -p $p $m $((MARK + 1)) $TGT 1234
	    check_result $? 0 "$prot $ovr - pass"

	    [ $ovr == "diff" ] && m="-M $((MARK + 1)) -m"

	    ip netns exec $NS ./cmsg_sender -$i -p $p $m $MARK -s $TGT 1234
	    check_result $? 1 "$prot $ovr - rejection"
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
