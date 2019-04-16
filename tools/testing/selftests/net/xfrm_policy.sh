#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Check xfrm policy resolution.  Topology:
#
# 1.2   1.1   3.1  3.10    2.1   2.2
# eth1  eth1 veth0 veth0 eth1   eth1
# ns1 ---- ns3 ----- ns4 ---- ns2
#
# ns3 and ns4 are connected via ipsec tunnel.
# pings from ns1 to ns2 (and vice versa) are supposed to work like this:
# ns1: ping 10.0.2.2: passes via ipsec tunnel.
# ns2: ping 10.0.1.2: passes via ipsec tunnel.

# ns1: ping 10.0.1.253: passes via ipsec tunnel (direct policy)
# ns2: ping 10.0.2.253: passes via ipsec tunnel (direct policy)
#
# ns1: ping 10.0.2.254: does NOT pass via ipsec tunnel (exception)
# ns2: ping 10.0.1.254: does NOT pass via ipsec tunnel (exception)

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4
ret=0
policy_checks_ok=1

KEY_SHA=0xdeadbeef1234567890abcdefabcdefabcdefabcd
KEY_AES=0x0123456789abcdef0123456789012345
SPI1=0x1
SPI2=0x2

do_esp_policy() {
    local ns=$1
    local me=$2
    local remote=$3
    local lnet=$4
    local rnet=$5

    # to encrypt packets as they go out (includes forwarded packets that need encapsulation)
    ip -net $ns xfrm policy add src $lnet dst $rnet dir out tmpl src $me dst $remote proto esp mode tunnel priority 100 action allow
    # to fwd decrypted packets after esp processing:
    ip -net $ns xfrm policy add src $rnet dst $lnet dir fwd tmpl src $remote dst $me proto esp mode tunnel priority 100 action allow
}

do_esp() {
    local ns=$1
    local me=$2
    local remote=$3
    local lnet=$4
    local rnet=$5
    local spi_out=$6
    local spi_in=$7

    ip -net $ns xfrm state add src $remote dst $me proto esp spi $spi_in  enc aes $KEY_AES  auth sha1 $KEY_SHA  mode tunnel sel src $rnet dst $lnet
    ip -net $ns xfrm state add src $me  dst $remote proto esp spi $spi_out enc aes $KEY_AES auth sha1 $KEY_SHA mode tunnel sel src $lnet dst $rnet

    do_esp_policy $ns $me $remote $lnet $rnet
}

# add policies with different netmasks, to make sure kernel carries
# the policies contained within new netmask over when search tree is
# re-built.
# peer netns that are supposed to be encapsulated via esp have addresses
# in the 10.0.1.0/24 and 10.0.2.0/24 subnets, respectively.
#
# Adding a policy for '10.0.1.0/23' will make it necessary to
# alter the prefix of 10.0.1.0 subnet.
# In case new prefix overlaps with existing node, the node and all
# policies it carries need to be merged with the existing one(s).
#
# Do that here.
do_overlap()
{
    local ns=$1

    # adds new nodes to tree (neither network exists yet in policy database).
    ip -net $ns xfrm policy add src 10.1.0.0/24 dst 10.0.0.0/24 dir fwd priority 200 action block

    # adds a new node in the 10.0.0.0/24 tree (dst node exists).
    ip -net $ns xfrm policy add src 10.2.0.0/24 dst 10.0.0.0/24 dir fwd priority 200 action block

    # adds a 10.2.0.0/23 node, but for different dst.
    ip -net $ns xfrm policy add src 10.2.0.0/23 dst 10.0.1.0/24 dir fwd priority 200 action block

    # dst now overlaps with the 10.0.1.0/24 ESP policy in fwd.
    # kernel must 'promote' existing one (10.0.0.0/24) to 10.0.0.0/23.
    # But 10.0.0.0/23 also includes existing 10.0.1.0/24, so that node
    # also has to be merged too, including source-sorted subtrees.
    # old:
    # 10.0.0.0/24 (node 1 in dst tree of the bin)
    #    10.1.0.0/24 (node in src tree of dst node 1)
    #    10.2.0.0/24 (node in src tree of dst node 1)
    # 10.0.1.0/24 (node 2 in dst tree of the bin)
    #    10.0.2.0/24 (node in src tree of dst node 2)
    #    10.2.0.0/24 (node in src tree of dst node 2)
    #
    # The next 'policy add' adds dst '10.0.0.0/23', which means
    # that dst node 1 and dst node 2 have to be merged including
    # the sub-tree.  As no duplicates are allowed, policies in
    # the two '10.0.2.0/24' are also merged.
    #
    # after the 'add', internal search tree should look like this:
    # 10.0.0.0/23 (node in dst tree of bin)
    #     10.0.2.0/24 (node in src tree of dst node)
    #     10.1.0.0/24 (node in src tree of dst node)
    #     10.2.0.0/24 (node in src tree of dst node)
    #
    # 10.0.0.0/24 and 10.0.1.0/24 nodes have been merged as 10.0.0.0/23.
    ip -net $ns xfrm policy add src 10.1.0.0/24 dst 10.0.0.0/23 dir fwd priority 200 action block
}

