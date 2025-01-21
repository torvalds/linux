#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2019 David Ahern <dsahern@gmail.com>. All rights reserved.
# Copyright (c) 2020 Michael Jeanson <mjeanson@efficios.com>. All rights reserved.
#
# Requires CONFIG_NET_VRF, CONFIG_VETH, CONFIG_BRIDGE and CONFIG_NET_NS.
#
#
# Symmetric routing topology
#
#                     blue         red
# +----+              .253 +----+ .253              +----+
# | h1 |-------------------| r1 |-------------------| h2 |
# +----+ .1                +----+                .2 +----+
#         172.16.1/24                  172.16.2/24
#    2001:db8:16:1/64                  2001:db8:16:2/64
#
#
# Route from h1 to h2 and back goes through r1, incoming vrf blue has a route
# to the outgoing vrf red for the n2 network and red has a route back to n1.
# The red VRF interface has a MTU of 1400.
#
# The first test sends a ping with a ttl of 1 from h1 to h2 and parses the
# output of the command to check that a ttl expired error is received.
#
# The second test runs traceroute from h1 to h2 and parses the output to check
# for a hop on r1.
#
# The third test sends a ping with a packet size of 1450 from h1 to h2 and
# parses the output of the command to check that a fragmentation error is
# received.
#
#
# Asymmetric routing topology
#
# This topology represents a customer setup where the issue with icmp errors
# and VRF route leaking was initialy reported. The MTU test isn't done here
# because of the lack of a return route in the red VRF.
#
#                     blue         red
#                     .253 +----+ .253
#                     +----| r1 |----+
#                     |    +----+    |
# +----+              |              |              +----+
# | h1 |--------------+              +--------------| h2 |
# +----+ .1           |              |           .2 +----+
#         172.16.1/24 |    +----+    | 172.16.2/24
#    2001:db8:16:1/64 +----| r2 |----+ 2001:db8:16:2/64
#                     .254 +----+ .254
#
#
# Route from h1 to h2 goes through r1, incoming vrf blue has a route to the
# outgoing vrf red for the n2 network but red doesn't have a route back to n1.
# Route from h2 to h1 goes through r2.
#
# The objective is to check that the incoming vrf routing table is selected
# to send an ICMP error back to the source when the ttl of a packet reaches 1
# while it is forwarded between different vrfs.

source lib.sh
VERBOSE=0
PAUSE_ON_FAIL=no
DEFAULT_TTYPE=sym

H1_N1=172.16.1.0/24
H1_N1_6=2001:db8:16:1::/64

H1_N1_IP=172.16.1.1
R1_N1_IP=172.16.1.253
R2_N1_IP=172.16.1.254

H1_N1_IP6=2001:db8:16:1::1
R1_N1_IP6=2001:db8:16:1::253
R2_N1_IP6=2001:db8:16:1::254

H2_N2=172.16.2.0/24
H2_N2_6=2001:db8:16:2::/64

H2_N2_IP=172.16.2.2
R1_N2_IP=172.16.2.253
R2_N2_IP=172.16.2.254

H2_N2_IP6=2001:db8:16:2::2
R1_N2_IP6=2001:db8:16:2::253
R2_N2_IP6=2001:db8:16:2::254

################################################################################
# helpers

log_section()
{
	echo
	echo "###########################################################################"
	echo "$*"
	echo "###########################################################################"
	echo
}

log_test()
{
	local rc=$1
	local expected=$2
	local msg="$3"

	if [ "${rc}" -eq "${expected}" ]; then
		printf "TEST: %-60s  [ OK ]\n" "${msg}"
		nsuccess=$((nsuccess+1))
	else
		ret=1
		nfail=$((nfail+1))
		printf "TEST: %-60s  [FAIL]\n" "${msg}"
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			echo
			echo "hit enter to continue, 'q' to quit"
			read -r a
			[ "$a" = "q" ] && exit 1
		fi
	fi
}

