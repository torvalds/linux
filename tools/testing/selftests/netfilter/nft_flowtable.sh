#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# This tests basic flowtable functionality.
# Creates following default topology:
#
# Originator (MTU 9000) <-Router1-> MTU 1500 <-Router2-> Responder (MTU 2000)
# Router1 is the one doing flow offloading, Router2 has no special
# purpose other than having a link that is smaller than either Originator
# and responder, i.e. TCPMSS announced values are too large and will still
# result in fragmentation and/or PMTU discovery.
#
# You can check with different Orgininator/Link/Responder MTU eg:
# nft_flowtable.sh -o8000 -l1500 -r2000
#

sfx=$(mktemp -u "XXXXXXXX")
ns1="ns1-$sfx"
ns2="ns2-$sfx"
nsr1="nsr1-$sfx"
nsr2="nsr2-$sfx"

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4
ret=0

nsin=""
ns1out=""
ns2out=""

log_netns=$(sysctl -n net.netfilter.nf_log_all_netns)

checktool (){
	if ! $1 > /dev/null 2>&1; then
		echo "SKIP: Could not $2"
		exit $ksft_skip
	fi
}

checktool "nft --version" "run test without nft tool"
checktool "ip -Version" "run test without ip tool"
checktool "which nc" "run test without nc (netcat)"
checktool "ip netns add $nsr1" "create net namespace $nsr1"

ip netns add $ns1
ip netns add $ns2
ip netns add $nsr2

cleanup() {
	ip netns del $ns1
	ip netns del $ns2
	ip netns del $nsr1
	ip netns del $nsr2

	rm -f "$nsin" "$ns1out" "$ns2out"

	[ $log_netns -eq 0 ] && sysctl -q net.netfilter.nf_log_all_netns=$log_netns
}

trap cleanup EXIT

sysctl -q net.netfilter.nf_log_all_netns=1

ip link add veth0 netns $nsr1 type veth peer name eth0 netns $ns1
ip link add veth1 netns $nsr1 type veth peer name veth0 netns $nsr2

ip link add veth1 netns $nsr2 type veth peer name eth0 netns $ns2

for dev in lo veth0 veth1; do
    ip -net $nsr1 link set $dev up
    ip -net $nsr2 link set $dev up
done

ip -net $nsr1 addr add 10.0.1.1/24 dev veth0
ip -net $nsr1 addr add dead:1::1/64 dev veth0

ip -net $nsr2 addr add 10.0.2.1/24 dev veth1
ip -net $nsr2 addr add dead:2::1/64 dev veth1

# set different MTUs so we need to push packets coming from ns1 (large MTU)
# to ns2 (smaller MTU) to stack either to perform fragmentation (ip_no_pmtu_disc=1),
# or to do PTMU discovery (send ICMP error back to originator).
# ns2 is going via nsr2 with a smaller mtu, so that TCPMSS announced by both peers
# is NOT the lowest link mtu.

omtu=9000
lmtu=1500
rmtu=2000

usage(){
	echo "nft_flowtable.sh [OPTIONS]"
	echo
	echo "MTU options"
	echo "   -o originator"
	echo "   -l link"
	echo "   -r responder"
	exit 1
}

while getopts "o:l:r:" o
do
	case $o in
		o) omtu=$OPTARG;;
		l) lmtu=$OPTARG;;
		r) rmtu=$OPTARG;;
		*) usage;;
	esac
done

if ! ip -net $nsr1 link set veth0 mtu $omtu; then
	exit 1
fi

ip -net $ns1 link set eth0 mtu $omtu

if ! ip -net $nsr2 link set veth1 mtu $rmtu; then
	exit 1
fi

ip -net $ns2 link set eth0 mtu $rmtu

# transfer-net between nsr1 and nsr2.
# these addresses are not used for connections.
ip -net $nsr1 addr add 192.168.10.1/24 dev veth1
ip -net $nsr1 addr add fee1:2::1/64 dev veth1

ip -net $nsr2 addr add 192.168.10.2/24 dev veth0
ip -net $nsr2 addr add fee1:2::2/64 dev veth0

for i in 0 1; do
  ip netns exec $nsr1 sysctl net.ipv4.conf.veth$i.forwarding=1 > /dev/null
  ip netns exec $nsr2 sysctl net.ipv4.conf.veth$i.forwarding=1 > /dev/null
done

for ns in $ns1 $ns2;do
  ip -net $ns link set lo up
  ip -net $ns link set eth0 up

  if ! ip netns exec $ns sysctl net.ipv4.tcp_no_metrics_save=1 > /dev/null; then
	echo "ERROR: Check Originator/Responder values (problem during address addition)"
	exit 1
  fi
  # don't set ip DF bit for first two tests
  ip netns exec $ns sysctl net.ipv4.ip_no_pmtu_disc=1 > /dev/null
