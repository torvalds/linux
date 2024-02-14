#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ksft_skip=4

NS=ns
IP6=2001:db8:1::1/64
TGT6=2001:db8:1::2
TMPF=$(mktemp --suffix ".pcap")

cleanup()
{
    rm -f $TMPF
    ip netns del $NS
}

trap cleanup EXIT

NSEXE="ip netns exec $NS"

tcpdump -h | grep immediate-mode >> /dev/null
if [ $? -ne 0 ]; then
    echo "SKIP - tcpdump with --immediate-mode option required"
    exit $ksft_skip
fi

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

# IPV6_TCLASS
TOS=0x10
TOS2=0x20

ip -6 -netns $NS rule add tos $TOS lookup 300
ip -6 -netns $NS route add table 300 prohibit any

for ovr in setsock cmsg both diff; do
    for p in u i r; do
	[ $p == "u" ] && prot=UDP
	[ $p == "i" ] && prot=ICMP
	[ $p == "r" ] && prot=RAW

	[ $ovr == "setsock" ] && m="-C"
	[ $ovr == "cmsg" ]    && m="-c"
	[ $ovr == "both" ]    && m="-C $((TOS2)) -c"
	[ $ovr == "diff" ]    && m="-C $((TOS )) -c"

	$NSEXE nohup tcpdump --immediate-mode -p -ni dummy0 -w $TMPF -c 4 2> /dev/null &
	BG=$!
	sleep 0.05

	$NSEXE ./cmsg_sender -6 -p $p $m $((TOS2)) $TGT6 1234
	check_result $? 0 "TCLASS $prot $ovr - pass"

	while [ -d /proc/$BG ]; do
	    $NSEXE ./cmsg_sender -6 -p u $TGT6 1234
	done

	tcpdump -r $TMPF -v 2>&1 | grep "class $TOS2" >> /dev/null
	check_result $? 0 "TCLASS $prot $ovr - packet data"
	rm $TMPF

	[ $ovr == "both" ]    && m="-C $((TOS )) -c"
	[ $ovr == "diff" ]    && m="-C $((TOS2)) -c"

	$NSEXE ./cmsg_sender -6 -p $p $m $((TOS)) -s $TGT6 1234
	check_result $? 1 "TCLASS $prot $ovr - rejection"
    done
done

# IPV6_HOPLIMIT
LIM=4

for ovr in setsock cmsg both diff; do
    for p in u i r; do
	[ $p == "u" ] && prot=UDP
	[ $p == "i" ] && prot=ICMP
	[ $p == "r" ] && prot=RAW

	[ $ovr == "setsock" ] && m="-L"
	[ $ovr == "cmsg" ]    && m="-l"
	[ $ovr == "both" ]    && m="-L $LIM -l"
	[ $ovr == "diff" ]    && m="-L $((LIM + 1)) -l"

	$NSEXE nohup tcpdump --immediate-mode -p -ni dummy0 -w $TMPF -c 4 2> /dev/null &
	BG=$!
	sleep 0.05

	$NSEXE ./cmsg_sender -6 -p $p $m $LIM $TGT6 1234
	check_result $? 0 "HOPLIMIT $prot $ovr - pass"

	while [ -d /proc/$BG ]; do
	    $NSEXE ./cmsg_sender -6 -p u $TGT6 1234
	done

	tcpdump -r $TMPF -v 2>&1 | grep "hlim $LIM[^0-9]" >> /dev/null
	check_result $? 0 "HOPLIMIT $prot $ovr - packet data"
	rm $TMPF
    done
done

# IPV6 exthdr
for p in u i r; do
    # Very basic "does it crash" test
    for h in h d r; do
	$NSEXE ./cmsg_sender -p $p -6 -H $h $TGT6 1234
	check_result $? 0 "ExtHdr $prot $ovr - pass"
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