run_cmd()
{
	local cmd="$*"
	local out
	local rc

	if [ "$VERBOSE" = "1" ]; then
		echo "COMMAND: $cmd"
	fi

	# shellcheck disable=SC2086
	out=$(eval $cmd 2>&1)
	rc=$?
	if [ "$VERBOSE" = "1" ] && [ -n "$out" ]; then
		echo "$out"
	fi

	[ "$VERBOSE" = "1" ] && echo

	return $rc
}

run_cmd_grep()
{
	local grep_pattern="$1"
	shift
	local cmd="$*"
	local out
	local rc

	if [ "$VERBOSE" = "1" ]; then
		echo "COMMAND: $cmd"
	fi

	# shellcheck disable=SC2086
	out=$(eval $cmd 2>&1)
	if [ "$VERBOSE" = "1" ] && [ -n "$out" ]; then
		echo "$out"
	fi

	echo "$out" | grep -q "$grep_pattern"
	rc=$?

	[ "$VERBOSE" = "1" ] && echo

	return $rc
}

################################################################################
# setup and teardown

cleanup()
{
	cleanup_ns $h1 $h2 $r1 $r2
}

setup_vrf()
{
	local ns=$1

	ip -netns "${ns}" rule del pref 0
	ip -netns "${ns}" rule add pref 32765 from all lookup local
	ip -netns "${ns}" -6 rule del pref 0
	ip -netns "${ns}" -6 rule add pref 32765 from all lookup local
}

create_vrf()
{
	local ns=$1
	local vrf=$2
	local table=$3

	ip -netns "${ns}" link add "${vrf}" type vrf table "${table}"
	ip -netns "${ns}" link set "${vrf}" up
	ip -netns "${ns}" route add vrf "${vrf}" unreachable default metric 8192
	ip -netns "${ns}" -6 route add vrf "${vrf}" unreachable default metric 8192

	ip -netns "${ns}" addr add 127.0.0.1/8 dev "${vrf}"
	ip -netns "${ns}" -6 addr add ::1 dev "${vrf}" nodad
}

setup_sym()
{
	local ns

	# make sure we are starting with a clean slate
	cleanup

	#
	# create nodes as namespaces
	setup_ns h1 h2 r1
	for ns in $h1 $h2 $r1; do
		if echo $ns | grep -q h[12]-; then
			ip netns exec $ns sysctl -q -w net.ipv6.conf.all.forwarding=0
			ip netns exec $ns sysctl -q -w net.ipv6.conf.all.keep_addr_on_down=1
		else
			ip netns exec $ns sysctl -q -w net.ipv4.ip_forward=1
			ip netns exec $ns sysctl -q -w net.ipv6.conf.all.forwarding=1
		fi
	done

	#
	# create interconnects
	#
	ip -netns $h1 link add eth0 type veth peer name r1h1
	ip -netns $h1 link set r1h1 netns $r1 name eth0 up

	ip -netns $h2 link add eth0 type veth peer name r1h2
	ip -netns $h2 link set r1h2 netns $r1 name eth1 up

	#
	# h1
	#
	ip -netns $h1 addr add dev eth0 ${H1_N1_IP}/24
	ip -netns $h1 -6 addr add dev eth0 ${H1_N1_IP6}/64 nodad
	ip -netns $h1 link set eth0 up

	# h1 to h2 via r1
	ip -netns $h1    route add ${H2_N2} via ${R1_N1_IP} dev eth0
	ip -netns $h1 -6 route add ${H2_N2_6} via "${R1_N1_IP6}" dev eth0

	#
	# h2
	#
	ip -netns $h2 addr add dev eth0 ${H2_N2_IP}/24
	ip -netns $h2 -6 addr add dev eth0 ${H2_N2_IP6}/64 nodad
	ip -netns $h2 link set eth0 up

	# h2 to h1 via r1
	ip -netns $h2 route add default via ${R1_N2_IP} dev eth0
	ip -netns $h2 -6 route add default via ${R1_N2_IP6} dev eth0

	#
	# r1
	#
	setup_vrf $r1
	create_vrf $r1 blue 1101
	create_vrf $r1 red 1102
	ip -netns $r1 link set mtu 1400 dev eth1
	ip -netns $r1 link set eth0 vrf blue up
	ip -netns $r1 link set eth1 vrf red up
	ip -netns $r1 addr add dev eth0 ${R1_N1_IP}/24
	ip -netns $r1 -6 addr add dev eth0 ${R1_N1_IP6}/64 nodad
	ip -netns $r1 addr add dev eth1 ${R1_N2_IP}/24
	ip -netns $r1 -6 addr add dev eth1 ${R1_N2_IP6}/64 nodad

	# Route leak from blue to red
	ip -netns $r1 route add vrf blue ${H2_N2} dev red
	ip -netns $r1 -6 route add vrf blue ${H2_N2_6} dev red

	# Route leak from red to blue
	ip -netns $r1 route add vrf red ${H1_N1} dev blue
	ip -netns $r1 -6 route add vrf red ${H1_N1_6} dev blue


	# Wait for ip config to settle
	sleep 2
}

