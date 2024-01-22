#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# L2TPv3 tunnel between 2 hosts
#
#            host-1          |   router   |     host-2
#                            |            |
#      lo          l2tp      |            |      l2tp           lo
# 172.16.101.1  172.16.1.1   |            | 172.16.1.2    172.16.101.2
#  fc00:101::1   fc00:1::1   |            |   fc00:1::2    fc00:101::2
#                            |            |
#                  eth0      |            |     eth0
#                10.1.1.1    |            |   10.1.2.1
#              2001:db8:1::1 |            | 2001:db8:2::1

source lib.sh
VERBOSE=0
PAUSE_ON_FAIL=no

which ping6 > /dev/null 2>&1 && ping6=$(which ping6) || ping6=$(which ping)

################################################################################
#
log_test()
{
	local rc=$1
	local expected=$2
	local msg="$3"

	if [ ${rc} -eq ${expected} ]; then
		printf "TEST: %-60s  [ OK ]\n" "${msg}"
		nsuccess=$((nsuccess+1))
	else
		ret=1
		nfail=$((nfail+1))
		printf "TEST: %-60s  [FAIL]\n" "${msg}"
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			echo
			echo "hit enter to continue, 'q' to quit"
			read a
			[ "$a" = "q" ] && exit 1
		fi
	fi
}

run_cmd()
{
	local ns
	local cmd
	local out
	local rc

	ns="$1"
	shift
	cmd="$*"

	if [ "$VERBOSE" = "1" ]; then
		printf "    COMMAND: $cmd\n"
	fi

	out=$(eval ip netns exec ${ns} ${cmd} 2>&1)
	rc=$?
	if [ "$VERBOSE" = "1" -a -n "$out" ]; then
		echo "    $out"
	fi

	[ "$VERBOSE" = "1" ] && echo

	return $rc
}

################################################################################
# create namespaces and interconnects

create_ns()
{
	local ns=$1
	local addr=$2
	local addr6=$3

	[ -z "${addr}" ] && addr="-"
	[ -z "${addr6}" ] && addr6="-"

	if [ "${addr}" != "-" ]; then
		ip -netns ${ns} addr add dev lo ${addr}
	fi
	if [ "${addr6}" != "-" ]; then
		ip -netns ${ns} -6 addr add dev lo ${addr6}
	fi

	ip -netns ${ns} ro add unreachable default metric 8192
	ip -netns ${ns} -6 ro add unreachable default metric 8192

	ip netns exec ${ns} sysctl -qw net.ipv4.ip_forward=1
	ip netns exec ${ns} sysctl -qw net.ipv6.conf.all.keep_addr_on_down=1
	ip netns exec ${ns} sysctl -qw net.ipv6.conf.all.forwarding=1
	ip netns exec ${ns} sysctl -qw net.ipv6.conf.default.forwarding=1
	ip netns exec ${ns} sysctl -qw net.ipv6.conf.default.accept_dad=0
}

# create veth pair to connect namespaces and apply addresses.
connect_ns()
{
	local ns1=$1
	local ns1_dev=$2
	local ns1_addr=$3
	local ns1_addr6=$4
	local ns2=$5
	local ns2_dev=$6
	local ns2_addr=$7
	local ns2_addr6=$8

	ip -netns ${ns1} li add ${ns1_dev} type veth peer name tmp
	ip -netns ${ns1} li set ${ns1_dev} up
	ip -netns ${ns1} li set tmp netns ${ns2} name ${ns2_dev}
	ip -netns ${ns2} li set ${ns2_dev} up

	if [ "${ns1_addr}" != "-" ]; then
		ip -netns ${ns1} addr add dev ${ns1_dev} ${ns1_addr}
		ip -netns ${ns2} addr add dev ${ns2_dev} ${ns2_addr}
	fi

	if [ "${ns1_addr6}" != "-" ]; then
		ip -netns ${ns1} addr add dev ${ns1_dev} ${ns1_addr6}
		ip -netns ${ns2} addr add dev ${ns2_dev} ${ns2_addr6}
	fi
}

################################################################################
# test setup

cleanup()
{
	cleanup_ns $host_1 $host_2 $router
}

