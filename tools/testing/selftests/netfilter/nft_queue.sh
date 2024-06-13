#!/bin/bash
#
# This tests nf_queue:
# 1. can process packets from all hooks
# 2. support running nfqueue from more than one base chain
#
# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4
ret=0

sfx=$(mktemp -u "XXXXXXXX")
ns1="ns1-$sfx"
ns2="ns2-$sfx"
nsrouter="nsrouter-$sfx"
timeout=4

cleanup()
{
	ip netns pids ${ns1} | xargs kill 2>/dev/null
	ip netns pids ${ns2} | xargs kill 2>/dev/null
	ip netns pids ${nsrouter} | xargs kill 2>/dev/null

	ip netns del ${ns1}
	ip netns del ${ns2}
	ip netns del ${nsrouter}
	rm -f "$TMPFILE0"
	rm -f "$TMPFILE1"
	rm -f "$TMPFILE2" "$TMPFILE3"
}

nft --version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without nft tool"
	exit $ksft_skip
fi

ip -Version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

ip netns add ${nsrouter}
if [ $? -ne 0 ];then
	echo "SKIP: Could not create net namespace"
	exit $ksft_skip
fi

TMPFILE0=$(mktemp)
TMPFILE1=$(mktemp)
TMPFILE2=$(mktemp)
TMPFILE3=$(mktemp)
trap cleanup EXIT

ip netns add ${ns1}
ip netns add ${ns2}

ip link add veth0 netns ${nsrouter} type veth peer name eth0 netns ${ns1} > /dev/null 2>&1
if [ $? -ne 0 ];then
    echo "SKIP: No virtual ethernet pair device support in kernel"
    exit $ksft_skip
fi
ip link add veth1 netns ${nsrouter} type veth peer name eth0 netns ${ns2}

ip -net ${nsrouter} link set lo up
ip -net ${nsrouter} link set veth0 up
ip -net ${nsrouter} addr add 10.0.1.1/24 dev veth0
ip -net ${nsrouter} addr add dead:1::1/64 dev veth0

ip -net ${nsrouter} link set veth1 up
ip -net ${nsrouter} addr add 10.0.2.1/24 dev veth1
ip -net ${nsrouter} addr add dead:2::1/64 dev veth1

ip -net ${ns1} link set lo up
ip -net ${ns1} link set eth0 up

ip -net ${ns2} link set lo up
ip -net ${ns2} link set eth0 up

ip -net ${ns1} addr add 10.0.1.99/24 dev eth0
ip -net ${ns1} addr add dead:1::99/64 dev eth0
ip -net ${ns1} route add default via 10.0.1.1
ip -net ${ns1} route add default via dead:1::1

ip -net ${ns2} addr add 10.0.2.99/24 dev eth0
ip -net ${ns2} addr add dead:2::99/64 dev eth0
ip -net ${ns2} route add default via 10.0.2.1
ip -net ${ns2} route add default via dead:2::1

load_ruleset() {
	local name=$1
	local prio=$2

ip netns exec ${nsrouter} nft -f /dev/stdin <<EOF
table inet $name {
	chain nfq {
		ip protocol icmp queue bypass
		icmpv6 type { "echo-request", "echo-reply" } queue num 1 bypass
	}
	chain pre {
		type filter hook prerouting priority $prio; policy accept;
		jump nfq
	}
	chain input {
		type filter hook input priority $prio; policy accept;
		jump nfq
	}
	chain forward {
		type filter hook forward priority $prio; policy accept;
		tcp dport 12345 queue num 2
		jump nfq
	}
	chain output {
		type filter hook output priority $prio; policy accept;
		tcp dport 12345 queue num 3
		tcp sport 23456 queue num 3
		jump nfq
	}
	chain post {
		type filter hook postrouting priority $prio; policy accept;
		jump nfq
	}
}
EOF
}

load_counter_ruleset() {
	local prio=$1

ip netns exec ${nsrouter} nft -f /dev/stdin <<EOF
table inet countrules {
	chain pre {
		type filter hook prerouting priority $prio; policy accept;
		counter
	}
	chain input {
		type filter hook input priority $prio; policy accept;
		counter
	}
	chain forward {
		type filter hook forward priority $prio; policy accept;
		counter
	}
	chain output {
		type filter hook output priority $prio; policy accept;
		counter
	}
	chain post {
		type filter hook postrouting priority $prio; policy accept;
		counter
	}
}
EOF
}

test_ping() {
  ip netns exec ${ns1} ping -c 1 -q 10.0.2.99 > /dev/null
  if [ $? -ne 0 ];then
	return 1
  fi

  ip netns exec ${ns1} ping -c 1 -q dead:2::99 > /dev/null
  if [ $? -ne 0 ];then
	return 1
  fi

  return 0
}

