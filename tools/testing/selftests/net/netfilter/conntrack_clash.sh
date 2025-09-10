#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh

clash_resolution_active=0
dport=22111
ret=0

cleanup()
{
	# netns cleanup also zaps any remaining socat echo server.
	cleanup_all_ns
}

checktool "nft --version" "run test without nft"
checktool "conntrack --version" "run test without conntrack"
checktool "socat -h" "run test without socat"

trap cleanup EXIT

setup_ns nsclient1 nsclient2 nsrouter

ip netns exec "$nsrouter" nft -f -<<EOF
table ip t {
	chain lb {
		meta l4proto udp dnat to numgen random mod 3 map { 0 : 10.0.2.1 . 9000, 1 : 10.0.2.1 . 9001, 2 : 10.0.2.1 . 9002 }
	}

	chain prerouting {
		type nat hook prerouting priority dstnat

		udp dport $dport counter jump lb
	}

	chain output {
		type nat hook output priority dstnat

		udp dport $dport counter jump lb
	}
}
EOF

load_simple_ruleset()
{
ip netns exec "$1" nft -f -<<EOF
table ip t {
	chain forward {
		type filter hook forward priority 0

		ct state new counter
	}
}
EOF
}

spawn_servers()
{
	local ns="$1"
	local ports="9000 9001 9002"

	for port in $ports; do
		ip netns exec "$ns" socat UDP-RECVFROM:$port,fork PIPE 2>/dev/null &
	done

	for port in $ports; do
		wait_local_port_listen "$ns" $port udp
	done
}

add_addr()
{
	local ns="$1"
	local dev="$2"
	local i="$3"
	local j="$4"

	ip -net "$ns" link set "$dev" up
	ip -net "$ns" addr add "10.0.$i.$j/24" dev "$dev"
}

ping_test()
{
	local ns="$1"
	local daddr="$2"

	if ! ip netns exec "$ns" ping -q -c 1 $daddr > /dev/null;then
		echo "FAIL: ping from $ns to $daddr"
		exit 1
	fi
}

run_one_clash_test()
{
	local ns="$1"
	local ctns="$2"
	local daddr="$3"
	local dport="$4"
	local entries
	local cre

	if ! ip netns exec "$ns" ./udpclash $daddr $dport;then
		echo "INFO: did not receive expected number of replies for $daddr:$dport"
		ip netns exec "$ctns" conntrack -S
		# don't fail: check if clash resolution triggered after all.
	fi

	entries=$(ip netns exec "$ctns" conntrack -S | wc -l)
	cre=$(ip netns exec "$ctns" conntrack -S | grep "clash_resolve=0" | wc -l)

	if [ "$cre" -ne "$entries" ];then
		clash_resolution_active=1
		return 0
	fi

	# not a failure: clash resolution logic did not trigger.
	# With right timing, xmit completed sequentially and
	# no parallel insertion occurs.
	return $ksft_skip
}

run_clash_test()
{
	local ns="$1"
	local ctns="$2"
	local daddr="$3"
	local dport="$4"
	local softerr=0

	for i in $(seq 1 10);do
		run_one_clash_test "$ns" "$ctns" "$daddr" "$dport"
		local rv=$?
		if [ $rv -eq 0 ];then
			echo "PASS: clash resolution test for $daddr:$dport on attempt $i"
			return 0
		elif [ $rv -eq $ksft_skip ]; then
			softerr=1
		fi
	done

	[ $softerr -eq 1 ] && echo "SKIP: clash resolution for $daddr:$dport did not trigger"
}

ip link add veth0 netns "$nsclient1" type veth peer name veth0 netns "$nsrouter"
ip link add veth0 netns "$nsclient2" type veth peer name veth1 netns "$nsrouter"
add_addr "$nsclient1" veth0 1 1
add_addr "$nsclient2" veth0 2 1
add_addr "$nsrouter" veth0 1 99
add_addr "$nsrouter" veth1 2 99

ip -net "$nsclient1" route add default via 10.0.1.99
ip -net "$nsclient2" route add default via 10.0.2.99
ip netns exec "$nsrouter" sysctl -q net.ipv4.ip_forward=1

ping_test "$nsclient1" 10.0.1.99
ping_test "$nsclient1" 10.0.2.1
ping_test "$nsclient2" 10.0.1.1

spawn_servers "$nsclient2"

# exercise clash resolution with nat:
# nsrouter is supposed to dnat to 10.0.2.1:900{0,1,2,3}.
run_clash_test "$nsclient1" "$nsrouter" 10.0.1.99 "$dport"

# exercise clash resolution without nat.
load_simple_ruleset "$nsclient2"
run_clash_test "$nsclient2" "$nsclient2" 127.0.0.1 9001

if [ $clash_resolution_active -eq 0 ];then
	[ "$ret" -eq 0 ] && ret=$ksft_skip
	echo "SKIP: Clash resolution did not trigger"
fi

exit $ret