setup_asym()
{
	local ns

	# make sure we are starting with a clean slate
	cleanup

	#
	# create nodes as namespaces
	setup_ns h1 h2 r1 r2
	for ns in $h1 $h2 $r1 $r2; do
		if echo $ns | grep -q h[12]-; then
			ip netns exec $ns sysctl -q -w net.ipv6.conf.all.forwarding=0
			ip netns exec $ns sysctl -q -w net.ipv6.conf.all.keep_addr_on_down=1
		else
			ip netns exec $ns sysctl -q -w net.ipv4.ip_forward=1
			ip netns exec $ns sysctl -q -w net.ipv6.conf.all.forwarding=1
		fi
	done

	#
	# create interconnects
	#
	ip -netns $h1 link add eth0 type veth peer name r1h1
	ip -netns $h1 link set r1h1 netns $r1 name eth0 up

	ip -netns $h1 link add eth1 type veth peer name r2h1
	ip -netns $h1 link set r2h1 netns $r2 name eth0 up

	ip -netns $h2 link add eth0 type veth peer name r1h2
	ip -netns $h2 link set r1h2 netns $r1 name eth1 up

	ip -netns $h2 link add eth1 type veth peer name r2h2
	ip -netns $h2 link set r2h2 netns $r2 name eth1 up

	#
	# h1
	#
	ip -netns $h1 link add br0 type bridge
	ip -netns $h1 link set br0 up
	ip -netns $h1 addr add dev br0 ${H1_N1_IP}/24
	ip -netns $h1 -6 addr add dev br0 ${H1_N1_IP6}/64 nodad
	ip -netns $h1 link set eth0 master br0 up
	ip -netns $h1 link set eth1 master br0 up

	# h1 to h2 via r1
	ip -netns $h1    route add ${H2_N2} via ${R1_N1_IP} dev br0
	ip -netns $h1 -6 route add ${H2_N2_6} via "${R1_N1_IP6}" dev br0

	#
	# h2
	#
	ip -netns $h2 link add br0 type bridge
	ip -netns $h2 link set br0 up
	ip -netns $h2 addr add dev br0 ${H2_N2_IP}/24
	ip -netns $h2 -6 addr add dev br0 ${H2_N2_IP6}/64 nodad
	ip -netns $h2 link set eth0 master br0 up
	ip -netns $h2 link set eth1 master br0 up

	# h2 to h1 via r2
	ip -netns $h2 route add default via ${R2_N2_IP} dev br0
	ip -netns $h2 -6 route add default via ${R2_N2_IP6} dev br0

	#
	# r1
	#
	setup_vrf $r1
	create_vrf $r1 blue 1101
	create_vrf $r1 red 1102
	ip -netns $r1 link set mtu 1400 dev eth1
	ip -netns $r1 link set eth0 vrf blue up
	ip -netns $r1 link set eth1 vrf red up
	ip -netns $r1 addr add dev eth0 ${R1_N1_IP}/24
	ip -netns $r1 -6 addr add dev eth0 ${R1_N1_IP6}/64 nodad
	ip -netns $r1 addr add dev eth1 ${R1_N2_IP}/24
	ip -netns $r1 -6 addr add dev eth1 ${R1_N2_IP6}/64 nodad

	# Route leak from blue to red
	ip -netns $r1 route add vrf blue ${H2_N2} dev red
	ip -netns $r1 -6 route add vrf blue ${H2_N2_6} dev red

	# No route leak from red to blue

	#
	# r2
	#
	ip -netns $r2 addr add dev eth0 ${R2_N1_IP}/24
	ip -netns $r2 -6 addr add dev eth0 ${R2_N1_IP6}/64 nodad
	ip -netns $r2 addr add dev eth1 ${R2_N2_IP}/24
	ip -netns $r2 -6 addr add dev eth1 ${R2_N2_IP6}/64 nodad

	# Wait for ip config to settle
	sleep 2
}