do_esp_policy_get_check() {
    local ns=$1
    local lnet=$2
    local rnet=$3

    ip -net $ns xfrm policy get src $lnet dst $rnet dir out > /dev/null
    if [ $? -ne 0 ] && [ $policy_checks_ok -eq 1 ] ;then
        policy_checks_ok=0
        echo "FAIL: ip -net $ns xfrm policy get src $lnet dst $rnet dir out"
        ret=1
    fi

    ip -net $ns xfrm policy get src $rnet dst $lnet dir fwd > /dev/null
    if [ $? -ne 0 ] && [ $policy_checks_ok -eq 1 ] ;then
        policy_checks_ok=0
        echo "FAIL: ip -net $ns xfrm policy get src $rnet dst $lnet dir fwd"
        ret=1
    fi
}

do_exception() {
    local ns=$1
    local me=$2
    local remote=$3
    local encryptip=$4
    local plain=$5

    # network $plain passes without tunnel
    ip -net $ns xfrm policy add dst $plain dir out priority 10 action allow

    # direct policy for $encryptip, use tunnel, higher prio takes precedence
    ip -net $ns xfrm policy add dst $encryptip dir out tmpl src $me dst $remote proto esp mode tunnel priority 1 action allow
}

# policies that are not supposed to match any packets generated in this test.
do_dummies4() {
    local ns=$1

    for i in $(seq 10 16);do
      # dummy policy with wildcard src/dst.
      echo netns exec $ns ip xfrm policy add src 0.0.0.0/0 dst 10.$i.99.0/30 dir out action block
      echo netns exec $ns ip xfrm policy add src 10.$i.99.0/30 dst 0.0.0.0/0 dir out action block
      for j in $(seq 32 64);do
        echo netns exec $ns ip xfrm policy add src 10.$i.1.0/30 dst 10.$i.$j.0/30 dir out action block
        # silly, as it encompasses the one above too, but its allowed:
        echo netns exec $ns ip xfrm policy add src 10.$i.1.0/29 dst 10.$i.$j.0/29 dir out action block
        # and yet again, even more broad one.
        echo netns exec $ns ip xfrm policy add src 10.$i.1.0/24 dst 10.$i.$j.0/24 dir out action block
        echo netns exec $ns ip xfrm policy add src 10.$i.$j.0/24 dst 10.$i.1.0/24 dir fwd action block
      done
    done | ip -batch /dev/stdin
}

do_dummies6() {
    local ns=$1

    for i in $(seq 10 16);do
      for j in $(seq 32 64);do
       echo netns exec $ns ip xfrm policy add src dead:$i::/64 dst dead:$i:$j::/64 dir out action block
       echo netns exec $ns ip xfrm policy add src dead:$i:$j::/64 dst dead:$i::/24 dir fwd action block
      done
    done | ip -batch /dev/stdin
}