done

ip -net $ns1 addr add 10.0.1.99/24 dev eth0
ip -net $ns2 addr add 10.0.2.99/24 dev eth0
ip -net $ns1 route add default via 10.0.1.1
ip -net $ns2 route add default via 10.0.2.1
ip -net $ns1 addr add dead:1::99/64 dev eth0
ip -net $ns2 addr add dead:2::99/64 dev eth0
ip -net $ns1 route add default via dead:1::1
ip -net $ns2 route add default via dead:2::1

ip -net $nsr1 route add default via 192.168.10.2
ip -net $nsr2 route add default via 192.168.10.1

ip netns exec $nsr1 nft -f - <<EOF
table inet filter {
  flowtable f1 {
     hook ingress priority 0
     devices = { veth0, veth1 }
   }

   counter routed_orig { }
   counter routed_repl { }

   chain forward {
      type filter hook forward priority 0; policy drop;

      # flow offloaded? Tag ct with mark 1, so we can detect when it fails.
      meta oif "veth1" tcp dport 12345 ct mark set 1 flow add @f1 counter name routed_orig accept

      # count packets supposedly offloaded as per direction.
      ct mark 1 counter name ct direction map { original : routed_orig, reply : routed_repl } accept

      ct state established,related accept

      meta nfproto ipv4 meta l4proto icmp accept
      meta nfproto ipv6 meta l4proto icmpv6 accept
   }
}
EOF

if [ $? -ne 0 ]; then
	echo "SKIP: Could not load nft ruleset"
	exit $ksft_skip
fi

# test basic connectivity
if ! ip netns exec $ns1 ping -c 1 -q 10.0.2.99 > /dev/null; then
  echo "ERROR: $ns1 cannot reach ns2" 1>&2
  exit 1
fi

if ! ip netns exec $ns2 ping -c 1 -q 10.0.1.99 > /dev/null; then
  echo "ERROR: $ns2 cannot reach $ns1" 1>&2
  exit 1
fi

if [ $ret -eq 0 ];then
	echo "PASS: netns routing/connectivity: $ns1 can reach $ns2"
fi

nsin=$(mktemp)
ns1out=$(mktemp)
ns2out=$(mktemp)

make_file()
{
	name=$1

	SIZE=$((RANDOM % (1024 * 128)))
	SIZE=$((SIZE + (1024 * 8)))
	TSIZE=$((SIZE * 1024))

	dd if=/dev/urandom of="$name" bs=1024 count=$SIZE 2> /dev/null

	SIZE=$((RANDOM % 1024))
	SIZE=$((SIZE + 128))
	TSIZE=$((TSIZE + SIZE))
	dd if=/dev/urandom conf=notrunc of="$name" bs=1 count=$SIZE 2> /dev/null
}

