#!/bin/bash
#
# This tests nf_queue:
# 1. can process packets from all hooks
# 2. support running nfqueue from more than one base chain
#
# shellcheck disable=SC2162,SC2317

source lib.sh
ret=0
timeout=5

cleanup()
{
	ip netns pids "$ns1" | xargs kill 2>/dev/null
	ip netns pids "$ns2" | xargs kill 2>/dev/null
	ip netns pids "$nsrouter" | xargs kill 2>/dev/null

	cleanup_all_ns

	rm -f "$TMPINPUT"
	rm -f "$TMPFILE0"
	rm -f "$TMPFILE1"
	rm -f "$TMPFILE2" "$TMPFILE3"
}

checktool "nft --version" "test without nft tool"
checktool "socat -h" "run test without socat"

modprobe -q sctp

trap cleanup EXIT

setup_ns ns1 ns2 ns3 nsrouter

TMPFILE0=$(mktemp)
TMPFILE1=$(mktemp)
TMPFILE2=$(mktemp)
TMPFILE3=$(mktemp)

TMPINPUT=$(mktemp)
COUNT=200
[ "$KSFT_MACHINE_SLOW" = "yes" ] && COUNT=25
dd conv=sparse status=none if=/dev/zero bs=1M count=$COUNT of="$TMPINPUT"

if ! ip link add veth0 netns "$nsrouter" type veth peer name eth0 netns "$ns1" > /dev/null 2>&1; then
    echo "SKIP: No virtual ethernet pair device support in kernel"
    exit $ksft_skip
fi
ip link add veth1 netns "$nsrouter" type veth peer name eth0 netns "$ns2"
ip link add veth2 netns "$nsrouter" type veth peer name eth0 netns "$ns3"

ip -net "$nsrouter" link set veth0 up
ip -net "$nsrouter" addr add 10.0.1.1/24 dev veth0
ip -net "$nsrouter" addr add dead:1::1/64 dev veth0 nodad

ip -net "$nsrouter" link set veth1 up
ip -net "$nsrouter" addr add 10.0.2.1/24 dev veth1
ip -net "$nsrouter" addr add dead:2::1/64 dev veth1 nodad

ip -net "$nsrouter" link set veth2 up
ip -net "$nsrouter" addr add 10.0.3.1/24 dev veth2
ip -net "$nsrouter" addr add dead:3::1/64 dev veth2 nodad

ip -net "$ns1" link set eth0 up
ip -net "$ns2" link set eth0 up
ip -net "$ns3" link set eth0 up

ip -net "$ns1" addr add 10.0.1.99/24 dev eth0
ip -net "$ns1" addr add dead:1::99/64 dev eth0 nodad
ip -net "$ns1" route add default via 10.0.1.1
ip -net "$ns1" route add default via dead:1::1

ip -net "$ns2" addr add 10.0.2.99/24 dev eth0
ip -net "$ns2" addr add dead:2::99/64 dev eth0 nodad
ip -net "$ns2" route add default via 10.0.2.1
ip -net "$ns2" route add default via dead:2::1

ip -net "$ns3" addr add 10.0.3.99/24 dev eth0
ip -net "$ns3" addr add dead:3::99/64 dev eth0 nodad
ip -net "$ns3" route add default via 10.0.3.1
ip -net "$ns3" route add default via dead:3::1

load_ruleset() {
	local name=$1
	local prio=$2

ip netns exec "$nsrouter" nft -f /dev/stdin <<EOF
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

ip netns exec "$nsrouter" nft -f /dev/stdin <<EOF
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
  if ! ip netns exec "$ns1" ping -c 1 -q 10.0.2.99 > /dev/null; then
	return 1
  fi

  if ! ip netns exec "$ns1" ping -c 1 -q dead:2::99 > /dev/null; then
	return 2
  fi

  return 0
}

