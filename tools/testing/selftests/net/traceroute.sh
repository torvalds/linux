#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Run traceroute/traceroute6 tests
#

source lib.sh
VERBOSE=0
PAUSE_ON_FAIL=no

################################################################################
#
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
		ip netns exec ${ns} ip addr add dev lo ${addr}
	fi
	if [ "${addr6}" != "-" ]; then
		ip netns exec ${ns} ip -6 addr add dev lo ${addr6}
	fi

	ip netns exec ${ns} ip ro add unreachable default metric 8192
	ip netns exec ${ns} ip -6 ro add unreachable default metric 8192

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

	ip netns exec ${ns1} ip li add ${ns1_dev} type veth peer name tmp
	ip netns exec ${ns1} ip li set ${ns1_dev} up
	ip netns exec ${ns1} ip li set tmp netns ${ns2} name ${ns2_dev}
	ip netns exec ${ns2} ip li set ${ns2_dev} up

	if [ "${ns1_addr}" != "-" ]; then
		ip netns exec ${ns1} ip addr add dev ${ns1_dev} ${ns1_addr}
	fi

	if [ "${ns2_addr}" != "-" ]; then
		ip netns exec ${ns2} ip addr add dev ${ns2_dev} ${ns2_addr}
	fi

	if [ "${ns1_addr6}" != "-" ]; then
		ip netns exec ${ns1} ip addr add dev ${ns1_dev} ${ns1_addr6}
	fi

	if [ "${ns2_addr6}" != "-" ]; then
		ip netns exec ${ns2} ip addr add dev ${ns2_dev} ${ns2_addr6}
	fi
}

################################################################################
# traceroute6 test
#
# Verify that in this scenario
#
#        ------------------------ N2
#         |                    |
#       ------              ------  N3  ----
#       | R1 |              | R2 |------|H2|
#       ------              ------      ----
#         |                    |
#        ------------------------ N1
#                  |
#                 ----
#                 |H1|
#                 ----
#
# where H1's default route goes through R1 and R1's default route goes
# through R2 over N2, traceroute6 from H1 to H2 reports R2's address
# on N2 and not N1.
#
# Addresses are assigned as follows:
#
# N1: 2000:101::/64
# N2: 2000:102::/64
# N3: 2000:103::/64
#
# R1's host part of address: 1
# R2's host part of address: 2
# H1's host part of address: 3
# H2's host part of address: 4
#
# For example:
# the IPv6 address of R1's interface on N2 is 2000:102::1/64

cleanup_traceroute6()
{
	cleanup_ns $h1 $h2 $r1 $r2
}

setup_traceroute6()
{
	brdev=br0

	# start clean
	cleanup_traceroute6

	set -e
	setup_ns h1 h2 r1 r2
	create_ns $h1
	create_ns $h2
	create_ns $r1
	create_ns $r2

	# Setup N3
	connect_ns $r2 eth3 - 2000:103::2/64 $h2 eth3 - 2000:103::4/64
	ip netns exec $h2 ip route add default via 2000:103::2

	# Setup N2
	connect_ns $r1 eth2 - 2000:102::1/64 $r2 eth2 - 2000:102::2/64
	ip netns exec $r1 ip route add default via 2000:102::2

	# Setup N1. host-1 and router-2 connect to a bridge in router-1.
	ip netns exec $r1 ip link add name ${brdev} type bridge
	ip netns exec $r1 ip link set ${brdev} up
	ip netns exec $r1 ip addr add 2000:101::1/64 dev ${brdev}

	connect_ns $h1 eth0 - 2000:101::3/64 $r1 eth0 - -
	ip netns exec $r1 ip link set dev eth0 master ${brdev}
	ip netns exec $h1 ip route add default via 2000:101::1

	connect_ns $r2 eth1 - 2000:101::2/64 $r1 eth1 - -
	ip netns exec $r1 ip link set dev eth1 master ${brdev}

	# Prime the network
	ip netns exec $h1 ping6 -c5 2000:103::4 >/dev/null 2>&1

	set +e
}

run_traceroute6()
{
	setup_traceroute6

	RET=0

	# traceroute6 host-2 from host-1 (expects 2000:102::2)
	run_cmd $h1 "traceroute6 2000:103::4 | grep -q 2000:102::2"
	check_err $? "traceroute6 did not return 2000:102::2"
	log_test "IPv6 traceroute"

	cleanup_traceroute6
}