check_connectivity()
{
	ip netns exec $h1 ping -c1 -w1 ${H2_N2_IP} >/dev/null 2>&1
	log_test $? 0 "Basic IPv4 connectivity"
	return $?
}

check_connectivity6()
{
	ip netns exec $h1 "${ping6}" -c1 -w1 ${H2_N2_IP6} >/dev/null 2>&1
	log_test $? 0 "Basic IPv6 connectivity"
	return $?
}

check_traceroute()
{
	if [ ! -x "$(command -v traceroute)" ]; then
		echo "SKIP: Could not run IPV4 test without traceroute"
		return 1
	fi
}

check_traceroute6()
{
	if [ ! -x "$(command -v traceroute6)" ]; then
		echo "SKIP: Could not run IPV6 test without traceroute6"
		return 1
	fi
}

ipv4_traceroute()
{
	local ttype="$1"

	[ "x$ttype" = "x" ] && ttype="$DEFAULT_TTYPE"

	log_section "IPv4 ($ttype route): VRF ICMP error route lookup traceroute"

	check_traceroute || return

	setup_"$ttype"

	check_connectivity || return

	run_cmd_grep "${R1_N1_IP}" ip netns exec $h1 traceroute ${H2_N2_IP}
	log_test $? 0 "Traceroute reports a hop on r1"
}

ipv4_traceroute_asym()
{
	ipv4_traceroute asym
}

ipv6_traceroute()
{
	local ttype="$1"

	[ "x$ttype" = "x" ] && ttype="$DEFAULT_TTYPE"

	log_section "IPv6 ($ttype route): VRF ICMP error route lookup traceroute"

	check_traceroute6 || return

	setup_"$ttype"

	check_connectivity6 || return

	run_cmd_grep "${R1_N1_IP6}" ip netns exec $h1 traceroute6 ${H2_N2_IP6}
	log_test $? 0 "Traceroute6 reports a hop on r1"
}

ipv6_traceroute_asym()
{
	ipv6_traceroute asym
}

ipv4_ping_ttl()
{
	local ttype="$1"

	[ "x$ttype" = "x" ] && ttype="$DEFAULT_TTYPE"

	log_section "IPv4 ($ttype route): VRF ICMP ttl error route lookup ping"

	setup_"$ttype"

	check_connectivity || return

	run_cmd_grep "Time to live exceeded" ip netns exec $h1 ping -t1 -c1 -W2 ${H2_N2_IP}
	log_test $? 0 "Ping received ICMP ttl exceeded"
}

ipv4_ping_ttl_asym()
{
	ipv4_ping_ttl asym
}