check_ipt_policy_count()
{
	ns=$1

	ip netns exec $ns iptables-save -c |grep policy | ( read c rest
		ip netns exec $ns iptables -Z
		if [ x"$c" = x'[0:0]' ]; then
			exit 0
		elif [ x"$c" = x ]; then
			echo "ERROR: No counters"
			ret=1
			exit 111
		else
			exit 1
		fi
	)
}

check_xfrm() {
	# 0: iptables -m policy rule count == 0
	# 1: iptables -m policy rule count != 0
	rval=$1
	ip=$2
	lret=0

	ip netns exec ns1 ping -q -c 1 10.0.2.$ip > /dev/null

	check_ipt_policy_count ns3
	if [ $? -ne $rval ] ; then
		lret=1
	fi
	check_ipt_policy_count ns4
	if [ $? -ne $rval ] ; then
		lret=1
	fi

	ip netns exec ns2 ping -q -c 1 10.0.1.$ip > /dev/null

	check_ipt_policy_count ns3
	if [ $? -ne $rval ] ; then
		lret=1
	fi
	check_ipt_policy_count ns4
	if [ $? -ne $rval ] ; then
		lret=1
	fi

	return $lret
}

check_exceptions()
{
	logpostfix="$1"
	local lret=0

	# ping to .254 should be excluded from the tunnel (exception is in place).
	check_xfrm 0 254
	if [ $? -ne 0 ]; then
		echo "FAIL: expected ping to .254 to fail ($logpostfix)"
		lret=1
	else
		echo "PASS: ping to .254 bypassed ipsec tunnel ($logpostfix)"
	fi

	# ping to .253 should use use ipsec due to direct policy exception.
	check_xfrm 1 253
	if [ $? -ne 0 ]; then
		echo "FAIL: expected ping to .253 to use ipsec tunnel ($logpostfix)"
		lret=1
	else
		echo "PASS: direct policy matches ($logpostfix)"
	fi

	# ping to .2 should use ipsec.
	check_xfrm 1 2
	if [ $? -ne 0 ]; then
		echo "FAIL: expected ping to .2 to use ipsec tunnel ($logpostfix)"
		lret=1
	else
		echo "PASS: policy matches ($logpostfix)"
	fi

	return $lret
}

#check for needed privileges
if [ "$(id -u)" -ne 0 ];then
	echo "SKIP: Need root privileges"
	exit $ksft_skip
fi

ip -Version 2>/dev/null >/dev/null
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without the ip tool"
	exit $ksft_skip
fi

# needed to check if policy lookup got valid ipsec result
iptables --version 2>/dev/null >/dev/null
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without iptables tool"
	exit $ksft_skip
fi

for i in 1 2 3 4; do
    ip netns add ns$i
    ip -net ns$i link set lo up
done

DEV=veth0
ip link add $DEV netns ns1 type veth peer name eth1 netns ns3
ip link add $DEV netns ns2 type veth peer name eth1 netns ns4

ip link add $DEV netns ns3 type veth peer name veth0 netns ns4

DEV=veth0
for i in 1 2; do
    ip -net ns$i link set $DEV up
    ip -net ns$i addr add 10.0.$i.2/24 dev $DEV
    ip -net ns$i addr add dead:$i::2/64 dev $DEV

    ip -net ns$i addr add 10.0.$i.253 dev $DEV
    ip -net ns$i addr add 10.0.$i.254 dev $DEV
    ip -net ns$i addr add dead:$i::fd dev $DEV
    ip -net ns$i addr add dead:$i::fe dev $DEV
done

for i in 3 4; do
ip -net ns$i link set eth1 up
ip -net ns$i link set veth0 up
done

ip -net ns1 route add default via 10.0.1.1
ip -net ns2 route add default via 10.0.2.1

ip -net ns3 addr add 10.0.1.1/24 dev eth1
ip -net ns3 addr add 10.0.3.1/24 dev veth0
ip -net ns3 addr add 2001:1::1/64 dev eth1
ip -net ns3 addr add 2001:3::1/64 dev veth0

ip -net ns3 route add default via 10.0.3.10