################################################################################
# traceroute6 with VRF test
#
# Verify that in this scenario
#
#        ------------------------ N2
#         |                    |
#       ------              ------  N3  ----
#       | R1 |              | R2 |------|H2|
#       ------              ------      ----
#         |                    |
#        ------------------------ N1
#                  |
#                 ----
#                 |H1|
#                 ----
#
# Where H1's default route goes through R1 and R1's default route goes through
# R2 over N2, traceroute6 from H1 to H2 reports R2's address on N2 and not N1.
# The interfaces connecting R2 to the different subnets are membmer in a VRF
# and the intention is to check that traceroute6 does not report the VRF's
# address.
#
# Addresses are assigned as follows:
#
# N1: 2000:101::/64
# N2: 2000:102::/64
# N3: 2000:103::/64
#
# R1's host part of address: 1
# R2's host part of address: 2
# H1's host part of address: 3
# H2's host part of address: 4
#
# For example:
# the IPv6 address of R1's interface on N2 is 2000:102::1/64

cleanup_traceroute6_vrf()
{
	cleanup_all_ns
}

setup_traceroute6_vrf()
{
	# Start clean
	cleanup_traceroute6_vrf

	setup_ns h1 h2 r1 r2
	create_ns "$h1"
	create_ns "$h2"
	create_ns "$r1"
	create_ns "$r2"

	ip -n "$r2" link add name vrf100 up type vrf table 100
	ip -n "$r2" addr add 2001:db8:100::1/64 dev vrf100

	# Setup N3
	connect_ns "$r2" eth3 - 2000:103::2/64 "$h2" eth3 - 2000:103::4/64

	ip -n "$r2" link set dev eth3 master vrf100

	ip -n "$h2" route add default via 2000:103::2

	# Setup N2
	connect_ns "$r1" eth2 - 2000:102::1/64 "$r2" eth2 - 2000:102::2/64

	ip -n "$r1" route add default via 2000:102::2

	ip -n "$r2" link set dev eth2 master vrf100

	# Setup N1. host-1 and router-2 connect to a bridge in router-1.
	ip -n "$r1" link add name br100 up type bridge
	ip -n "$r1" addr add 2000:101::1/64 dev br100

	connect_ns "$h1" eth0 - 2000:101::3/64 "$r1" eth0 - -

	ip -n "$h1" route add default via 2000:101::1

	ip -n "$r1" link set dev eth0 master br100

	connect_ns "$r2" eth1 - 2000:101::2/64 "$r1" eth1 - -

	ip -n "$r2" link set dev eth1 master vrf100

	ip -n "$r1" link set dev eth1 master br100

	# Prime the network
	ip netns exec "$h1" ping6 -c5 2000:103::4 >/dev/null 2>&1
}

run_traceroute6_vrf()
{
	setup_traceroute6_vrf

	RET=0

	# traceroute6 host-2 from host-1 (expects 2000:102::2)
	run_cmd "$h1" "traceroute6 2000:103::4 | grep 2000:102::2"
	check_err $? "traceroute6 did not return 2000:102::2"
	log_test "IPv6 traceroute with VRF"

	cleanup_traceroute6_vrf
}

################################################################################
# traceroute test
#
# Verify that traceroute from H1 to H2 shows 1.0.3.1 and 1.0.1.1 when
# traceroute uses 1.0.3.3 and 1.0.1.3 as the source IP, respectively.
#
#      1.0.3.3/24    1.0.3.1/24
# ---- 1.0.1.3/24    1.0.1.1/24 ---- 1.0.2.1/24    1.0.2.4/24 ----
# |H1|--------------------------|R1|--------------------------|H2|
# ----            N1            ----            N2            ----
#
# where net.ipv4.icmp_errors_use_inbound_ifaddr is set on R1 and 1.0.3.1/24 and
# 1.0.1.1/24 are R1's primary addresses on N1. The kernel is expected to prefer
# a source address that is on the same subnet as the destination IP of the ICMP
# error message.

cleanup_traceroute()
{
	cleanup_ns $h1 $h2 $router
}