test_ping_router() {
  ip netns exec ${ns1} ping -c 1 -q 10.0.2.1 > /dev/null
  if [ $? -ne 0 ];then
	return 1
  fi

  ip netns exec ${ns1} ping -c 1 -q dead:2::1 > /dev/null
  if [ $? -ne 0 ];then
	return 1
  fi

  return 0
}

test_queue_blackhole() {
	local proto=$1

ip netns exec ${nsrouter} nft -f /dev/stdin <<EOF
table $proto blackh {
	chain forward {
	type filter hook forward priority 0; policy accept;
		queue num 600
	}
}
EOF
	if [ $proto = "ip" ] ;then
		ip netns exec ${ns1} ping -W 2 -c 1 -q 10.0.2.99 > /dev/null
		lret=$?
	elif [ $proto = "ip6" ]; then
		ip netns exec ${ns1} ping -W 2 -c 1 -q dead:2::99 > /dev/null
		lret=$?
	else
		lret=111
	fi

	# queue without bypass keyword should drop traffic if no listener exists.
	if [ $lret -eq 0 ];then
		echo "FAIL: $proto expected failure, got $lret" 1>&2
		exit 1
	fi

	ip netns exec ${nsrouter} nft delete table $proto blackh
	if [ $? -ne 0 ] ;then
	        echo "FAIL: $proto: Could not delete blackh table"
	        exit 1
	fi

        echo "PASS: $proto: statement with no listener results in packet drop"
}

test_queue()
{
	local expected=$1
	local last=""

	# spawn nf-queue listeners
	ip netns exec ${nsrouter} ./nf-queue -c -q 0 -t $timeout > "$TMPFILE0" &
	ip netns exec ${nsrouter} ./nf-queue -c -q 1 -t $timeout > "$TMPFILE1" &
	sleep 1
	test_ping
	ret=$?
	if [ $ret -ne 0 ];then
		echo "FAIL: netns routing/connectivity with active listener on queue $queue: $ret" 1>&2
		exit $ret
	fi

	test_ping_router
	ret=$?
	if [ $ret -ne 0 ];then
		echo "FAIL: netns router unreachable listener on queue $queue: $ret" 1>&2
		exit $ret
	fi

	wait
	ret=$?

	for file in $TMPFILE0 $TMPFILE1; do
		last=$(tail -n1 "$file")
		if [ x"$last" != x"$expected packets total" ]; then
			echo "FAIL: Expected $expected packets total, but got $last" 1>&2
			cat "$file" 1>&2

			ip netns exec ${nsrouter} nft list ruleset
			exit 1
		fi
	done

	echo "PASS: Expected and received $last"
}

test_tcp_forward()
{
	ip netns exec ${nsrouter} ./nf-queue -q 2 -t $timeout &
	local nfqpid=$!

	tmpfile=$(mktemp) || exit 1
	dd conv=sparse status=none if=/dev/zero bs=1M count=200 of=$tmpfile
	ip netns exec ${ns2} nc -w 5 -l -p 12345 <"$tmpfile" >/dev/null &
	local rpid=$!

	sleep 1
	ip netns exec ${ns1} nc -w 5 10.0.2.99 12345 <"$tmpfile" >/dev/null &

	rm -f "$tmpfile"

	wait $rpid
	wait $lpid
	[ $? -eq 0 ] && echo "PASS: tcp and nfqueue in forward chain"
}

test_tcp_localhost()
{
	tmpfile=$(mktemp) || exit 1

	dd conv=sparse status=none if=/dev/zero bs=1M count=200 of=$tmpfile
	ip netns exec ${nsrouter} nc -w 5 -l -p 12345 <"$tmpfile" >/dev/null &
	local rpid=$!

	ip netns exec ${nsrouter} ./nf-queue -q 3 -t $timeout &
	local nfqpid=$!

	sleep 1
	ip netns exec ${nsrouter} nc -w 5 127.0.0.1 12345 <"$tmpfile" > /dev/null
	rm -f "$tmpfile"

	wait $rpid
	[ $? -eq 0 ] && echo "PASS: tcp via loopback"
	wait 2>/dev/null
}

test_tcp_localhost_connectclose()
{
	tmpfile=$(mktemp) || exit 1

	ip netns exec ${nsrouter} ./connect_close -p 23456 -t $timeout &

	ip netns exec ${nsrouter} ./nf-queue -q 3 -t $timeout &
	local nfqpid=$!

	sleep 1
	rm -f "$tmpfile"

	wait $rpid
	[ $? -eq 0 ] && echo "PASS: tcp via loopback with connect/close"
	wait 2>/dev/null
}

