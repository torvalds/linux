#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

[[ -z $TC ]] && TC='tc'
[[ -z $IP ]] && IP='ip'

REDIRECT_USER='./tc_l2_redirect'
REDIRECT_BPF='./tc_l2_redirect_kern.o'

RP_FILTER=$(< /proc/sys/net/ipv4/conf/all/rp_filter)
IPV6_DISABLED=$(< /proc/sys/net/ipv6/conf/all/disable_ipv6)
IPV6_FORWARDING=$(< /proc/sys/net/ipv6/conf/all/forwarding)

function config_common {
	local tun_type=$1

	$IP netns add ns1
	$IP netns add ns2
	$IP link add ve1 type veth peer name vens1
	$IP link add ve2 type veth peer name vens2
	$IP link set dev ve1 up
	$IP link set dev ve2 up
	$IP link set dev ve1 mtu 1500
	$IP link set dev ve2 mtu 1500
	$IP link set dev vens1 netns ns1
	$IP link set dev vens2 netns ns2

	$IP -n ns1 link set dev lo up
	$IP -n ns1 link set dev vens1 up
	$IP -n ns1 addr add 10.1.1.101/24 dev vens1
	$IP -n ns1 addr add 2401:db01::65/64 dev vens1 nodad
	$IP -n ns1 route add default via 10.1.1.1 dev vens1
	$IP -n ns1 route add default via 2401:db01::1 dev vens1

	$IP -n ns2 link set dev lo up
	$IP -n ns2 link set dev vens2 up
	$IP -n ns2 addr add 10.2.1.102/24 dev vens2
	$IP -n ns2 addr add 2401:db02::66/64 dev vens2 nodad
	$IP -n ns2 addr add 10.10.1.102 dev lo
	$IP -n ns2 addr add 2401:face::66/64 dev lo nodad
	$IP -n ns2 link add ipt2 type ipip local 10.2.1.102 remote 10.2.1.1
	$IP -n ns2 link add ip6t2 type ip6tnl mode any local 2401:db02::66 remote 2401:db02::1
	$IP -n ns2 link set dev ipt2 up
	$IP -n ns2 link set dev ip6t2 up
	$IP netns exec ns2 $TC qdisc add dev vens2 clsact
	$IP netns exec ns2 $TC filter add dev vens2 ingress bpf da obj $REDIRECT_BPF sec drop_non_tun_vip
	if [[ $tun_type == "ipip" ]]; then
		$IP -n ns2 route add 10.1.1.0/24 dev ipt2
		$IP netns exec ns2 sysctl -q -w net.ipv4.conf.all.rp_filter=0
		$IP netns exec ns2 sysctl -q -w net.ipv4.conf.ipt2.rp_filter=0
	else
		$IP -n ns2 route add 10.1.1.0/24 dev ip6t2
		$IP -n ns2 route add 2401:db01::/64 dev ip6t2
		$IP netns exec ns2 sysctl -q -w net.ipv4.conf.all.rp_filter=0
		$IP netns exec ns2 sysctl -q -w net.ipv4.conf.ip6t2.rp_filter=0
	fi

	$IP addr add 10.1.1.1/24 dev ve1
	$IP addr add 2401:db01::1/64 dev ve1 nodad
	$IP addr add 10.2.1.1/24 dev ve2
	$IP addr add 2401:db02::1/64 dev ve2 nodad

	$TC qdisc add dev ve2 clsact
	$TC filter add dev ve2 ingress bpf da obj $REDIRECT_BPF sec l2_to_iptun_ingress_forward

	sysctl -q -w net.ipv4.conf.all.rp_filter=0
	sysctl -q -w net.ipv6.conf.all.forwarding=1
	sysctl -q -w net.ipv6.conf.all.disable_ipv6=0
}

