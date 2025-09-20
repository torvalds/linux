#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh

IP4=172.16.0.1/24
TGT4=172.16.0.2
IP6=2001:db8:1::1/64
TGT6=2001:db8:1::2

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

# Need FQ for TXTIME
ip netns exec $NS tc qdisc replace dev dummy0 root fq

# Test
BAD=0
TOTAL=0

check_result() {
    local ret=$1
    local got=$2
    local exp=$3
    local case=$4
    local xfail=$5
    local xf=
    local inc=

    if [ "$xfail" == "xfail" ]; then
	xf="(XFAIL)"
	inc=0
    else
	inc=1
    fi

    ((TOTAL++))
    if [ $ret -ne 0 ]; then
	echo "  Case $case returned $ret, expected 0 $xf"
	((BAD+=inc))
    elif [ "$2" != "$3" ]; then
	echo "  Case $case returned '$got', expected '$exp' $xf"
	((BAD+=inc))
    fi
}

for i in "-4 $TGT4" "-6 $TGT6"; do
    for p in u i r; do
	[ $p == "u" ] && prot=UDPv${i:1:2}
	[ $p == "i" ] && prot=ICMPv${i:1:2}
	[ $p == "r" ] && prot=RAWv${i:1:2}

	ts=$(ip netns exec $NS ./cmsg_sender -p $p $i 1234)
	check_result $? "$ts" "" "$prot - no options"

	ts=$(ip netns exec $NS ./cmsg_sender -p $p $i 1234 -t | wc -l)
	check_result $? "$ts" "2" "$prot - ts cnt"
	ts=$(ip netns exec $NS ./cmsg_sender -p $p $i 1234 -t |
		 sed -n "s/.*SCHED ts0 [0-9].*/OK/p")
	check_result $? "$ts" "OK" "$prot - ts0 SCHED"
	ts=$(ip netns exec $NS ./cmsg_sender -p $p $i 1234 -t |
		 sed -n "s/.*SND ts0 [0-9].*/OK/p")
	check_result $? "$ts" "OK" "$prot - ts0 SND"

	ts=$(ip netns exec $NS ./cmsg_sender -p $p $i 1234 -t -d 1000 |
		 awk '/SND/ { if ($3 > 1000) print "OK"; }')
	check_result $? "$ts" "OK" "$prot - TXTIME abs"

	[ "$KSFT_MACHINE_SLOW" = yes ] && xfail=xfail

	ts=$(ip netns exec $NS ./cmsg_sender -p $p $i 1234 -t -d 1000 |
		 awk '/SND/ {snd=$3}
		      /SCHED/ {sch=$3}
		      END { if (snd - sch > 500) print "OK";
			    else print snd, "-", sch, "<", 500; }')
	check_result $? "$ts" "OK" "$prot - TXTIME rel" $xfail
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