test_tcp_localhost_requeue()
{
ip netns exec ${nsrouter} nft -f /dev/stdin <<EOF
flush ruleset
table inet filter {
	chain output {
		type filter hook output priority 0; policy accept;
		tcp dport 12345 limit rate 1/second burst 1 packets counter queue num 0
	}
	chain post {
		type filter hook postrouting priority 0; policy accept;
		tcp dport 12345 limit rate 1/second burst 1 packets counter queue num 0
	}
}
EOF
	tmpfile=$(mktemp) || exit 1
	dd conv=sparse status=none if=/dev/zero bs=1M count=200 of=$tmpfile
	ip netns exec ${nsrouter} nc -w 5 -l -p 12345 <"$tmpfile" >/dev/null &
	local rpid=$!

	ip netns exec ${nsrouter} ./nf-queue -c -q 1 -t $timeout > "$TMPFILE2" &

	# nfqueue 1 will be called via output hook.  But this time,
        # re-queue the packet to nfqueue program on queue 2.
	ip netns exec ${nsrouter} ./nf-queue -G -d 150 -c -q 0 -Q 1 -t $timeout > "$TMPFILE3" &

	sleep 1
	ip netns exec ${nsrouter} nc -w 5 127.0.0.1 12345 <"$tmpfile" > /dev/null
	rm -f "$tmpfile"

	wait

	if ! diff -u "$TMPFILE2" "$TMPFILE3" ; then
		echo "FAIL: lost packets during requeue?!" 1>&2
		return
	fi

	echo "PASS: tcp via loopback and re-queueing"
}

test_icmp_vrf() {
	ip -net $ns1 link add tvrf type vrf table 9876
	if [ $? -ne 0 ];then
		echo "SKIP: Could not add vrf device"
		return
	fi

	ip -net $ns1 li set eth0 master tvrf
	ip -net $ns1 li set tvrf up

	ip -net $ns1 route add 10.0.2.0/24 via 10.0.1.1 dev eth0 table 9876
ip netns exec ${ns1} nft -f /dev/stdin <<EOF
flush ruleset
table inet filter {
	chain output {
		type filter hook output priority 0; policy accept;
		meta oifname "tvrf" icmp type echo-request counter queue num 1
		meta oifname "eth0" icmp type echo-request counter queue num 1
	}
	chain post {
		type filter hook postrouting priority 0; policy accept;
		meta oifname "tvrf" icmp type echo-request counter queue num 1
		meta oifname "eth0" icmp type echo-request counter queue num 1
	}
}
EOF
	ip netns exec ${ns1} ./nf-queue -q 1 -t $timeout &
	local nfqpid=$!

	sleep 1
	ip netns exec ${ns1} ip vrf exec tvrf ping -c 1 10.0.2.99 > /dev/null

	for n in output post; do
		for d in tvrf eth0; do
			ip netns exec ${ns1} nft list chain inet filter $n | grep -q "oifname \"$d\" icmp type echo-request counter packets 1"
			if [ $? -ne 0 ] ; then
				echo "FAIL: chain $n: icmp packet counter mismatch for device $d" 1>&2
				ip netns exec ${ns1} nft list ruleset
				ret=1
				return
			fi
		done
	done

	wait $nfqpid
	[ $? -eq 0 ] && echo "PASS: icmp+nfqueue via vrf"
	wait 2>/dev/null
}

ip netns exec ${nsrouter} sysctl net.ipv6.conf.all.forwarding=1 > /dev/null
ip netns exec ${nsrouter} sysctl net.ipv4.conf.veth0.forwarding=1 > /dev/null
ip netns exec ${nsrouter} sysctl net.ipv4.conf.veth1.forwarding=1 > /dev/null

load_ruleset "filter" 0

sleep 3

test_ping
ret=$?
if [ $ret -eq 0 ];then
	# queue bypass works (rules were skipped, no listener)
	echo "PASS: ${ns1} can reach ${ns2}"
else
	echo "FAIL: ${ns1} cannot reach ${ns2}: $ret" 1>&2
	exit $ret
fi

test_queue_blackhole ip
test_queue_blackhole ip6

# dummy ruleset to add base chains between the
# queueing rules.  We don't want the second reinject
# to re-execute the old hooks.
load_counter_ruleset 10

# we are hooking all: prerouting/input/forward/output/postrouting.
# we ping ${ns2} from ${ns1} via ${nsrouter} using ipv4 and ipv6, so:
# 1x icmp prerouting,forward,postrouting -> 3 queue events (6 incl. reply).
# 1x icmp prerouting,input,output postrouting -> 4 queue events incl. reply.
# so we expect that userspace program receives 10 packets.
test_queue 10

# same.  We queue to a second program as well.
load_ruleset "filter2" 20
test_queue 20

test_tcp_forward
test_tcp_localhost
test_tcp_localhost_connectclose
test_tcp_localhost_requeue
test_icmp_vrf

exit $ret