setup_traceroute()
{
	# start clean
	cleanup_traceroute

	set -e
	setup_ns h1 h2 router
	create_ns $h1
	create_ns $h2
	create_ns $router

	connect_ns $h1 eth0 1.0.1.3/24 - \
	           $router eth1 1.0.3.1/24 -
	ip -n "$h1" addr add 1.0.3.3/24 dev eth0
	ip netns exec $h1 ip route add default via 1.0.1.1

	ip netns exec $router ip addr add 1.0.1.1/24 dev eth1
	ip netns exec $router sysctl -qw \
				net.ipv4.icmp_errors_use_inbound_ifaddr=1

	connect_ns $h2 eth0 1.0.2.4/24 - \
	           $router eth2 1.0.2.1/24 -
	ip netns exec $h2 ip route add default via 1.0.2.1

	# Prime the network
	ip netns exec $h1 ping -c5 1.0.2.4 >/dev/null 2>&1

	set +e
}

run_traceroute()
{
	setup_traceroute

	RET=0

	# traceroute host-2 from host-1. Expect a source IP that is on the same
	# subnet as destination IP of the ICMP error message.
	run_cmd "$h1" "traceroute -s 1.0.1.3 1.0.2.4 | grep -q 1.0.1.1"
	check_err $? "traceroute did not return 1.0.1.1"
	run_cmd "$h1" "traceroute -s 1.0.3.3 1.0.2.4 | grep -q 1.0.3.1"
	check_err $? "traceroute did not return 1.0.3.1"
	log_test "IPv4 traceroute"

	cleanup_traceroute
}

################################################################################
# traceroute with VRF test
#
# Verify that traceroute from H1 to H2 shows 1.0.3.1 and 1.0.1.1 when
# traceroute uses 1.0.3.3 and 1.0.1.3 as the source IP, respectively. The
# intention is to check that the kernel does not choose an IP assigned to the
# VRF device, but rather an address from the VRF port (eth1) that received the
# packet that generates the ICMP error message.
#
#                          1.0.4.1/24 (vrf100)
#      1.0.3.3/24    1.0.3.1/24
# ---- 1.0.1.3/24    1.0.1.1/24 ---- 1.0.2.1/24    1.0.2.4/24 ----
# |H1|--------------------------|R1|--------------------------|H2|
# ----            N1            ----            N2            ----

cleanup_traceroute_vrf()
{
	cleanup_all_ns
}

setup_traceroute_vrf()
{
	# Start clean
	cleanup_traceroute_vrf

	setup_ns h1 h2 router
	create_ns "$h1"
	create_ns "$h2"
	create_ns "$router"

	ip -n "$router" link add name vrf100 up type vrf table 100
	ip -n "$router" addr add 1.0.4.1/24 dev vrf100

	connect_ns "$h1" eth0 1.0.1.3/24 - \
	           "$router" eth1 1.0.1.1/24 -

	ip -n "$h1" addr add 1.0.3.3/24 dev eth0
	ip -n "$h1" route add default via 1.0.1.1

	ip -n "$router" link set dev eth1 master vrf100
	ip -n "$router" addr add 1.0.3.1/24 dev eth1
	ip netns exec "$router" sysctl -qw \
		net.ipv4.icmp_errors_use_inbound_ifaddr=1

	connect_ns "$h2" eth0 1.0.2.4/24 - \
	           "$router" eth2 1.0.2.1/24 -

	ip -n "$h2" route add default via 1.0.2.1

	ip -n "$router" link set dev eth2 master vrf100

	# Prime the network
	ip netns exec "$h1" ping -c5 1.0.2.4 >/dev/null 2>&1
}

run_traceroute_vrf()
{
	setup_traceroute_vrf

	RET=0

	# traceroute host-2 from host-1. Expect a source IP that is on the same
	# subnet as destination IP of the ICMP error message.
	run_cmd "$h1" "traceroute -s 1.0.1.3 1.0.2.4 | grep 1.0.1.1"
	check_err $? "traceroute did not return 1.0.1.1"
	run_cmd "$h1" "traceroute -s 1.0.3.3 1.0.2.4 | grep 1.0.3.1"
	check_err $? "traceroute did not return 1.0.3.1"
	log_test "IPv4 traceroute with VRF"

	cleanup_traceroute_vrf
}

################################################################################
# Run tests

run_tests()
{
	run_traceroute6
	run_traceroute6_vrf
	run_traceroute
	run_traceroute_vrf
}

################################################################################
# main

while getopts :pv o
do
	case $o in
		p) PAUSE_ON_FAIL=yes;;
		v) VERBOSE=$(($VERBOSE + 1));;
		*) exit 1;;
	esac
done

require_command traceroute6
require_command traceroute

run_tests

exit "${EXIT_STATUS}"