test_ping_router() {
  if ! ip netns exec "$ns1" ping -c 1 -q 10.0.2.1 > /dev/null; then
	return 3
  fi

  if ! ip netns exec "$ns1" ping -c 1 -q dead:2::1 > /dev/null; then
	return 4
  fi

  return 0
}

test_queue_blackhole() {
	local proto=$1

ip netns exec "$nsrouter" nft -f /dev/stdin <<EOF
table $proto blackh {
	chain forward {
	type filter hook forward priority 0; policy accept;
		queue num 600
	}
}
EOF
	if [ "$proto" = "ip" ] ;then
		ip netns exec "$ns1" ping -W 2 -c 1 -q 10.0.2.99 > /dev/null
		lret=$?
	elif [ "$proto" = "ip6" ]; then
		ip netns exec "$ns1" ping -W 2 -c 1 -q dead:2::99 > /dev/null
		lret=$?
	else
		lret=111
	fi

	# queue without bypass keyword should drop traffic if no listener exists.
	if [ "$lret" -eq 0 ];then
		echo "FAIL: $proto expected failure, got $lret" 1>&2
		exit 1
	fi

	if ! ip netns exec "$nsrouter" nft delete table "$proto" blackh; then
	        echo "FAIL: $proto: Could not delete blackh table"
	        exit 1
	fi

        echo "PASS: $proto: statement with no listener results in packet drop"
}

nf_queue_wait()
{
	local procfile="/proc/self/net/netfilter/nfnetlink_queue"
	local netns id

	netns="$1"
	id="$2"

	# if this file doesn't exist, nfnetlink_module isn't loaded.
	# rather than loading it ourselves, wait for kernel module autoload
	# completion, nfnetlink should do so automatically because nf_queue
	# helper program, spawned in the background, asked for this functionality.
	test -f "$procfile" &&
		ip netns exec "$netns" cat "$procfile" | grep -q "^ *$id "
}

test_queue()
{
	local expected="$1"
	local last=""

	# spawn nf_queue listeners
	ip netns exec "$nsrouter" ./nf_queue -c -q 0 -t $timeout > "$TMPFILE0" &
	ip netns exec "$nsrouter" ./nf_queue -c -q 1 -t $timeout > "$TMPFILE1" &

	busywait "$BUSYWAIT_TIMEOUT" nf_queue_wait "$nsrouter" 0
	busywait "$BUSYWAIT_TIMEOUT" nf_queue_wait "$nsrouter" 1

	if ! test_ping;then
		echo "FAIL: netns routing/connectivity with active listener on queues 0 and 1: $ret" 1>&2
		exit $ret
	fi

	if ! test_ping_router;then
		echo "FAIL: netns router unreachable listener on queue 0 and 1: $ret" 1>&2
		exit $ret
	fi

	wait
	ret=$?

	for file in $TMPFILE0 $TMPFILE1; do
		last=$(tail -n1 "$file")
		if [ x"$last" != x"$expected packets total" ]; then
			echo "FAIL: Expected $expected packets total, but got $last" 1>&2
			ip netns exec "$nsrouter" nft list ruleset
			exit 1
		fi
	done

	echo "PASS: Expected and received $last"
}

listener_ready()
{
	ss -N "$1" -lnt -o "sport = :12345" | grep -q 12345
}

test_tcp_forward()
{
	ip netns exec "$nsrouter" ./nf_queue -q 2 &
	local nfqpid=$!

	timeout 5 ip netns exec "$ns2" socat -u TCP-LISTEN:12345 STDOUT >/dev/null &
	local rpid=$!

	busywait "$BUSYWAIT_TIMEOUT" listener_ready "$ns2"
	busywait "$BUSYWAIT_TIMEOUT" nf_queue_wait "$nsrouter" 2

	ip netns exec "$ns1" socat -u STDIN TCP:10.0.2.99:12345 <"$TMPINPUT" >/dev/null

	wait "$rpid" && echo "PASS: tcp and nfqueue in forward chain"
	kill "$nfqpid"
}