ipv4_ping_frag()
{
	local ttype="$1"

	[ "x$ttype" = "x" ] && ttype="$DEFAULT_TTYPE"

	log_section "IPv4 ($ttype route): VRF ICMP fragmentation error route lookup ping"

	setup_"$ttype"

	check_connectivity || return

	run_cmd_grep "Frag needed" ip netns exec $h1 ping -s 1450 -Mdo -c1 -W2 ${H2_N2_IP}
	log_test $? 0 "Ping received ICMP Frag needed"
}

ipv4_ping_frag_asym()
{
	ipv4_ping_frag asym
}

ipv6_ping_ttl()
{
	local ttype="$1"

	[ "x$ttype" = "x" ] && ttype="$DEFAULT_TTYPE"

	log_section "IPv6 ($ttype route): VRF ICMP ttl error route lookup ping"

	setup_"$ttype"

	check_connectivity6 || return

	run_cmd_grep "Time exceeded: Hop limit" ip netns exec $h1 "${ping6}" -t1 -c1 -W2 ${H2_N2_IP6}
	log_test $? 0 "Ping received ICMP Hop limit"
}

ipv6_ping_ttl_asym()
{
	ipv6_ping_ttl asym
}

ipv6_ping_frag()
{
	local ttype="$1"

	[ "x$ttype" = "x" ] && ttype="$DEFAULT_TTYPE"

	log_section "IPv6 ($ttype route): VRF ICMP fragmentation error route lookup ping"

	setup_"$ttype"

	check_connectivity6 || return

	run_cmd_grep "Packet too big" ip netns exec $h1 "${ping6}" -s 1450 -Mdo -c1 -W2 ${H2_N2_IP6}
	log_test $? 0 "Ping received ICMP Packet too big"
}

ipv6_ping_frag_asym()
{
	ipv6_ping_frag asym
}

ipv4_ping_local()
{
	log_section "IPv4 (sym route): VRF ICMP local error route lookup ping"

	setup_sym

	check_connectivity || return

	run_cmd ip netns exec $r1 ip vrf exec blue ping -c1 -w1 ${H2_N2_IP}
	log_test $? 0 "VRF ICMP local IPv4"
}

ipv4_tcp_local()
{
	log_section "IPv4 (sym route): VRF tcp local connection"

	setup_sym

	check_connectivity || return

	run_cmd nettest -s -O "$h2" -l ${H2_N2_IP} -I eth0 -3 eth0 &
	sleep 1
	run_cmd nettest -N "$r1" -d blue -r ${H2_N2_IP}
	log_test $? 0 "VRF tcp local connection IPv4"
}

ipv4_udp_local()
{
	log_section "IPv4 (sym route): VRF udp local connection"

	setup_sym

	check_connectivity || return

	run_cmd nettest -s -D -O "$h2" -l ${H2_N2_IP} -I eth0 -3 eth0 &
	sleep 1
	run_cmd nettest -D -N "$r1" -d blue -r ${H2_N2_IP}
	log_test $? 0 "VRF udp local connection IPv4"
}

ipv6_ping_local()
{
	log_section "IPv6 (sym route): VRF ICMP local error route lookup ping"

	setup_sym

	check_connectivity6 || return

	run_cmd ip netns exec $r1 ip vrf exec blue ${ping6} -c1 -w1 ${H2_N2_IP6}
	log_test $? 0 "VRF ICMP local IPv6"
}

ipv6_tcp_local()
{
	log_section "IPv6 (sym route): VRF tcp local connection"

	setup_sym

	check_connectivity6 || return

	run_cmd nettest -s -6 -O "$h2" -l ${H2_N2_IP6} -I eth0 -3 eth0 &
	sleep 1
	run_cmd nettest -6 -N "$r1" -d blue -r ${H2_N2_IP6}
	log_test $? 0 "VRF tcp local connection IPv6"
}