ip -net ns4 addr add 10.0.2.1/24 dev eth1
ip -net ns4 addr add 10.0.3.10/24 dev veth0
ip -net ns4 addr add 2001:2::1/64 dev eth1
ip -net ns4 addr add 2001:3::10/64 dev veth0
ip -net ns4 route add default via 10.0.3.1

for j in 4 6; do
	for i in 3 4;do
		ip netns exec ns$i sysctl net.ipv$j.conf.eth1.forwarding=1 > /dev/null
		ip netns exec ns$i sysctl net.ipv$j.conf.veth0.forwarding=1 > /dev/null
	done
done

# abuse iptables rule counter to check if ping matches a policy
ip netns exec ns3 iptables -p icmp -A FORWARD -m policy --dir out --pol ipsec
ip netns exec ns4 iptables -p icmp -A FORWARD -m policy --dir out --pol ipsec
if [ $? -ne 0 ];then
	echo "SKIP: Could not insert iptables rule"
	for i in 1 2 3 4;do ip netns del ns$i;done
	exit $ksft_skip
fi

#          localip  remoteip  localnet    remotenet
do_esp ns3 10.0.3.1 10.0.3.10 10.0.1.0/24 10.0.2.0/24 $SPI1 $SPI2
do_esp ns3 dead:3::1 dead:3::10 dead:1::/64 dead:2::/64 $SPI1 $SPI2
do_esp ns4 10.0.3.10 10.0.3.1 10.0.2.0/24 10.0.1.0/24 $SPI2 $SPI1
do_esp ns4 dead:3::10 dead:3::1 dead:2::/64 dead:1::/64 $SPI2 $SPI1

do_dummies4 ns3
do_dummies6 ns4

do_esp_policy_get_check ns3 10.0.1.0/24 10.0.2.0/24
do_esp_policy_get_check ns4 10.0.2.0/24 10.0.1.0/24
do_esp_policy_get_check ns3 dead:1::/64 dead:2::/64
do_esp_policy_get_check ns4 dead:2::/64 dead:1::/64

# ping to .254 should use ipsec, exception is not installed.
check_xfrm 1 254
if [ $? -ne 0 ]; then
	echo "FAIL: expected ping to .254 to use ipsec tunnel"
	ret=1
else
	echo "PASS: policy before exception matches"
fi

# installs exceptions
#                localip  remoteip   encryptdst  plaindst
do_exception ns3 10.0.3.1 10.0.3.10 10.0.2.253 10.0.2.240/28
do_exception ns4 10.0.3.10 10.0.3.1 10.0.1.253 10.0.1.240/28

do_exception ns3 dead:3::1 dead:3::10 dead:2::fd  dead:2:f0::/96
do_exception ns4 dead:3::10 dead:3::1 dead:1::fd  dead:1:f0::/96

check_exceptions "exceptions"
if [ $? -ne 0 ]; then
	ret=1
fi

# insert block policies with adjacent/overlapping netmasks
do_overlap ns3

check_exceptions "exceptions and block policies"
if [ $? -ne 0 ]; then
	ret=1
fi

for n in ns3 ns4;do
	ip -net $n xfrm policy set hthresh4 28 24 hthresh6 126 125
	sleep $((RANDOM%5))
done

check_exceptions "exceptions and block policies after hresh changes"

# full flush of policy db, check everything gets freed incl. internal meta data
ip -net ns3 xfrm policy flush

do_esp_policy ns3 10.0.3.1 10.0.3.10 10.0.1.0/24 10.0.2.0/24
do_exception ns3 10.0.3.1 10.0.3.10 10.0.2.253 10.0.2.240/28

# move inexact policies to hash table
ip -net ns3 xfrm policy set hthresh4 16 16

sleep $((RANDOM%5))
check_exceptions "exceptions and block policies after hthresh change in ns3"

# restore original hthresh settings -- move policies back to tables
for n in ns3 ns4;do
	ip -net $n xfrm policy set hthresh4 32 32 hthresh6 128 128
	sleep $((RANDOM%5))
done
check_exceptions "exceptions and block policies after hresh change to normal"

for i in 1 2 3 4;do ip netns del ns$i;done

exit $ret