setup_l2tp_ipv4()
{
	#
	# configure l2tpv3 tunnel on host-1
	#
	ip -netns $host_1 l2tp add tunnel tunnel_id 1041 peer_tunnel_id 1042 \
			 encap ip local 10.1.1.1 remote 10.1.2.1
	ip -netns $host_1 l2tp add session name l2tp4 tunnel_id 1041 \
			 session_id 1041 peer_session_id 1042
	ip -netns $host_1 link set dev l2tp4 up
	ip -netns $host_1 addr add dev l2tp4 172.16.1.1 peer 172.16.1.2

	#
	# configure l2tpv3 tunnel on host-2
	#
	ip -netns $host_2 l2tp add tunnel tunnel_id 1042 peer_tunnel_id 1041 \
			 encap ip local 10.1.2.1 remote 10.1.1.1
	ip -netns $host_2 l2tp add session name l2tp4 tunnel_id 1042 \
			 session_id 1042 peer_session_id 1041
	ip -netns $host_2 link set dev l2tp4 up
	ip -netns $host_2 addr add dev l2tp4 172.16.1.2 peer 172.16.1.1

	#
	# add routes to loopback addresses
	#
	ip -netns $host_1 ro add 172.16.101.2/32 via 172.16.1.2
	ip -netns $host_2 ro add 172.16.101.1/32 via 172.16.1.1
}

setup_l2tp_ipv6()
{
	#
	# configure l2tpv3 tunnel on host-1
	#
	ip -netns $host_1 l2tp add tunnel tunnel_id 1061 peer_tunnel_id 1062 \
			 encap ip local 2001:db8:1::1 remote 2001:db8:2::1
	ip -netns $host_1 l2tp add session name l2tp6 tunnel_id 1061 \
			 session_id 1061 peer_session_id 1062
	ip -netns $host_1 link set dev l2tp6 up
	ip -netns $host_1 addr add dev l2tp6 fc00:1::1 peer fc00:1::2

	#
	# configure l2tpv3 tunnel on host-2
	#
	ip -netns $host_2 l2tp add tunnel tunnel_id 1062 peer_tunnel_id 1061 \
			 encap ip local 2001:db8:2::1 remote 2001:db8:1::1
	ip -netns $host_2 l2tp add session name l2tp6 tunnel_id 1062 \
			 session_id 1062 peer_session_id 1061
	ip -netns $host_2 link set dev l2tp6 up
	ip -netns $host_2 addr add dev l2tp6 fc00:1::2 peer fc00:1::1

	#
	# add routes to loopback addresses
	#
	ip -netns $host_1 -6 ro add fc00:101::2/128 via fc00:1::2
	ip -netns $host_2 -6 ro add fc00:101::1/128 via fc00:1::1
}

setup()
{
	# start clean
	cleanup

	set -e
	setup_ns host_1 host_2 router
	create_ns $host_1 172.16.101.1/32 fc00:101::1/128
	create_ns $host_2 172.16.101.2/32 fc00:101::2/128
	create_ns $router

	connect_ns $host_1 eth0 10.1.1.1/24 2001:db8:1::1/64 \
	           $router eth1 10.1.1.2/24 2001:db8:1::2/64

	connect_ns $host_2 eth0 10.1.2.1/24 2001:db8:2::1/64 \
	           $router eth2 10.1.2.2/24 2001:db8:2::2/64

	ip -netns $host_1 ro add 10.1.2.0/24 via 10.1.1.2
	ip -netns $host_1 -6 ro add 2001:db8:2::/64 via 2001:db8:1::2

	ip -netns $host_2 ro add 10.1.1.0/24 via 10.1.2.2
	ip -netns $host_2 -6 ro add 2001:db8:1::/64 via 2001:db8:2::2

	setup_l2tp_ipv4
	setup_l2tp_ipv6
	set +e
}