function cleanup {
	set +e
	[[ -z $DEBUG ]] || set +x
	$IP netns delete ns1 >& /dev/null
	$IP netns delete ns2 >& /dev/null
	$IP link del ve1 >& /dev/null
	$IP link del ve2 >& /dev/null
	$IP link del ipt >& /dev/null
	$IP link del ip6t >& /dev/null
	sysctl -q -w net.ipv4.conf.all.rp_filter=$RP_FILTER
	sysctl -q -w net.ipv6.conf.all.forwarding=$IPV6_FORWARDING
	sysctl -q -w net.ipv6.conf.all.disable_ipv6=$IPV6_DISABLED
	rm -f /sys/fs/bpf/tc/globals/tun_iface
	[[ -z $DEBUG ]] || set -x
	set -e
}

function l2_to_ipip {
	echo -n "l2_to_ipip $1: "

	local dir=$1

	config_common ipip

	$IP link add ipt type ipip external
	$IP link set dev ipt up
	sysctl -q -w net.ipv4.conf.ipt.rp_filter=0
	sysctl -q -w net.ipv4.conf.ipt.forwarding=1

	if [[ $dir == "egress" ]]; then
		$IP route add 10.10.1.0/24 via 10.2.1.102 dev ve2
		$TC filter add dev ve2 egress bpf da obj $REDIRECT_BPF sec l2_to_iptun_ingress_redirect
		sysctl -q -w net.ipv4.conf.ve1.forwarding=1
	else
		$TC qdisc add dev ve1 clsact
		$TC filter add dev ve1 ingress bpf da obj $REDIRECT_BPF sec l2_to_iptun_ingress_redirect
	fi

	$REDIRECT_USER -U /sys/fs/bpf/tc/globals/tun_iface -i $(< /sys/class/net/ipt/ifindex)

	$IP netns exec ns1 ping -c1 10.10.1.102 >& /dev/null

	if [[ $dir == "egress" ]]; then
		# test direct egress to ve2 (i.e. not forwarding from
		# ve1 to ve2).
		ping -c1 10.10.1.102 >& /dev/null
	fi

	cleanup

	echo "OK"
}

function l2_to_ip6tnl {
	echo -n "l2_to_ip6tnl $1: "

	local dir=$1

	config_common ip6tnl

	$IP link add ip6t type ip6tnl mode any external
	$IP link set dev ip6t up
	sysctl -q -w net.ipv4.conf.ip6t.rp_filter=0
	sysctl -q -w net.ipv4.conf.ip6t.forwarding=1

	if [[ $dir == "egress" ]]; then
		$IP route add 10.10.1.0/24 via 10.2.1.102 dev ve2
		$IP route add 2401:face::/64 via 2401:db02::66 dev ve2
		$TC filter add dev ve2 egress bpf da obj $REDIRECT_BPF sec l2_to_ip6tun_ingress_redirect
		sysctl -q -w net.ipv4.conf.ve1.forwarding=1
	else
		$TC qdisc add dev ve1 clsact
		$TC filter add dev ve1 ingress bpf da obj $REDIRECT_BPF sec l2_to_ip6tun_ingress_redirect
	fi

	$REDIRECT_USER -U /sys/fs/bpf/tc/globals/tun_iface -i $(< /sys/class/net/ip6t/ifindex)

	$IP netns exec ns1 ping -c1 10.10.1.102 >& /dev/null
	$IP netns exec ns1 ping -6 -c1 2401:face::66 >& /dev/null

	if [[ $dir == "egress" ]]; then
		# test direct egress to ve2 (i.e. not forwarding from
		# ve1 to ve2).
		ping -c1 10.10.1.102 >& /dev/null
		ping -6 -c1 2401:face::66 >& /dev/null
	fi

	cleanup

	echo "OK"
}

cleanup
test_names="l2_to_ipip l2_to_ip6tnl"
test_dirs="ingress egress"
if [[ $# -ge 2 ]]; then
	test_names=$1
	test_dirs=$2
elif [[ $# -ge 1 ]]; then
	test_names=$1
fi

for t in $test_names; do
	for d in $test_dirs; do
		$t $d
	done
done
