#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh

ret=0

checktool "nft --version" "run test without nft tool"
checktool "iperf3 --version" "run test without iperf3"

setup_ns nsr ns1 ns2

modprobe -q nf_conntrack

cleanup() {
	ip netns pids "$ns1" | xargs kill 2>/dev/null
	ip netns pids "$ns2" | xargs kill 2>/dev/null

	cleanup_all_ns
}

trap cleanup EXIT

ip link add veth0 netns "$nsr" type veth peer name eth0 netns "$ns1"
ip link add veth1 netns "$nsr" type veth peer name eth0 netns "$ns2"

for dev in veth0 veth1; do
	ip -net "$nsr" link set "$dev" up
done

ip -net "$nsr" addr add 10.0.1.1/24 dev veth0
ip -net "$nsr" addr add 10.0.2.1/24 dev veth1

ip netns exec "$nsr" sysctl -q net.ipv4.conf.veth0.forwarding=1
ip netns exec "$nsr" sysctl -q net.ipv4.conf.veth1.forwarding=1
ip netns exec "$nsr" sysctl -q net.netfilter.nf_conntrack_tcp_loose=0

for n in $ns1 $ns2; do
  ip -net "$n" link set eth0 up
done
ip -net "$ns1" addr add 10.0.1.99/24 dev eth0
ip -net "$ns2" addr add 10.0.2.99/24 dev eth0
ip -net "$ns1" route add default via 10.0.1.1
ip -net "$ns2" route add default via 10.0.2.1

# test basic connectivity
if ! ip netns exec "$ns1" ping -c 1 -q 10.0.2.99 > /dev/null; then
  echo "ERROR: $ns1 cannot reach $ns2" 1>&2
  exit 1
fi

if ! ip netns exec "$ns2" ping -c 1 -q 10.0.1.99 > /dev/null; then
  echo "ERROR: $ns2 cannot reach $ns1" 1>&2
  exit 1
fi

ip netns exec "$ns2" iperf3 -s > /dev/null 2>&1 &
# ip netns exec $nsr tcpdump -vvv -n -i veth1 tcp | head -n 10 &

sleep 1

ip netns exec "$nsr" nft -f - <<EOF
table inet filter {
   chain prerouting {
      type filter hook prerouting priority -300; policy accept;
      meta iif veth0 tcp flags syn counter notrack
   }

  chain forward {
      type filter hook forward priority 0; policy accept;

      ct state new,established counter accept

      meta iif veth0 meta l4proto tcp ct state untracked,invalid synproxy mss 1460 sack-perm timestamp

      ct state invalid counter drop

      # make ns2 unreachable w.o. tcp synproxy
      tcp flags syn counter drop
   }
}
EOF
if [ $? -ne 0 ]; then
	echo "SKIP: Cannot add nft synproxy"
	exit $ksft_skip
fi

if ! ip netns exec "$ns1" timeout 5 iperf3 -c 10.0.2.99 -n $((1 * 1024 * 1024)) > /dev/null; then
	echo "FAIL: iperf3 returned an error" 1>&2
	ret=1
	ip netns exec "$nsr" nft list ruleset
else
	echo "PASS: synproxy connection successful"
fi

exit $ret
