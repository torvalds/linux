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

source lib.sh

ret=0
SOCAT_TIMEOUT=60

nsin=""
nsin_small=""
ns1out=""
ns2out=""

log_netns=$(sysctl -n net.netfilter.nf_log_all_netns)

checktool "nft --version" "run test without nft tool"
checktool "socat -h" "run test without socat"

setup_ns ns1 ns2 nsr1 nsr2

cleanup() {
	ip netns pids "$ns1" | xargs kill 2>/dev/null
	ip netns pids "$ns2" | xargs kill 2>/dev/null

	cleanup_all_ns

	rm -f "$nsin" "$nsin_small" "$ns1out" "$ns2out"

	[ "$log_netns" -eq 0 ] && sysctl -q net.netfilter.nf_log_all_netns="$log_netns"
}

trap cleanup EXIT

sysctl -q net.netfilter.nf_log_all_netns=1

ip link add veth0 netns "$nsr1" type veth peer name eth0 netns "$ns1"
ip link add veth1 netns "$nsr1" type veth peer name veth0 netns "$nsr2"

ip link add veth1 netns "$nsr2" type veth peer name eth0 netns "$ns2"

for dev in veth0 veth1; do
    ip -net "$nsr1" link set "$dev" up
    ip -net "$nsr2" link set "$dev" up
done

ip -net "$nsr1" addr add 10.0.1.1/24 dev veth0
ip -net "$nsr1" addr add dead:1::1/64 dev veth0 nodad

ip -net "$nsr2" addr add 10.0.2.1/24 dev veth1
ip -net "$nsr2" addr add dead:2::1/64 dev veth1 nodad

# set different MTUs so we need to push packets coming from ns1 (large MTU)
# to ns2 (smaller MTU) to stack either to perform fragmentation (ip_no_pmtu_disc=1),
# or to do PTMU discovery (send ICMP error back to originator).
# ns2 is going via nsr2 with a smaller mtu, so that TCPMSS announced by both peers
# is NOT the lowest link mtu.

omtu=9000
lmtu=1500
rmtu=2000

filesize=$((2 * 1024 * 1024))
filesize_small=$((filesize / 16))

usage(){
	echo "nft_flowtable.sh [OPTIONS]"
	echo
	echo "MTU options"
	echo "   -o originator"
	echo "   -l link"
	echo "   -r responder"
	exit 1
}

while getopts "o:l:r:s:" o
do
	case $o in
		o) omtu=$OPTARG;;
		l) lmtu=$OPTARG;;
		r) rmtu=$OPTARG;;
		s)
			filesize=$OPTARG
			filesize_small=$((OPTARG / 16))
		;;
		*) usage;;
	esac
done

if ! ip -net "$nsr1" link set veth0 mtu "$omtu"; then
	exit 1
fi

ip -net "$ns1" link set eth0 mtu "$omtu"

if ! ip -net "$nsr2" link set veth1 mtu "$rmtu"; then
	exit 1
fi

if ! ip -net "$nsr1" link set veth1 mtu "$lmtu"; then
	exit 1
fi

if ! ip -net "$nsr2" link set veth0 mtu "$lmtu"; then
	exit 1
fi

ip -net "$ns2" link set eth0 mtu "$rmtu"

# transfer-net between nsr1 and nsr2.
# these addresses are not used for connections.
ip -net "$nsr1" addr add 192.168.10.1/24 dev veth1
ip -net "$nsr1" addr add fee1:2::1/64 dev veth1 nodad

ip -net "$nsr2" addr add 192.168.10.2/24 dev veth0
ip -net "$nsr2" addr add fee1:2::2/64 dev veth0 nodad

for i in 0 1; do
  ip netns exec "$nsr1" sysctl net.ipv4.conf.veth$i.forwarding=1 > /dev/null
  ip netns exec "$nsr2" sysctl net.ipv4.conf.veth$i.forwarding=1 > /dev/null
done

