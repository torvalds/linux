#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Check that UNREPLIED tcp conntrack will eventually timeout.
#

source lib.sh

if ! nft --version > /dev/null 2>&1;then
	echo "SKIP: Could not run test without nft tool"
	exit $ksft_skip
fi

if ! conntrack --version > /dev/null 2>&1;then
	echo "SKIP: Could not run test without conntrack tool"
	exit $ksft_skip
fi

ret=0

cleanup() {
	ip netns pids "$ns1" | xargs kill 2>/dev/null
	ip netns pids "$ns2" | xargs kill 2>/dev/null

	cleanup_all_ns
}

ipv4() {
    echo -n 192.168."$1".2
}

check_counter()
{
	ns=$1
	name=$2
	expect=$3
	local lret=0

	if ! ip netns exec "$ns2" nft list counter inet filter "$name" | grep -q "$expect"; then
		echo "ERROR: counter $name in $ns2 has unexpected value (expected $expect)" 1>&2
		ip netns exec "$ns2" nft list counter inet filter "$name" 1>&2
		lret=1
	fi

	return $lret
}

trap cleanup EXIT

# Create test namespaces
setup_ns ns1 ns2

# Connect the namespace to the host using a veth pair
ip -net "$ns1" link add name veth1 type veth peer name veth2
ip -net "$ns1" link set netns "$ns2" dev veth2

ip -net "$ns1" link set up dev lo
ip -net "$ns2" link set up dev lo
ip -net "$ns1" link set up dev veth1
ip -net "$ns2" link set up dev veth2

ip -net "$ns2" addr add 10.11.11.2/24 dev veth2
ip -net "$ns2" route add default via 10.11.11.1

ip netns exec "$ns2" sysctl -q net.ipv4.conf.veth2.forwarding=1

# add a rule inside NS so we enable conntrack
ip netns exec "$ns1" nft -f - <<EOF
table inet filter {
	chain input {
		type filter hook input priority 0; policy accept;
		ct state established accept
	}
}
EOF

ip -net "$ns1" addr add 10.11.11.1/24 dev veth1
ip -net "$ns1" route add 10.99.99.99 via 10.11.11.2

# Check connectivity works
ip netns exec "$ns1" ping -q -c 2 10.11.11.2 >/dev/null || exit 1

ip netns exec "$ns2" socat -u -4 TCP-LISTEN:8080,reuseaddr STDOUT &

ip netns exec "$ns2" nft -f - <<EOF
table inet filter {
	counter connreq { }
	counter redir { }
	chain input {
		type filter hook input priority 0; policy accept;
		ct state new tcp flags syn ip daddr 10.99.99.99 tcp dport 80 counter name "connreq" accept
		ct state new ct status dnat tcp dport 8080 counter name "redir" accept
	}
}
EOF
if [ $? -ne 0 ]; then
	echo "ERROR: Could not load nft rules"
	exit 1
fi

ip netns exec "$ns2" sysctl -q net.netfilter.nf_conntrack_tcp_timeout_syn_sent=10

echo "INFO: connect $ns1 -> $ns2 to the virtual ip"
ip netns exec "$ns1" bash -c 'for i in $(seq 1 $BUSYWAIT_TIMEOUT) ; do
	socat -u STDIN TCP:10.99.99.99:80 < /dev/null
	sleep 0.1
	done' &

wait_for_attempt()
{
	count=$(ip netns exec "$ns2" conntrack -L -p tcp --dport 80 2>/dev/null | wc -l)
	if [ "$count" -gt 0 ]; then
		return 0
	fi

	return 1
}

# wait for conntrack to pick the new connection request up before loading
# the nat redirect rule.
if ! busywait "$BUSYWAIT_TIMEOUT" wait_for_attempt; then
	echo "ERROR: $ns2 did not pick up tcp connection from peer"
	exit 1
fi

ip netns exec "$ns2" nft -f - <<EOF
table inet nat {
	chain prerouting {
		type nat hook prerouting priority 0; policy accept;
		ip daddr 10.99.99.99 tcp dport 80 redirect to :8080
	}
}
EOF
if [ $? -ne 0 ]; then
	echo "ERROR: Could not load nat redirect"
	exit 1
fi

wait_for_redirect()
{
	count=$(ip netns exec "$ns2" conntrack -L -p tcp --reply-port-src 8080 2>/dev/null | wc -l)
	if [ "$count" -gt 0 ]; then
		return 0
	fi

	return 1
}
echo "INFO: NAT redirect added in ns $ns2, waiting for $BUSYWAIT_TIMEOUT ms for nat to take effect"

busywait "$BUSYWAIT_TIMEOUT" wait_for_redirect
ret=$?

expect="packets 1 bytes 60"
if ! check_counter "$ns2" "redir" "$expect"; then
	ret=1
fi

if [ $ret -eq 0 ];then
	echo "PASS: redirection counter has expected values"
else
	echo "ERROR: no tcp connection was redirected"
fi

exit $ret