ipv6_udp_local()
{
	log_section "IPv6 (sym route): VRF udp local connection"

	setup_sym

	check_connectivity6 || return

	run_cmd nettest -s -6 -D -O "$h2" -l ${H2_N2_IP6} -I eth0 -3 eth0 &
	sleep 1
	run_cmd nettest -6 -D -N "$r1" -d blue -r ${H2_N2_IP6}
	log_test $? 0 "VRF udp local connection IPv6"
}

################################################################################
# usage

usage()
{
        cat <<EOF
usage: ${0##*/} OPTS

	-4          Run IPv4 tests only
	-6          Run IPv6 tests only
        -t TEST     Run only TEST
	-p          Pause on fail
	-v          verbose mode (show commands and output)
EOF
}

################################################################################
# main

# Some systems don't have a ping6 binary anymore
command -v ping6 > /dev/null 2>&1 && ping6=$(command -v ping6) || ping6=$(command -v ping)

check_gen_prog "nettest"

TESTS_IPV4="ipv4_ping_ttl ipv4_traceroute ipv4_ping_frag ipv4_ping_local ipv4_tcp_local
ipv4_udp_local ipv4_ping_ttl_asym ipv4_traceroute_asym"
TESTS_IPV6="ipv6_ping_ttl ipv6_traceroute ipv6_ping_local ipv6_tcp_local ipv6_udp_local
ipv6_ping_ttl_asym ipv6_traceroute_asym"

ret=0
nsuccess=0
nfail=0

while getopts :46t:pvh o
do
	case $o in
		4) TESTS=ipv4;;
		6) TESTS=ipv6;;
		t) TESTS=$OPTARG;;
		p) PAUSE_ON_FAIL=yes;;
		v) VERBOSE=1;;
		h) usage; exit 0;;
		*) usage; exit 1;;
	esac
done

#
# show user test config
#
if [ -z "$TESTS" ]; then
        TESTS="$TESTS_IPV4 $TESTS_IPV6"
elif [ "$TESTS" = "ipv4" ]; then
        TESTS="$TESTS_IPV4"
elif [ "$TESTS" = "ipv6" ]; then
        TESTS="$TESTS_IPV6"
fi

for t in $TESTS
do
	case $t in
	ipv4_ping_ttl|ping)              ipv4_ping_ttl;;&
	ipv4_ping_ttl_asym|ping)         ipv4_ping_ttl_asym;;&
	ipv4_traceroute|traceroute)      ipv4_traceroute;;&
	ipv4_traceroute_asym|traceroute) ipv4_traceroute_asym;;&
	ipv4_ping_frag|ping)             ipv4_ping_frag;;&
	ipv4_ping_local|ping)            ipv4_ping_local;;&
	ipv4_tcp_local)                  ipv4_tcp_local;;&
	ipv4_udp_local)                  ipv4_udp_local;;&

	ipv6_ping_ttl|ping)              ipv6_ping_ttl;;&
	ipv6_ping_ttl_asym|ping)         ipv6_ping_ttl_asym;;&
	ipv6_traceroute|traceroute)      ipv6_traceroute;;&
	ipv6_traceroute_asym|traceroute) ipv6_traceroute_asym;;&
	ipv6_ping_frag|ping)             ipv6_ping_frag;;&
	ipv6_ping_local|ping)            ipv6_ping_local;;&
	ipv6_tcp_local)                  ipv6_tcp_local;;&
	ipv6_udp_local)                  ipv6_udp_local;;&

	# setup namespaces and config, but do not run any tests
	setup_sym|setup)                 setup_sym; exit 0;;
	setup_asym)                      setup_asym; exit 0;;

	help)                       echo "Test names: $TESTS"; exit 0;;
	esac
done

cleanup

printf "\nTests passed: %3d\n" ${nsuccess}
printf "Tests failed: %3d\n"   ${nfail}

exit $ret
