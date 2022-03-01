#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

NS=ns
IP4=172.16.0.1/24
TGT4=172.16.0.2
IP6=2001:db8:1::1/64
TGT6=2001:db8:1::2
MARK=1000

cleanup()
{
    ip netns del $NS
}

trap cleanup EXIT

# Namespaces
ip netns add $NS

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

ip netns exec $NS ./cmsg_so_mark $TGT4 1234 $((MARK + 1))
check_result $? 0 "IPv4 pass"
ip netns exec $NS ./cmsg_so_mark $TGT6 1234 $((MARK + 1))
check_result $? 0 "IPv6 pass"

ip netns exec $NS ./cmsg_so_mark $TGT4 1234 $MARK
check_result $? 1 "IPv4 rejection"
ip netns exec $NS ./cmsg_so_mark $TGT6 1234 $MARK
check_result $? 1 "IPv6 rejection"

# Summary
if [ $BAD -ne 0 ]; then
    echo "FAIL - $BAD/$TOTAL cases failed"
    exit 1
else
    echo "OK"
    exit 0
fi
