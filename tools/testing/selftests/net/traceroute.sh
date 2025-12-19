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

__check_traceroute_version()
{
	local cmd=$1; shift
	local req_ver=$1; shift
	local ver

	req_ver=$(echo "$req_ver" | sed 's/\.//g')
	ver=$($cmd -V 2>&1 | grep -Eo '[0-9]+.[0-9]+.[0-9]+' | sed 's/\.//g')
	if [[ $ver -lt $req_ver ]]; then
		return 1
	else
		return 0
	fi
}

check_traceroute6_version()
{
	local req_ver=$1; shift

	__check_traceroute_version traceroute6 "$req_ver"
}

check_traceroute_version()
{
	local req_ver=$1; shift

	__check_traceroute_version traceroute "$req_ver"
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
	ip netns exec ${ns} sysctl -qw net.ipv4.icmp_ratelimit=0
	ip netns exec ${ns} sysctl -qw net.ipv6.icmp.ratelimit=0
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
# traceroute6 with ICMP extensions test
#
# Verify that in this scenario
#
# ----                          ----                          ----
# |H1|--------------------------|R1|--------------------------|H2|
# ----            N1            ----            N2            ----
#
# ICMP extensions are correctly reported. The loopback interfaces on all the
# nodes are assigned global addresses and the interfaces connecting the nodes
# are assigned IPv6 link-local addresses.

cleanup_traceroute6_ext()
{
	cleanup_all_ns
}

setup_traceroute6_ext()
{
	# Start clean
	cleanup_traceroute6_ext

	setup_ns h1 r1 h2
	create_ns "$h1"
	create_ns "$r1"
	create_ns "$h2"

	# Setup N1
	connect_ns "$h1" eth1 - fe80::1/64 "$r1" eth1 - fe80::2/64
	# Setup N2
	connect_ns "$r1" eth2 - fe80::3/64 "$h2" eth2 - fe80::4/64

	# Setup H1
	ip -n "$h1" address add 2001:db8:1::1/128 dev lo
	ip -n "$h1" route add ::/0 nexthop via fe80::2 dev eth1

	# Setup R1
	ip -n "$r1" address add 2001:db8:1::2/128 dev lo
	ip -n "$r1" route add 2001:db8:1::1/128 nexthop via fe80::1 dev eth1
	ip -n "$r1" route add 2001:db8:1::3/128 nexthop via fe80::4 dev eth2

	# Setup H2
	ip -n "$h2" address add 2001:db8:1::3/128 dev lo
	ip -n "$h2" route add ::/0 nexthop via fe80::3 dev eth2

	# Prime the network
	ip netns exec "$h1" ping6 -c5 2001:db8:1::3 >/dev/null 2>&1
}

traceroute6_ext_iio_iif_test()
{
	local r1_ifindex h2_ifindex
	local pkt_len=$1; shift

	# Test that incoming interface info is not appended by default.
	run_cmd "$h1" "traceroute6 -e 2001:db8:1::3 $pkt_len | grep INC"
	check_fail $? "Incoming interface info appended by default when should not"

	# Test that the extension is appended when enabled.
	run_cmd "$r1" "bash -c \"echo 0x01 > /proc/sys/net/ipv6/icmp/errors_extension_mask\""
	check_err $? "Failed to enable incoming interface info extension on R1"

	run_cmd "$h1" "traceroute6 -e 2001:db8:1::3 $pkt_len | grep INC"
	check_err $? "Incoming interface info not appended after enable"

	# Test that the extension is not appended when disabled.
	run_cmd "$r1" "bash -c \"echo 0x00 > /proc/sys/net/ipv6/icmp/errors_extension_mask\""
	check_err $? "Failed to disable incoming interface info extension on R1"

	run_cmd "$h1" "traceroute6 -e 2001:db8:1::3 $pkt_len | grep INC"
	check_fail $? "Incoming interface info appended after disable"

	# Test that the extension is sent correctly from both R1 and H2.
	run_cmd "$r1" "sysctl -w net.ipv6.icmp.errors_extension_mask=0x01"
	r1_ifindex=$(ip -n "$r1" -j link show dev eth1 | jq '.[]["ifindex"]')
	run_cmd "$h1" "traceroute6 -e 2001:db8:1::3 $pkt_len | grep '<INC:$r1_ifindex,\"eth1\",mtu=1500>'"
	check_err $? "Wrong incoming interface info reported from R1"

	run_cmd "$h2" "sysctl -w net.ipv6.icmp.errors_extension_mask=0x01"
	h2_ifindex=$(ip -n "$h2" -j link show dev eth2 | jq '.[]["ifindex"]')
	run_cmd "$h1" "traceroute6 -e 2001:db8:1::3 $pkt_len | grep '<INC:$h2_ifindex,\"eth2\",mtu=1500>'"
	check_err $? "Wrong incoming interface info reported from H2"

	# Add a global address on the incoming interface of R1 and check that
	# it is reported.
	run_cmd "$r1" "ip address add 2001:db8:100::1/64 dev eth1 nodad"
	run_cmd "$h1" "traceroute6 -e 2001:db8:1::3 $pkt_len | grep '<INC:$r1_ifindex,2001:db8:100::1,\"eth1\",mtu=1500>'"
	check_err $? "Wrong incoming interface info reported from R1 after address addition"
	run_cmd "$r1" "ip address del 2001:db8:100::1/64 dev eth1"

	# Change name and MTU and make sure the result is still correct.
	run_cmd "$r1" "ip link set dev eth1 name eth1tag mtu 1501"
	run_cmd "$h1" "traceroute6 -e 2001:db8:1::3 $pkt_len | grep '<INC:$r1_ifindex,\"eth1tag\",mtu=1501>'"
	check_err $? "Wrong incoming interface info reported from R1 after name and MTU change"
	run_cmd "$r1" "ip link set dev eth1tag name eth1 mtu 1500"

	run_cmd "$r1" "sysctl -w net.ipv6.icmp.errors_extension_mask=0x00"
	run_cmd "$h2" "sysctl -w net.ipv6.icmp.errors_extension_mask=0x00"
}

run_traceroute6_ext()
{
	# Need at least version 2.1.5 for RFC 5837 support.
	if ! check_traceroute6_version 2.1.5; then
		log_test_skip "traceroute6 too old, missing ICMP extensions support"
		return
	fi

	setup_traceroute6_ext

	RET=0

	## General ICMP extensions tests

	# Test that ICMP extensions are disabled by default.
	run_cmd "$h1" "sysctl net.ipv6.icmp.errors_extension_mask | grep \"= 0$\""
	check_err $? "ICMP extensions are not disabled by default"

	# Test that unsupported values are rejected. Do not use "sysctl" as
	# older versions do not return an error code upon failure.
	run_cmd "$h1" "bash -c \"echo 0x80 > /proc/sys/net/ipv6/icmp/errors_extension_mask\""
	check_fail $? "Unsupported sysctl value was not rejected"

	## Extension-specific tests

	# Incoming interface info test. Test with various packet sizes,
	# including the default one.
	traceroute6_ext_iio_iif_test
	traceroute6_ext_iio_iif_test 127
	traceroute6_ext_iio_iif_test 128
	traceroute6_ext_iio_iif_test 129

	log_test "IPv6 traceroute with ICMP extensions"

	cleanup_traceroute6_ext
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
# traceroute with ICMP extensions test
#
# Verify that in this scenario
#
# ----                          ----                          ----
# |H1|--------------------------|R1|--------------------------|H2|
# ----            N1            ----            N2            ----
#
# ICMP extensions are correctly reported. The loopback interfaces on all the
# nodes are assigned global addresses and the interfaces connecting the nodes
# are assigned IPv6 link-local addresses.

cleanup_traceroute_ext()
{
	cleanup_all_ns
}

setup_traceroute_ext()
{
	# Start clean
	cleanup_traceroute_ext

	setup_ns h1 r1 h2
	create_ns "$h1"
	create_ns "$r1"
	create_ns "$h2"

	# Setup N1
	connect_ns "$h1" eth1 - fe80::1/64 "$r1" eth1 - fe80::2/64
	# Setup N2
	connect_ns "$r1" eth2 - fe80::3/64 "$h2" eth2 - fe80::4/64

	# Setup H1
	ip -n "$h1" address add 192.0.2.1/32 dev lo
	ip -n "$h1" route add 0.0.0.0/0 nexthop via inet6 fe80::2 dev eth1

	# Setup R1
	ip -n "$r1" address add 192.0.2.2/32 dev lo
	ip -n "$r1" route add 192.0.2.1/32 nexthop via inet6 fe80::1 dev eth1
	ip -n "$r1" route add 192.0.2.3/32 nexthop via inet6 fe80::4 dev eth2

	# Setup H2
	ip -n "$h2" address add 192.0.2.3/32 dev lo
	ip -n "$h2" route add 0.0.0.0/0 nexthop via inet6 fe80::3 dev eth2

	# Prime the network
	ip netns exec "$h1" ping -c5 192.0.2.3 >/dev/null 2>&1
}

traceroute_ext_iio_iif_test()
{
	local r1_ifindex h2_ifindex
	local pkt_len=$1; shift

	# Test that incoming interface info is not appended by default.
	run_cmd "$h1" "traceroute -e 192.0.2.3 $pkt_len | grep INC"
	check_fail $? "Incoming interface info appended by default when should not"

	# Test that the extension is appended when enabled.
	run_cmd "$r1" "bash -c \"echo 0x01 > /proc/sys/net/ipv4/icmp_errors_extension_mask\""
	check_err $? "Failed to enable incoming interface info extension on R1"

	run_cmd "$h1" "traceroute -e 192.0.2.3 $pkt_len | grep INC"
	check_err $? "Incoming interface info not appended after enable"

	# Test that the extension is not appended when disabled.
	run_cmd "$r1" "bash -c \"echo 0x00 > /proc/sys/net/ipv4/icmp_errors_extension_mask\""
	check_err $? "Failed to disable incoming interface info extension on R1"

	run_cmd "$h1" "traceroute -e 192.0.2.3 $pkt_len | grep INC"
	check_fail $? "Incoming interface info appended after disable"

	# Test that the extension is sent correctly from both R1 and H2.
	run_cmd "$r1" "sysctl -w net.ipv4.icmp_errors_extension_mask=0x01"
	r1_ifindex=$(ip -n "$r1" -j link show dev eth1 | jq '.[]["ifindex"]')
	run_cmd "$h1" "traceroute -e 192.0.2.3 $pkt_len | grep '<INC:$r1_ifindex,\"eth1\",mtu=1500>'"
	check_err $? "Wrong incoming interface info reported from R1"

	run_cmd "$h2" "sysctl -w net.ipv4.icmp_errors_extension_mask=0x01"
	h2_ifindex=$(ip -n "$h2" -j link show dev eth2 | jq '.[]["ifindex"]')
	run_cmd "$h1" "traceroute -e 192.0.2.3 $pkt_len | grep '<INC:$h2_ifindex,\"eth2\",mtu=1500>'"
	check_err $? "Wrong incoming interface info reported from H2"

	# Add a global address on the incoming interface of R1 and check that
	# it is reported.
	run_cmd "$r1" "ip address add 198.51.100.1/24 dev eth1"
	run_cmd "$h1" "traceroute -e 192.0.2.3 $pkt_len | grep '<INC:$r1_ifindex,198.51.100.1,\"eth1\",mtu=1500>'"
	check_err $? "Wrong incoming interface info reported from R1 after address addition"
	run_cmd "$r1" "ip address del 198.51.100.1/24 dev eth1"

	# Change name and MTU and make sure the result is still correct.
	# Re-add the route towards H1 since it was deleted when we removed the
	# last IPv4 address from eth1 on R1.
	run_cmd "$r1" "ip route add 192.0.2.1/32 nexthop via inet6 fe80::1 dev eth1"
	run_cmd "$r1" "ip link set dev eth1 name eth1tag mtu 1501"
	run_cmd "$h1" "traceroute -e 192.0.2.3 $pkt_len | grep '<INC:$r1_ifindex,\"eth1tag\",mtu=1501>'"
	check_err $? "Wrong incoming interface info reported from R1 after name and MTU change"
	run_cmd "$r1" "ip link set dev eth1tag name eth1 mtu 1500"

	run_cmd "$r1" "sysctl -w net.ipv4.icmp_errors_extension_mask=0x00"
	run_cmd "$h2" "sysctl -w net.ipv4.icmp_errors_extension_mask=0x00"
}

run_traceroute_ext()
{
	# Need at least version 2.1.5 for RFC 5837 support.
	if ! check_traceroute_version 2.1.5; then
		log_test_skip "traceroute too old, missing ICMP extensions support"
		return
	fi

	setup_traceroute_ext

	RET=0

	## General ICMP extensions tests

	# Test that ICMP extensions are disabled by default.
	run_cmd "$h1" "sysctl net.ipv4.icmp_errors_extension_mask | grep \"= 0$\""
	check_err $? "ICMP extensions are not disabled by default"

	# Test that unsupported values are rejected. Do not use "sysctl" as
	# older versions do not return an error code upon failure.
	run_cmd "$h1" "bash -c \"echo 0x80 > /proc/sys/net/ipv4/icmp_errors_extension_mask\""
	check_fail $? "Unsupported sysctl value was not rejected"

	## Extension-specific tests

	# Incoming interface info test. Test with various packet sizes,
	# including the default one.
	traceroute_ext_iio_iif_test
	traceroute_ext_iio_iif_test 127
	traceroute_ext_iio_iif_test 128
	traceroute_ext_iio_iif_test 129

	log_test "IPv4 traceroute with ICMP extensions"

	cleanup_traceroute_ext
}

################################################################################
# Run tests

run_tests()
{
	run_traceroute6
	run_traceroute6_vrf
	run_traceroute6_ext
	run_traceroute
	run_traceroute_vrf
	run_traceroute_ext
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
require_command jq

run_tests

exit "${EXIT_STATUS}"