test_tcp_localhost()
{
	timeout 5 ip netns exec "$nsrouter" socat -u TCP-LISTEN:12345 STDOUT >/dev/null &
	local rpid=$!

	ip netns exec "$nsrouter" ./nf_queue -q 3 &
	local nfqpid=$!

	busywait "$BUSYWAIT_TIMEOUT" listener_ready "$nsrouter"
	busywait "$BUSYWAIT_TIMEOUT" nf_queue_wait "$nsrouter" 3

	ip netns exec "$nsrouter" socat -u STDIN TCP:127.0.0.1:12345 <"$TMPINPUT" >/dev/null

	wait "$rpid" && echo "PASS: tcp via loopback"
	kill "$nfqpid"
}

test_tcp_localhost_connectclose()
{
	ip netns exec "$nsrouter" ./nf_queue -q 3 &
	local nfqpid=$!

	busywait "$BUSYWAIT_TIMEOUT" nf_queue_wait "$nsrouter" 3

	timeout 10 ip netns exec "$nsrouter" ./connect_close -p 23456 -t 3

	kill "$nfqpid"
	wait && echo "PASS: tcp via loopback with connect/close"
}

test_tcp_localhost_requeue()
{
ip netns exec "$nsrouter" nft -f /dev/stdin <<EOF
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
	timeout 5 ip netns exec "$nsrouter" socat -u TCP-LISTEN:12345 STDOUT >/dev/null &
	local rpid=$!

	ip netns exec "$nsrouter" ./nf_queue -c -q 1 -t "$timeout" > "$TMPFILE2" &

	# nfqueue 1 will be called via output hook.  But this time,
        # re-queue the packet to nfqueue program on queue 2.
	ip netns exec "$nsrouter" ./nf_queue -G -d 150 -c -q 0 -Q 1 -t "$timeout" > "$TMPFILE3" &

	busywait "$BUSYWAIT_TIMEOUT" listener_ready "$nsrouter"
	ip netns exec "$nsrouter" socat -u STDIN TCP:127.0.0.1:12345 <"$TMPINPUT" > /dev/null

	wait

	if ! diff -u "$TMPFILE2" "$TMPFILE3" ; then
		echo "FAIL: lost packets during requeue?!" 1>&2
		return
	fi

	echo "PASS: tcp via loopback and re-queueing"
}

test_icmp_vrf() {
	if ! ip -net "$ns1" link add tvrf type vrf table 9876;then
		echo "SKIP: Could not add vrf device"
		return
	fi

	ip -net "$ns1" li set eth0 master tvrf
	ip -net "$ns1" li set tvrf up

	ip -net "$ns1" route add 10.0.2.0/24 via 10.0.1.1 dev eth0 table 9876
ip netns exec "$ns1" nft -f /dev/stdin <<EOF
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
	ip netns exec "$ns1" ./nf_queue -q 1 &
	local nfqpid=$!

	busywait "$BUSYWAIT_TIMEOUT" nf_queue_wait "$ns1" 1

	ip netns exec "$ns1" ip vrf exec tvrf ping -c 1 10.0.2.99 > /dev/null

	for n in output post; do
		for d in tvrf eth0; do
			if ! ip netns exec "$ns1" nft list chain inet filter "$n" | grep -q "oifname \"$d\" icmp type echo-request counter packets 1"; then
				kill "$nfqpid"
				echo "FAIL: chain $n: icmp packet counter mismatch for device $d" 1>&2
				ip netns exec "$ns1" nft list ruleset
				ret=1
				return
			fi
		done
	done

	kill "$nfqpid"
	echo "PASS: icmp+nfqueue via vrf"
}

sctp_listener_ready()
{
	ss -S -N "$1" -ln -o "sport = :12345" | grep -q 12345
}

check_output_files()
{
	local f1="$1"
	local f2="$2"
	local err="$3"

	if ! cmp "$f1" "$f2" ; then
		echo "FAIL: $err: input and output file differ" 1>&2
		echo -n " Input file" 1>&2
		ls -l "$f1" 1>&2
		echo -n "Output file" 1>&2
		ls -l "$f2" 1>&2
		ret=1
	fi
}