for ns in "$ns1" "$ns2";do
  ip -net "$ns" link set eth0 up

  if ! ip netns exec "$ns" sysctl net.ipv4.tcp_no_metrics_save=1 > /dev/null; then
	echo "ERROR: Check Originator/Responder values (problem during address addition)"
	exit 1
  fi
  # don't set ip DF bit for first two tests
  ip netns exec "$ns" sysctl net.ipv4.ip_no_pmtu_disc=1 > /dev/null
done

ip -net "$ns1" addr add 10.0.1.99/24 dev eth0
ip -net "$ns2" addr add 10.0.2.99/24 dev eth0
ip -net "$ns1" route add default via 10.0.1.1
ip -net "$ns2" route add default via 10.0.2.1
ip -net "$ns1" addr add dead:1::99/64 dev eth0 nodad
ip -net "$ns2" addr add dead:2::99/64 dev eth0 nodad
ip -net "$ns1" route add default via dead:1::1
ip -net "$ns2" route add default via dead:2::1

ip -net "$nsr1" route add default via 192.168.10.2
ip -net "$nsr2" route add default via 192.168.10.1

ip netns exec "$nsr1" nft -f - <<EOF
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

ip netns exec "$ns2" nft -f - <<EOF
table inet filter {
   counter ip4dscp0 { }
   counter ip4dscp3 { }

   chain input {
      type filter hook input priority 0; policy accept;
      meta l4proto tcp goto {
	      ip dscp cs3 counter name ip4dscp3 accept
	      ip dscp 0 counter name ip4dscp0 accept
      }
   }
}
EOF

if [ $? -ne 0 ]; then
	echo -n "SKIP: Could not load ruleset: "
	nft --version
	exit $ksft_skip
fi

# test basic connectivity
if ! ip netns exec "$ns1" ping -c 1 -q 10.0.2.99 > /dev/null; then
  echo "ERROR: $ns1 cannot reach ns2" 1>&2
  exit 1
fi

if ! ip netns exec "$ns2" ping -c 1 -q 10.0.1.99 > /dev/null; then
  echo "ERROR: $ns2 cannot reach $ns1" 1>&2
  exit 1
fi

nsin=$(mktemp)
nsin_small=$(mktemp)
ns1out=$(mktemp)
ns2out=$(mktemp)

make_file()
{
	name="$1"
	sz="$2"

	head -c "$sz" < /dev/urandom > "$name"
}