check_counters()
{
	local what=$1
	local ok=1

	local orig=$(ip netns exec $nsr1 nft reset counter inet filter routed_orig | grep packets)
	local repl=$(ip netns exec $nsr1 nft reset counter inet filter routed_repl | grep packets)

	local orig_cnt=${orig#*bytes}
	local repl_cnt=${repl#*bytes}

	local fs=$(du -sb $nsin)
	local max_orig=${fs%%/*}
	local max_repl=$((max_orig/4))

	if [ $orig_cnt -gt $max_orig ];then
		echo "FAIL: $what: original counter $orig_cnt exceeds expected value $max_orig" 1>&2
		ret=1
		ok=0
	fi

	if [ $repl_cnt -gt $max_repl ];then
		echo "FAIL: $what: reply counter $repl_cnt exceeds expected value $max_repl" 1>&2
		ret=1
		ok=0
	fi

	if [ $ok -eq 1 ]; then
		echo "PASS: $what"
	fi
}

check_transfer()
{
	in=$1
	out=$2
	what=$3

	if ! cmp "$in" "$out" > /dev/null 2>&1; then
		echo "FAIL: file mismatch for $what" 1>&2
		ls -l "$in"
		ls -l "$out"
		return 1
	fi

	return 0
}

test_tcp_forwarding_ip()
{
	local nsa=$1
	local nsb=$2
	local dstip=$3
	local dstport=$4
	local lret=0

	ip netns exec $nsb nc -w 5 -l -p 12345 < "$nsin" > "$ns2out" &
	lpid=$!

	sleep 1
	ip netns exec $nsa nc -w 4 "$dstip" "$dstport" < "$nsin" > "$ns1out" &
	cpid=$!

	sleep 3

	if ps -p $lpid > /dev/null;then
		kill $lpid
	fi

	if ps -p $cpid > /dev/null;then
		kill $cpid
	fi

	wait

	if ! check_transfer "$nsin" "$ns2out" "ns1 -> ns2"; then
		lret=1
	fi

	if ! check_transfer "$nsin" "$ns1out" "ns1 <- ns2"; then
		lret=1
	fi

	return $lret
}

test_tcp_forwarding()
{
	test_tcp_forwarding_ip "$1" "$2" 10.0.2.99 12345

	return $?
}

test_tcp_forwarding_nat()
{
	local lret
	local pmtu

	test_tcp_forwarding_ip "$1" "$2" 10.0.2.99 12345
	lret=$?

	pmtu=$3
	what=$4

	if [ $lret -eq 0 ] ; then
		if [ $pmtu -eq 1 ] ;then
			check_counters "flow offload for ns1/ns2 with masquerade and pmtu discovery $what"
		else
			echo "PASS: flow offload for ns1/ns2 with masquerade $what"
		fi

		test_tcp_forwarding_ip "$1" "$2" 10.6.6.6 1666
		lret=$?
		if [ $pmtu -eq 1 ] ;then
			check_counters "flow offload for ns1/ns2 with dnat and pmtu discovery $what"
		elif [ $lret -eq 0 ] ; then
			echo "PASS: flow offload for ns1/ns2 with dnat $what"
		fi
	fi

	return $lret
}

make_file "$nsin"

# First test:
# No PMTU discovery, nsr1 is expected to fragment packets from ns1 to ns2 as needed.
# Due to MTU mismatch in both directions, all packets (except small packets like pure
# acks) have to be handled by normal forwarding path.  Therefore, packet counters
# are not checked.
if test_tcp_forwarding $ns1 $ns2; then
	echo "PASS: flow offloaded for ns1/ns2"
else
	echo "FAIL: flow offload for ns1/ns2:" 1>&2
	ip netns exec $nsr1 nft list ruleset
	ret=1
fi

# delete default route, i.e. ns2 won't be able to reach ns1 and
# will depend on ns1 being masqueraded in nsr1.
# expect ns1 has nsr1 address.
ip -net $ns2 route del default via 10.0.2.1
ip -net $ns2 route del default via dead:2::1
ip -net $ns2 route add 192.168.10.1 via 10.0.2.1

# Second test:
# Same, but with NAT enabled.  Same as in first test: we expect normal forward path
# to handle most packets.
ip netns exec $nsr1 nft -f - <<EOF
table ip nat {
   chain prerouting {
      type nat hook prerouting priority 0; policy accept;
      meta iif "veth0" ip daddr 10.6.6.6 tcp dport 1666 counter dnat ip to 10.0.2.99:12345
   }

   chain postrouting {
      type nat hook postrouting priority 0; policy accept;
      meta oifname "veth1" counter masquerade
   }
}
EOF

if ! test_tcp_forwarding_nat $ns1 $ns2 0 ""; then
	echo "FAIL: flow offload for ns1/ns2 with NAT" 1>&2
	ip netns exec $nsr1 nft list ruleset
	ret=1
fi

# Third test:
# Same as second test, but with PMTU discovery enabled. This
# means that we expect the fastpath to handle packets as soon
# as the endpoints adjust the packet size.
ip netns exec $ns1 sysctl net.ipv4.ip_no_pmtu_disc=0 > /dev/null
ip netns exec $ns2 sysctl net.ipv4.ip_no_pmtu_disc=0 > /dev/null

# reset counters.
# With pmtu in-place we'll also check that nft counters
# are lower than file size and packets were forwarded via flowtable layer.
# For earlier tests (large mtus), packets cannot be handled via flowtable
# (except pure acks and other small packets).
ip netns exec $nsr1 nft reset counters table inet filter >/dev/null

if ! test_tcp_forwarding_nat $ns1 $ns2 1 ""; then
	echo "FAIL: flow offload for ns1/ns2 with NAT and pmtu discovery" 1>&2
	ip netns exec $nsr1 nft list ruleset
fi

# Another test:
# Add bridge interface br0 to Router1, with NAT enabled.
ip -net $nsr1 link add name br0 type bridge
ip -net $nsr1 addr flush dev veth0
ip -net $nsr1 link set up dev veth0
ip -net $nsr1 link set veth0 master br0
ip -net $nsr1 addr add 10.0.1.1/24 dev br0
ip -net $nsr1 addr add dead:1::1/64 dev br0
ip -net $nsr1 link set up dev br0

ip netns exec $nsr1 sysctl net.ipv4.conf.br0.forwarding=1 > /dev/null

# br0 with NAT enabled.
ip netns exec $nsr1 nft -f - <<EOF
flush table ip nat
table ip nat {
   chain prerouting {
      type nat hook prerouting priority 0; policy accept;
      meta iif "br0" ip daddr 10.6.6.6 tcp dport 1666 counter dnat ip to 10.0.2.99:12345
   }

   chain postrouting {
      type nat hook postrouting priority 0; policy accept;
      meta oifname "veth1" counter masquerade
   }
}
EOF

if ! test_tcp_forwarding_nat $ns1 $ns2 1 "on bridge"; then
	echo "FAIL: flow offload for ns1/ns2 with bridge NAT" 1>&2
	ip netns exec $nsr1 nft list ruleset
	ret=1
fi


# Another test:
# Add bridge interface br0 to Router1, with NAT and VLAN.
ip -net $nsr1 link set veth0 nomaster
ip -net $nsr1 link set down dev veth0
ip -net $nsr1 link add link veth0 name veth0.10 type vlan id 10
ip -net $nsr1 link set up dev veth0
ip -net $nsr1 link set up dev veth0.10
ip -net $nsr1 link set veth0.10 master br0

ip -net $ns1 addr flush dev eth0
ip -net $ns1 link add link eth0 name eth0.10 type vlan id 10
ip -net $ns1 link set eth0 up
ip -net $ns1 link set eth0.10 up
ip -net $ns1 addr add 10.0.1.99/24 dev eth0.10
ip -net $ns1 route add default via 10.0.1.1
ip -net $ns1 addr add dead:1::99/64 dev eth0.10

if ! test_tcp_forwarding_nat $ns1 $ns2 1 "bridge and VLAN"; then
	echo "FAIL: flow offload for ns1/ns2 with bridge NAT and VLAN" 1>&2
	ip netns exec $nsr1 nft list ruleset
	ret=1
fi

# restore test topology (remove bridge and VLAN)
ip -net $nsr1 link set veth0 nomaster
ip -net $nsr1 link set veth0 down
ip -net $nsr1 link set veth0.10 down
ip -net $nsr1 link delete veth0.10 type vlan
ip -net $nsr1 link delete br0 type bridge
ip -net $ns1 addr flush dev eth0.10
ip -net $ns1 link set eth0.10 down
ip -net $ns1 link set eth0 down
ip -net $ns1 link delete eth0.10 type vlan

# restore address in ns1 and nsr1
ip -net $ns1 link set eth0 up
ip -net $ns1 addr add 10.0.1.99/24 dev eth0
ip -net $ns1 route add default via 10.0.1.1
ip -net $ns1 addr add dead:1::99/64 dev eth0
ip -net $ns1 route add default via dead:1::1
ip -net $nsr1 addr add 10.0.1.1/24 dev veth0
ip -net $nsr1 addr add dead:1::1/64 dev veth0
ip -net $nsr1 link set up dev veth0

KEY_SHA="0x"$(ps -xaf | sha1sum | cut -d " " -f 1)
KEY_AES="0x"$(ps -xaf | md5sum | cut -d " " -f 1)
SPI1=$RANDOM
SPI2=$RANDOM

if [ $SPI1 -eq $SPI2 ]; then
	SPI2=$((SPI2+1))
fi

do_esp() {
    local ns=$1
    local me=$2
    local remote=$3
    local lnet=$4
    local rnet=$5
    local spi_out=$6
    local spi_in=$7

    ip -net $ns xfrm state add src $remote dst $me proto esp spi $spi_in  enc aes $KEY_AES  auth sha1 $KEY_SHA mode tunnel sel src $rnet dst $lnet
    ip -net $ns xfrm state add src $me  dst $remote proto esp spi $spi_out enc aes $KEY_AES auth sha1 $KEY_SHA mode tunnel sel src $lnet dst $rnet

    # to encrypt packets as they go out (includes forwarded packets that need encapsulation)
    ip -net $ns xfrm policy add src $lnet dst $rnet dir out tmpl src $me dst $remote proto esp mode tunnel priority 1 action allow
    # to fwd decrypted packets after esp processing:
    ip -net $ns xfrm policy add src $rnet dst $lnet dir fwd tmpl src $remote dst $me proto esp mode tunnel priority 1 action allow

}

do_esp $nsr1 192.168.10.1 192.168.10.2 10.0.1.0/24 10.0.2.0/24 $SPI1 $SPI2

do_esp $nsr2 192.168.10.2 192.168.10.1 10.0.2.0/24 10.0.1.0/24 $SPI2 $SPI1

ip netns exec $nsr1 nft delete table ip nat

# restore default routes
ip -net $ns2 route del 192.168.10.1 via 10.0.2.1
ip -net $ns2 route add default via 10.0.2.1
ip -net $ns2 route add default via dead:2::1

if test_tcp_forwarding $ns1 $ns2; then
	check_counters "ipsec tunnel mode for ns1/ns2"
else
	echo "FAIL: ipsec tunnel mode for ns1/ns2"
	ip netns exec $nsr1 nft list ruleset 1>&2
	ip netns exec $nsr1 cat /proc/net/xfrm_stat 1>&2
fi

exit $ret