test_sctp_forward()
{
	ip netns exec "$nsrouter" nft -f /dev/stdin <<EOF
flush ruleset
table inet sctpq {
        chain forward {
        type filter hook forward priority 0; policy accept;
                sctp dport 12345 queue num 10
        }
}
EOF
	timeout 60 ip netns exec "$ns2" socat -u SCTP-LISTEN:12345 STDOUT > "$TMPFILE1" &
	local rpid=$!

	busywait "$BUSYWAIT_TIMEOUT" sctp_listener_ready "$ns2"

	ip netns exec "$nsrouter" ./nf_queue -q 10 -G &
	local nfqpid=$!

	ip netns exec "$ns1" socat -u STDIN SCTP:10.0.2.99:12345 <"$TMPINPUT" >/dev/null

	if ! ip netns exec "$nsrouter" nft delete table inet sctpq; then
		echo "FAIL:  Could not delete sctpq table"
		exit 1
	fi

	wait "$rpid" && echo "PASS: sctp and nfqueue in forward chain"
	kill "$nfqpid"

	check_output_files "$TMPINPUT" "$TMPFILE1" "sctp forward"
}

test_sctp_output()
{
        ip netns exec "$ns1" nft -f /dev/stdin <<EOF
table inet sctpq {
        chain output {
        type filter hook output priority 0; policy accept;
                sctp dport 12345 queue num 11
        }
}
EOF
	# reduce test file size, software segmentation causes sk wmem increase.
	dd conv=sparse status=none if=/dev/zero bs=1M count=$((COUNT/2)) of="$TMPINPUT"

	timeout 60 ip netns exec "$ns2" socat -u SCTP-LISTEN:12345 STDOUT > "$TMPFILE1" &
	local rpid=$!

	busywait "$BUSYWAIT_TIMEOUT" sctp_listener_ready "$ns2"

	ip netns exec "$ns1" ./nf_queue -q 11 &
	local nfqpid=$!

	ip netns exec "$ns1" socat -u STDIN SCTP:10.0.2.99:12345 <"$TMPINPUT" >/dev/null

	if ! ip netns exec "$ns1" nft delete table inet sctpq; then
		echo "FAIL:  Could not delete sctpq table"
		exit 1
	fi

	# must wait before checking completeness of output file.
	wait "$rpid" && echo "PASS: sctp and nfqueue in output chain with GSO"
	kill "$nfqpid"

	check_output_files "$TMPINPUT" "$TMPFILE1" "sctp output"
}

udp_listener_ready()
{
	ss -S -N "$1" -uln -o "sport = :12345" | grep -q 12345
}

output_files_written()
{
	test -s "$1" && test -s "$2"
}