setup_ipsec()
{
	#
	# IPv4
	#
	run_cmd $host_1 ip xfrm policy add \
		src 10.1.1.1 dst 10.1.2.1 dir out \
		tmpl proto esp mode transport

	run_cmd $host_1 ip xfrm policy add \
		src 10.1.2.1 dst 10.1.1.1 dir in \
		tmpl proto esp mode transport

	run_cmd $host_2 ip xfrm policy add \
		src 10.1.1.1 dst 10.1.2.1 dir in \
		tmpl proto esp mode transport

	run_cmd $host_2 ip xfrm policy add \
		src 10.1.2.1 dst 10.1.1.1 dir out \
		tmpl proto esp mode transport

	ip -netns $host_1 xfrm state add \
		src 10.1.1.1 dst 10.1.2.1 \
		spi 0x1000 proto esp aead 'rfc4106(gcm(aes))' \
		0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode transport

	ip -netns $host_1 xfrm state add \
		src 10.1.2.1 dst 10.1.1.1 \
		spi 0x1001 proto esp aead 'rfc4106(gcm(aes))' \
		0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode transport

	ip -netns $host_2 xfrm state add \
		src 10.1.1.1 dst 10.1.2.1 \
		spi 0x1000 proto esp aead 'rfc4106(gcm(aes))' \
		0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode transport

	ip -netns $host_2 xfrm state add \
		src 10.1.2.1 dst 10.1.1.1 \
		spi 0x1001 proto esp aead 'rfc4106(gcm(aes))' \
		0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode transport

	#
	# IPV6
	#
	run_cmd $host_1 ip -6 xfrm policy add \
		src 2001:db8:1::1 dst 2001:db8:2::1 dir out \
		tmpl proto esp mode transport

	run_cmd $host_1 ip -6 xfrm policy add \
		src 2001:db8:2::1 dst 2001:db8:1::1 dir in \
		tmpl proto esp mode transport

	run_cmd $host_2 ip -6 xfrm policy add \
		src 2001:db8:1::1 dst 2001:db8:2::1 dir in \
		tmpl proto esp mode transport

	run_cmd $host_2 ip -6 xfrm policy add \
		src 2001:db8:2::1 dst 2001:db8:1::1 dir out \
		tmpl proto esp mode transport

	ip -netns $host_1 -6 xfrm state add \
		src 2001:db8:1::1 dst 2001:db8:2::1 \
		spi 0x1000 proto esp aead 'rfc4106(gcm(aes))' \
		0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode transport

	ip -netns $host_1 -6 xfrm state add \
		src 2001:db8:2::1 dst 2001:db8:1::1 \
		spi 0x1001 proto esp aead 'rfc4106(gcm(aes))' \
		0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode transport

	ip -netns $host_2 -6 xfrm state add \
		src 2001:db8:1::1 dst 2001:db8:2::1 \
		spi 0x1000 proto esp aead 'rfc4106(gcm(aes))' \
		0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode transport

	ip -netns $host_2 -6 xfrm state add \
		src 2001:db8:2::1 dst 2001:db8:1::1 \
		spi 0x1001 proto esp aead 'rfc4106(gcm(aes))' \
		0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode transport
}

teardown_ipsec()
{
	run_cmd $host_1 ip xfrm state flush
	run_cmd $host_1 ip xfrm policy flush
	run_cmd $host_2 ip xfrm state flush
	run_cmd $host_2 ip xfrm policy flush
}

################################################################################
# generate traffic through tunnel for various cases

run_ping()
{
	local desc="$1"

	run_cmd $host_1 ping -c1 -w1 172.16.1.2
	log_test $? 0 "IPv4 basic L2TP tunnel ${desc}"

	run_cmd $host_1 ping -c1 -w1 -I 172.16.101.1 172.16.101.2
	log_test $? 0 "IPv4 route through L2TP tunnel ${desc}"

	run_cmd $host_1 ${ping6} -c1 -w1 fc00:1::2
	log_test $? 0 "IPv6 basic L2TP tunnel ${desc}"

	run_cmd $host_1 ${ping6} -c1 -w1 -I fc00:101::1 fc00:101::2
	log_test $? 0 "IPv6 route through L2TP tunnel ${desc}"
}

run_tests()
{
	local desc

	setup
	run_ping

	setup_ipsec
	run_ping "- with IPsec"
	run_cmd $host_1 ping -c1 -w1 172.16.1.2
	log_test $? 0 "IPv4 basic L2TP tunnel ${desc}"

	run_cmd $host_1 ping -c1 -w1 -I 172.16.101.1 172.16.101.2
	log_test $? 0 "IPv4 route through L2TP tunnel ${desc}"

	run_cmd $host_1 ${ping6} -c1 -w1 fc00:1::2
	log_test $? 0 "IPv6 basic L2TP tunnel - with IPsec"

	run_cmd $host_1 ${ping6} -c1 -w1 -I fc00:101::1 fc00:101::2
	log_test $? 0 "IPv6 route through L2TP tunnel - with IPsec"

	teardown_ipsec
	run_ping "- after IPsec teardown"
}

################################################################################
# main

declare -i nfail=0
declare -i nsuccess=0

while getopts :pv o
do
	case $o in
		p) PAUSE_ON_FAIL=yes;;
		v) VERBOSE=$(($VERBOSE + 1));;
		*) exit 1;;
	esac
done

run_tests
cleanup

printf "\nTests passed: %3d\n" ${nsuccess}
printf "Tests failed: %3d\n"   ${nfail}