check_counters()
{
	local what=$1
	local ok=1

	local orig repl
	orig=$(ip netns exec "$nsr1" nft reset counter inet filter routed_orig | grep packets)
	repl=$(ip netns exec "$nsr1" nft reset counter inet filter routed_repl | grep packets)

	local orig_cnt=${orig#*bytes}
	local repl_cnt=${repl#*bytes}

	local fs
	fs=$(du -sb "$nsin")
	local max_orig=${fs%%/*}
	local max_repl=$((max_orig))

	# flowtable fastpath should bypass normal routing one, i.e. the counters in forward hook
	# should always be lower than the size of the transmitted file (max_orig).
	if [ "$orig_cnt" -gt "$max_orig" ];then
		echo "FAIL: $what: original counter $orig_cnt exceeds expected value $max_orig, reply counter $repl_cnt" 1>&2
		ret=1
		ok=0
	fi

	if [ "$repl_cnt" -gt $max_repl ];then
		echo "FAIL: $what: reply counter $repl_cnt exceeds expected value $max_repl, original counter $orig_cnt" 1>&2
		ret=1
		ok=0
	fi

	if [ $ok -eq 1 ]; then
		echo "PASS: $what"
	fi
}

check_dscp()
{
	local what=$1
	local pmtud="$2"
	local ok=1

	local counter
	counter=$(ip netns exec "$ns2" nft reset counter inet filter ip4dscp3 | grep packets)

	local pc4=${counter%*bytes*}
	local pc4=${pc4#*packets}

	counter=$(ip netns exec "$ns2" nft reset counter inet filter ip4dscp0 | grep packets)
	local pc4z=${counter%*bytes*}
	local pc4z=${pc4z#*packets}

	local failmsg="FAIL: pmtu $pmtu: $what counters do not match, expected"

	case "$what" in
	"dscp_none")
		if [ "$pc4" -gt 0 ] || [ "$pc4z" -eq 0 ]; then
			echo "$failmsg dscp3 == 0, dscp0 > 0, but got $pc4,$pc4z" 1>&2
			ret=1
			ok=0
		fi
		;;
	"dscp_fwd")
		if [ "$pc4" -eq 0 ] || [ "$pc4z" -eq 0 ]; then
			echo "$failmsg dscp3 and dscp0 > 0 but got $pc4,$pc4z" 1>&2
			ret=1
			ok=0
		fi
		;;
	"dscp_ingress")
		if [ "$pc4" -eq 0 ] || [ "$pc4z" -gt 0 ]; then
			echo "$failmsg dscp3 > 0, dscp0 == 0 but got $pc4,$pc4z" 1>&2
			ret=1
			ok=0
		fi
		;;
	"dscp_egress")
		if [ "$pc4" -eq 0 ] || [ "$pc4z" -gt 0 ]; then
			echo "$failmsg dscp3 > 0, dscp0 == 0 but got $pc4,$pc4z" 1>&2
			ret=1
			ok=0
		fi
		;;
	*)
		echo "$failmsg: Unknown DSCP check" 1>&2
		ret=1
		ok=0
	esac

	if [ "$ok" -eq 1 ] ;then
		echo "PASS: $what: dscp packet counters match"
	fi
}

check_transfer()
{
	local in=$1
	local out=$2
	local what=$3

	if ! cmp "$in" "$out" > /dev/null 2>&1; then
		echo "FAIL: file mismatch for $what" 1>&2
		ls -l "$in"
		ls -l "$out"
		return 1
	fi

	return 0
}

listener_ready()
{
	ss -N "$nsb" -lnt -o "sport = :12345" | grep -q 12345
}

test_tcp_forwarding_ip()
{
	local nsa=$1
	local nsb=$2
	local pmtu=$3
	local dstip=$4
	local dstport=$5
	local lret=0
	local socatc
	local socatl
	local infile="$nsin"

	if [ $pmtu -eq 0 ]; then
		infile="$nsin_small"
	fi

	timeout "$SOCAT_TIMEOUT" ip netns exec "$nsb" socat -4 TCP-LISTEN:12345,reuseaddr STDIO < "$infile" > "$ns2out" &
	lpid=$!

	busywait 1000 listener_ready

	timeout "$SOCAT_TIMEOUT" ip netns exec "$nsa" socat -4 TCP:"$dstip":"$dstport" STDIO < "$infile" > "$ns1out"
	socatc=$?

	wait $lpid
	socatl=$?

	if [ $socatl -ne 0 ] || [ $socatc -ne 0 ];then
		rc=1
	fi

	if ! check_transfer "$infile" "$ns2out" "ns1 -> ns2"; then
		lret=1
		ret=1
	fi

	if ! check_transfer "$infile" "$ns1out" "ns1 <- ns2"; then
		lret=1
		ret=1
	fi

	return $lret
}

test_tcp_forwarding()
{
	local pmtu="$3"

	test_tcp_forwarding_ip "$1" "$2" "$pmtu" 10.0.2.99 12345

	return $?
}

test_tcp_forwarding_set_dscp()
{
	local pmtu="$3"

ip netns exec "$nsr1" nft -f - <<EOF
table netdev dscpmangle {
   chain setdscp0 {
      type filter hook ingress device "veth0" priority 0; policy accept
	ip dscp set cs3
  }
}
EOF
if [ $? -eq 0 ]; then
	test_tcp_forwarding_ip "$1" "$2" "$3" 10.0.2.99 12345
	check_dscp "dscp_ingress" "$pmtu"

	ip netns exec "$nsr1" nft delete table netdev dscpmangle
else
	echo "SKIP: Could not load netdev:ingress for veth0"
fi

ip netns exec "$nsr1" nft -f - <<EOF
table netdev dscpmangle {
   chain setdscp0 {
      type filter hook egress device "veth1" priority 0; policy accept
      ip dscp set cs3
  }
}
EOF
if [ $? -eq 0 ]; then
	test_tcp_forwarding_ip "$1" "$2" "$pmtu"  10.0.2.99 12345
	check_dscp "dscp_egress" "$pmtu"

	ip netns exec "$nsr1" nft delete table netdev dscpmangle
else
	echo "SKIP: Could not load netdev:egress for veth1"
fi

	# partial.  If flowtable really works, then both dscp-is-0 and dscp-is-cs3
	# counters should have seen packets (before and after ft offload kicks in).
	ip netns exec "$nsr1" nft -a insert rule inet filter forward ip dscp set cs3
	test_tcp_forwarding_ip "$1" "$2" "$pmtu"  10.0.2.99 12345
	check_dscp "dscp_fwd" "$pmtu"
}

test_tcp_forwarding_nat()
{
	local nsa="$1"
	local nsb="$2"
	local pmtu="$3"
	local what="$4"
	local lret

	[ "$pmtu" -eq 0 ] && what="$what (pmtu disabled)"

	test_tcp_forwarding_ip "$nsa" "$nsb" "$pmtu" 10.0.2.99 12345
	lret=$?

	if [ "$lret" -eq 0 ] ; then
		if [ "$pmtu" -eq 1 ] ;then
			check_counters "flow offload for ns1/ns2 with masquerade $what"
		else
			echo "PASS: flow offload for ns1/ns2 with masquerade $what"
		fi

		test_tcp_forwarding_ip "$1" "$2" "$pmtu" 10.6.6.6 1666
		lret=$?
		if [ "$pmtu" -eq 1 ] ;then
			check_counters "flow offload for ns1/ns2 with dnat $what"
		elif [ "$lret" -eq 0 ] ; then
			echo "PASS: flow offload for ns1/ns2 with dnat $what"
		fi
	else
		echo "FAIL: flow offload for ns1/ns2 with dnat $what"
	fi

	return $lret
}

make_file "$nsin" "$filesize"
make_file "$nsin_small" "$filesize_small"

# First test:
# No PMTU discovery, nsr1 is expected to fragment packets from ns1 to ns2 as needed.
# Due to MTU mismatch in both directions, all packets (except small packets like pure
# acks) have to be handled by normal forwarding path.  Therefore, packet counters
# are not checked.
if test_tcp_forwarding "$ns1" "$ns2" 0; then
	echo "PASS: flow offloaded for ns1/ns2"
else
	echo "FAIL: flow offload for ns1/ns2:" 1>&2
	ip netns exec "$nsr1" nft list ruleset
	ret=1
fi

# delete default route, i.e. ns2 won't be able to reach ns1 and
# will depend on ns1 being masqueraded in nsr1.
# expect ns1 has nsr1 address.
ip -net "$ns2" route del default via 10.0.2.1
ip -net "$ns2" route del default via dead:2::1
ip -net "$ns2" route add 192.168.10.1 via 10.0.2.1

# Second test:
# Same, but with NAT enabled.  Same as in first test: we expect normal forward path
# to handle most packets.
ip netns exec "$nsr1" nft -f - <<EOF
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

check_dscp "dscp_none" "0"
if ! test_tcp_forwarding_set_dscp "$ns1" "$ns2" 0 ""; then
	echo "FAIL: flow offload for ns1/ns2 with dscp update and no pmtu discovery" 1>&2
	exit 0
fi

if ! test_tcp_forwarding_nat "$ns1" "$ns2" 0 ""; then
	echo "FAIL: flow offload for ns1/ns2 with NAT" 1>&2
	ip netns exec "$nsr1" nft list ruleset
	ret=1
fi

# Third test:
# Same as second test, but with PMTU discovery enabled. This
# means that we expect the fastpath to handle packets as soon
# as the endpoints adjust the packet size.
ip netns exec "$ns1" sysctl net.ipv4.ip_no_pmtu_disc=0 > /dev/null
ip netns exec "$ns2" sysctl net.ipv4.ip_no_pmtu_disc=0 > /dev/null

# reset counters.
# With pmtu in-place we'll also check that nft counters
# are lower than file size and packets were forwarded via flowtable layer.
# For earlier tests (large mtus), packets cannot be handled via flowtable
# (except pure acks and other small packets).
ip netns exec "$nsr1" nft reset counters table inet filter >/dev/null
ip netns exec "$ns2"  nft reset counters table inet filter >/dev/null

if ! test_tcp_forwarding_set_dscp "$ns1" "$ns2" 1 ""; then
	echo "FAIL: flow offload for ns1/ns2 with dscp update and pmtu discovery" 1>&2
	exit 0
fi

ip netns exec "$nsr1" nft reset counters table inet filter >/dev/null

if ! test_tcp_forwarding_nat "$ns1" "$ns2" 1 ""; then
	echo "FAIL: flow offload for ns1/ns2 with NAT and pmtu discovery" 1>&2
	ip netns exec "$nsr1" nft list ruleset
fi

# Another test:
# Add bridge interface br0 to Router1, with NAT enabled.
test_bridge() {
if ! ip -net "$nsr1" link add name br0 type bridge 2>/dev/null;then
	echo "SKIP: could not add bridge br0"
	[ "$ret" -eq 0 ] && ret=$ksft_skip
	return
fi
ip -net "$nsr1" addr flush dev veth0
ip -net "$nsr1" link set up dev veth0
ip -net "$nsr1" link set veth0 master br0
ip -net "$nsr1" addr add 10.0.1.1/24 dev br0
ip -net "$nsr1" addr add dead:1::1/64 dev br0 nodad
ip -net "$nsr1" link set up dev br0

ip netns exec "$nsr1" sysctl net.ipv4.conf.br0.forwarding=1 > /dev/null

# br0 with NAT enabled.
ip netns exec "$nsr1" nft -f - <<EOF
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

if ! test_tcp_forwarding_nat "$ns1" "$ns2" 1 "on bridge"; then
	echo "FAIL: flow offload for ns1/ns2 with bridge NAT" 1>&2
	ip netns exec "$nsr1" nft list ruleset
	ret=1
fi


# Another test:
# Add bridge interface br0 to Router1, with NAT and VLAN.
ip -net "$nsr1" link set veth0 nomaster
ip -net "$nsr1" link set down dev veth0
ip -net "$nsr1" link add link veth0 name veth0.10 type vlan id 10
ip -net "$nsr1" link set up dev veth0
ip -net "$nsr1" link set up dev veth0.10
ip -net "$nsr1" link set veth0.10 master br0

ip -net "$ns1" addr flush dev eth0
ip -net "$ns1" link add link eth0 name eth0.10 type vlan id 10
ip -net "$ns1" link set eth0 up
ip -net "$ns1" link set eth0.10 up
ip -net "$ns1" addr add 10.0.1.99/24 dev eth0.10
ip -net "$ns1" route add default via 10.0.1.1
ip -net "$ns1" addr add dead:1::99/64 dev eth0.10 nodad

if ! test_tcp_forwarding_nat "$ns1" "$ns2" 1 "bridge and VLAN"; then
	echo "FAIL: flow offload for ns1/ns2 with bridge NAT and VLAN" 1>&2
	ip netns exec "$nsr1" nft list ruleset
	ret=1
fi

# restore test topology (remove bridge and VLAN)
ip -net "$nsr1" link set veth0 nomaster
ip -net "$nsr1" link set veth0 down
ip -net "$nsr1" link set veth0.10 down
ip -net "$nsr1" link delete veth0.10 type vlan
ip -net "$nsr1" link delete br0 type bridge
ip -net "$ns1" addr flush dev eth0.10
ip -net "$ns1" link set eth0.10 down
ip -net "$ns1" link set eth0 down
ip -net "$ns1" link delete eth0.10 type vlan

# restore address in ns1 and nsr1
ip -net "$ns1" link set eth0 up
ip -net "$ns1" addr add 10.0.1.99/24 dev eth0
ip -net "$ns1" route add default via 10.0.1.1
ip -net "$ns1" addr add dead:1::99/64 dev eth0 nodad
ip -net "$ns1" route add default via dead:1::1
ip -net "$nsr1" addr add 10.0.1.1/24 dev veth0
ip -net "$nsr1" addr add dead:1::1/64 dev veth0 nodad
ip -net "$nsr1" link set up dev veth0
}

test_bridge

KEY_SHA="0x"$(ps -af | sha1sum | cut -d " " -f 1)
KEY_AES="0x"$(ps -af | md5sum | cut -d " " -f 1)
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

    ip -net "$ns" xfrm state add src "$remote" dst "$me" proto esp spi "$spi_in"  enc aes "$KEY_AES"  auth sha1 "$KEY_SHA" mode tunnel sel src "$rnet" dst "$lnet"
    ip -net "$ns" xfrm state add src "$me"  dst "$remote" proto esp spi "$spi_out" enc aes "$KEY_AES" auth sha1 "$KEY_SHA" mode tunnel sel src "$lnet" dst "$rnet"

    # to encrypt packets as they go out (includes forwarded packets that need encapsulation)
    ip -net "$ns" xfrm policy add src "$lnet" dst "$rnet" dir out tmpl src "$me" dst "$remote" proto esp mode tunnel priority 1 action allow
    # to fwd decrypted packets after esp processing:
    ip -net "$ns" xfrm policy add src "$rnet" dst "$lnet" dir fwd tmpl src "$remote" dst "$me" proto esp mode tunnel priority 1 action allow
}

do_esp "$nsr1" 192.168.10.1 192.168.10.2 10.0.1.0/24 10.0.2.0/24 "$SPI1" "$SPI2"

do_esp "$nsr2" 192.168.10.2 192.168.10.1 10.0.2.0/24 10.0.1.0/24 "$SPI2" "$SPI1"

ip netns exec "$nsr1" nft delete table ip nat

# restore default routes
ip -net "$ns2" route del 192.168.10.1 via 10.0.2.1
ip -net "$ns2" route add default via 10.0.2.1
ip -net "$ns2" route add default via dead:2::1

if test_tcp_forwarding "$ns1" "$ns2" 1; then
	check_counters "ipsec tunnel mode for ns1/ns2"
else
	echo "FAIL: ipsec tunnel mode for ns1/ns2"
	ip netns exec "$nsr1" nft list ruleset 1>&2
	ip netns exec "$nsr1" cat /proc/net/xfrm_stat 1>&2
fi

if [ "$1" = "" ]; then
	low=1280
	mtu=$((65536 - low))
	o=$(((RANDOM%mtu) + low))
	l=$(((RANDOM%mtu) + low))
	r=$(((RANDOM%mtu) + low))

	MINSIZE=$((2 *  1000 * 1000))
	MAXSIZE=$((64 * 1000 * 1000))

	filesize=$(((RANDOM * RANDOM) % MAXSIZE))
	if [ "$filesize" -lt "$MINSIZE" ]; then
		filesize=$((filesize+MINSIZE))
	fi

	echo "re-run with random mtus and file size: -o $o -l $l -r $r -s $filesize"
	$0 -o "$o" -l "$l" -r "$r" -s "$filesize" || ret=1
fi

exit $ret