test_udp_ct_race()
{
        ip netns exec "$nsrouter" nft -f /dev/stdin <<EOF
flush ruleset
table inet udpq {
	chain prerouting {
		type nat hook prerouting priority dstnat - 5; policy accept;
		ip daddr 10.6.6.6 udp dport 12345 counter dnat to numgen inc mod 2 map { 0 : 10.0.2.99, 1 : 10.0.3.99 }
	}
        chain postrouting {
		type filter hook postrouting priority srcnat - 5; policy accept;
		udp dport 12345 counter queue num 12
        }
}
EOF
	:> "$TMPFILE1"
	:> "$TMPFILE2"

	timeout 10 ip netns exec "$ns2" socat UDP-LISTEN:12345,fork,pf=ipv4 OPEN:"$TMPFILE1",trunc &
	local rpid1=$!

	timeout 10 ip netns exec "$ns3" socat UDP-LISTEN:12345,fork,pf=ipv4 OPEN:"$TMPFILE2",trunc &
	local rpid2=$!

	ip netns exec "$nsrouter" ./nf_queue -q 12 -d 1000 &
	local nfqpid=$!

	busywait "$BUSYWAIT_TIMEOUT" udp_listener_ready "$ns2"
	busywait "$BUSYWAIT_TIMEOUT" udp_listener_ready "$ns3"
	busywait "$BUSYWAIT_TIMEOUT" nf_queue_wait "$nsrouter" 12

	# Send two packets, one should end up in ns1, other in ns2.
	# This is because nfqueue will delay packet for long enough so that
	# second packet will not find existing conntrack entry.
	echo "Packet 1" | ip netns exec "$ns1" socat -u STDIN UDP-DATAGRAM:10.6.6.6:12345,bind=0.0.0.0:55221
	echo "Packet 2" | ip netns exec "$ns1" socat -u STDIN UDP-DATAGRAM:10.6.6.6:12345,bind=0.0.0.0:55221

	busywait 10000 output_files_written "$TMPFILE1" "$TMPFILE2"

	kill "$nfqpid"

	if ! ip netns exec "$nsrouter" bash -c 'conntrack -L -p udp --dport 12345 2>/dev/null | wc -l | grep -q "^1"'; then
		echo "FAIL: Expected One udp conntrack entry"
		ip netns exec "$nsrouter" conntrack -L -p udp --dport 12345
		ret=1
	fi

	if ! ip netns exec "$nsrouter" nft delete table inet udpq; then
		echo "FAIL: Could not delete udpq table"
		ret=1
		return
	fi

	NUMLINES1=$(wc -l < "$TMPFILE1")
	NUMLINES2=$(wc -l < "$TMPFILE2")

	if [ "$NUMLINES1" -ne 1 ] || [ "$NUMLINES2" -ne 1 ]; then
		ret=1
		echo "FAIL: uneven udp packet distribution: $NUMLINES1 $NUMLINES2"
		echo -n "$TMPFILE1: ";cat "$TMPFILE1"
		echo -n "$TMPFILE2: ";cat "$TMPFILE2"
		return
	fi

	echo "PASS: both udp receivers got one packet each"
}

test_queue_removal()
{
	read tainted_then < /proc/sys/kernel/tainted

	ip netns exec "$ns1" nft -f - <<EOF
flush ruleset
table ip filter {
	chain output {
		type filter hook output priority 0; policy accept;
		ip protocol icmp queue num 0
	}
}
EOF
	ip netns exec "$ns1" ./nf_queue -q 0 -d 30000 &
	local nfqpid=$!

	busywait "$BUSYWAIT_TIMEOUT" nf_queue_wait "$ns1" 0

	ip netns exec "$ns1" ping -w 2 -f -c 10 127.0.0.1 -q >/dev/null
	kill $nfqpid

	ip netns exec "$ns1" nft flush ruleset

	if [ "$tainted_then" -ne 0 ];then
		return
	fi

	read tainted_now < /proc/sys/kernel/tainted
	if [ "$tainted_now" -eq 0 ];then
		echo "PASS: queue program exiting while packets queued"
	else
		echo "TAINT: queue program exiting while packets queued"
		ret=1
	fi
}

ip netns exec "$nsrouter" sysctl net.ipv6.conf.all.forwarding=1 > /dev/null
ip netns exec "$nsrouter" sysctl net.ipv4.conf.veth0.forwarding=1 > /dev/null
ip netns exec "$nsrouter" sysctl net.ipv4.conf.veth1.forwarding=1 > /dev/null
ip netns exec "$nsrouter" sysctl net.ipv4.conf.veth2.forwarding=1 > /dev/null

load_ruleset "filter" 0

if test_ping; then
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
ip netns exec "$ns1" nft flush ruleset

test_tcp_forward
test_tcp_localhost
test_tcp_localhost_connectclose
test_tcp_localhost_requeue
test_sctp_forward
test_sctp_output
test_udp_ct_race

# should be last, adds vrf device in ns1 and changes routes
test_icmp_vrf
test_queue_removal

exit $ret
