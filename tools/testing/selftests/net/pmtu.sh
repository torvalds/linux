#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Check that route PMTU values match expectations, and that initial device MTU
# values are assigned correctly
#
# Tests currently implemented:
#
# - pmtu_ipv4
#	Set up two namespaces, A and B, with two paths between them over routers
#	R1 and R2 (also implemented with namespaces), with different MTUs:
#
#	  segment a_r1    segment b_r1		a_r1: 2000
#	.--------------R1--------------.	b_r1: 1400
#	A                               B	a_r2: 2000
#	'--------------R2--------------'	b_r2: 1500
#	  segment a_r2    segment b_r2
#
#	Check that PMTU exceptions with the correct PMTU are created. Then
#	decrease and increase the MTU of the local link for one of the paths,
#	A to R1, checking that route exception PMTU changes accordingly over
#	this path. Also check that locked exceptions are created when an ICMP
#	message advertising a PMTU smaller than net.ipv4.route.min_pmtu is
#	received
#
# - pmtu_ipv6
#	Same as pmtu_ipv4, except for locked PMTU tests, using IPv6
#
# - pmtu_ipv4_vxlan4_exception
#	Set up the same network topology as pmtu_ipv4, create a VXLAN tunnel
#	over IPv4 between A and B, routed via R1. On the link between R1 and B,
#	set a MTU lower than the VXLAN MTU and the MTU on the link between A and
#	R1. Send IPv4 packets, exceeding the MTU between R1 and B, over VXLAN
#	from A to B and check that the PMTU exception is created with the right
#	value on A
#
# - pmtu_ipv6_vxlan4_exception
#	Same as pmtu_ipv4_vxlan4_exception, but send IPv6 packets from A to B
#
# - pmtu_ipv4_vxlan6_exception
#	Same as pmtu_ipv4_vxlan4_exception, but use IPv6 transport from A to B
#
# - pmtu_ipv6_vxlan6_exception
#	Same as pmtu_ipv4_vxlan6_exception, but send IPv6 packets from A to B
#
# - pmtu_ipv4_geneve4_exception
#	Same as pmtu_ipv4_vxlan4_exception, but using a GENEVE tunnel instead of
#	VXLAN
#
# - pmtu_ipv6_geneve4_exception
#	Same as pmtu_ipv6_vxlan4_exception, but using a GENEVE tunnel instead of
#	VXLAN
#
# - pmtu_ipv4_geneve6_exception
#	Same as pmtu_ipv4_vxlan6_exception, but using a GENEVE tunnel instead of
#	VXLAN
#
# - pmtu_ipv6_geneve6_exception
#	Same as pmtu_ipv6_vxlan6_exception, but using a GENEVE tunnel instead of
#	VXLAN
#
# - pmtu_ipv{4,6}_br_vxlan{4,6}_exception
#	Set up three namespaces, A, B, and C, with routing between A and B over
#	R1. R2 is unused in these tests. A has a veth connection to C, and is
#	connected to B via a VXLAN endpoint, which is directly bridged to C.
#	MTU on the B-R1 link is lower than other MTUs.
#
#	Check that both C and A are able to communicate with B over the VXLAN
#	tunnel, and that PMTU exceptions with the correct values are created.
#
#	                  segment a_r1    segment b_r1            b_r1: 4000
#	                .--------------R1--------------.    everything
#	   C---veth     A                               B         else: 5000
#	        ' bridge                                |
#	            '---- - - - - - VXLAN - - - - - - - '
#
# - pmtu_ipv{4,6}_br_geneve{4,6}_exception
#	Same as pmtu_ipv{4,6}_br_vxlan{4,6}_exception, with a GENEVE tunnel
#	instead.
#
# - pmtu_ipv{4,6}_ovs_vxlan{4,6}_exception
#	Set up two namespaces, B, and C, with routing between the init namespace
#	and B over R1. A and R2 are unused in these tests. The init namespace
#	has a veth connection to C, and is connected to B via a VXLAN endpoint,
#	which is handled by Open vSwitch and bridged to C. MTU on the B-R1 link
#	is lower than other MTUs.
#
#	Check that C is able to communicate with B over the VXLAN tunnel, and
#	that PMTU exceptions with the correct values are created.
#
#	                  segment a_r1    segment b_r1            b_r1: 4000
#	                .--------------R1--------------.    everything
#	   C---veth    init                             B         else: 5000
#	        '- ovs                                  |
#	            '---- - - - - - VXLAN - - - - - - - '
#
# - pmtu_ipv{4,6}_ovs_geneve{4,6}_exception
#	Same as pmtu_ipv{4,6}_ovs_vxlan{4,6}_exception, with a GENEVE tunnel
#	instead.
#
# - pmtu_ipv{4,6}_fou{4,6}_exception
#	Same as pmtu_ipv4_vxlan4, but using a direct IPv4/IPv6 encapsulation
#	(FoU) over IPv4/IPv6, instead of VXLAN
#
# - pmtu_ipv{4,6}_fou{4,6}_exception
#	Same as pmtu_ipv4_vxlan4, but using a generic UDP IPv4/IPv6
#	encapsulation (GUE) over IPv4/IPv6, instead of VXLAN
#
# - pmtu_ipv{4,6}_ipv{4,6}_exception
#	Same as pmtu_ipv4_vxlan4, but using a IPv4/IPv6 tunnel over IPv4/IPv6,
#	instead of VXLAN
#
# - pmtu_vti4_exception
#	Set up vti tunnel on top of veth, with xfrm states and policies, in two
#	namespaces with matching endpoints. Check that route exception is not
#	created if link layer MTU is not exceeded, then exceed it and check that
#	exception is created with the expected PMTU. The approach described
#	below for IPv6 doesn't apply here, because, on IPv4, administrative MTU
#	changes alone won't affect PMTU
#
# - pmtu_vti6_exception
#	Set up vti6 tunnel on top of veth, with xfrm states and policies, in two
#	namespaces with matching endpoints. Check that route exception is
#	created by exceeding link layer MTU with ping to other endpoint. Then
#	decrease and increase MTU of tunnel, checking that route exception PMTU
#	changes accordingly
#
# - pmtu_vti4_default_mtu
#	Set up vti4 tunnel on top of veth, in two namespaces with matching
#	endpoints. Check that MTU assigned to vti interface is the MTU of the
#	lower layer (veth) minus additional lower layer headers (zero, for veth)
#	minus IPv4 header length
#
# - pmtu_vti6_default_mtu
#	Same as above, for IPv6
#
# - pmtu_vti4_link_add_mtu
#	Set up vti4 interface passing MTU value at link creation, check MTU is
#	configured, and that link is not created with invalid MTU values
#
# - pmtu_vti6_link_add_mtu
#	Same as above, for IPv6
#
# - pmtu_vti6_link_change_mtu
#	Set up two dummy interfaces with different MTUs, create a vti6 tunnel
#	and check that configured MTU is used on link creation and changes, and
#	that MTU is properly calculated instead when MTU is not configured from
#	userspace
#
# - cleanup_ipv4_exception
#	Similar to pmtu_ipv4_vxlan4_exception, but explicitly generate PMTU
#	exceptions on multiple CPUs and check that the veth device tear-down
# 	happens in a timely manner
#
# - cleanup_ipv6_exception
#	Same as above, but use IPv6 transport from A to B
#
# - list_flush_ipv4_exception
#	Using the same topology as in pmtu_ipv4, create exceptions, and check
#	they are shown when listing exception caches, gone after flushing them
#
# - list_flush_ipv6_exception
#	Using the same topology as in pmtu_ipv6, create exceptions, and check
#	they are shown when listing exception caches, gone after flushing them
#
# - pmtu_ipv4_route_change
#	Use the same topology as in pmtu_ipv4, but issue a route replacement
#	command and delete the corresponding device afterward. This tests for
#	proper cleanup of the PMTU exceptions by the route replacement path.
#	Device unregistration should complete successfully
#
# - pmtu_ipv6_route_change
#	Same as above but with IPv6

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

PAUSE_ON_FAIL=no
VERBOSE=0
TRACING=0

# Some systems don't have a ping6 binary anymore
which ping6 > /dev/null 2>&1 && ping6=$(which ping6) || ping6=$(which ping)

#               Name                          Description                  re-run with nh
tests="
	pmtu_ipv4_exception		ipv4: PMTU exceptions			1
	pmtu_ipv6_exception		ipv6: PMTU exceptions			1
	pmtu_ipv4_vxlan4_exception	IPv4 over vxlan4: PMTU exceptions	1
	pmtu_ipv6_vxlan4_exception	IPv6 over vxlan4: PMTU exceptions	1
	pmtu_ipv4_vxlan6_exception	IPv4 over vxlan6: PMTU exceptions	1
	pmtu_ipv6_vxlan6_exception	IPv6 over vxlan6: PMTU exceptions	1
	pmtu_ipv4_geneve4_exception	IPv4 over geneve4: PMTU exceptions	1
	pmtu_ipv6_geneve4_exception	IPv6 over geneve4: PMTU exceptions	1
	pmtu_ipv4_geneve6_exception	IPv4 over geneve6: PMTU exceptions	1
	pmtu_ipv6_geneve6_exception	IPv6 over geneve6: PMTU exceptions	1
	pmtu_ipv4_br_vxlan4_exception	IPv4, bridged vxlan4: PMTU exceptions	1
	pmtu_ipv6_br_vxlan4_exception	IPv6, bridged vxlan4: PMTU exceptions	1
	pmtu_ipv4_br_vxlan6_exception	IPv4, bridged vxlan6: PMTU exceptions	1
	pmtu_ipv6_br_vxlan6_exception	IPv6, bridged vxlan6: PMTU exceptions	1
	pmtu_ipv4_br_geneve4_exception	IPv4, bridged geneve4: PMTU exceptions	1
	pmtu_ipv6_br_geneve4_exception	IPv6, bridged geneve4: PMTU exceptions	1
	pmtu_ipv4_br_geneve6_exception	IPv4, bridged geneve6: PMTU exceptions	1
	pmtu_ipv6_br_geneve6_exception	IPv6, bridged geneve6: PMTU exceptions	1
	pmtu_ipv4_ovs_vxlan4_exception	IPv4, OVS vxlan4: PMTU exceptions	1
	pmtu_ipv6_ovs_vxlan4_exception	IPv6, OVS vxlan4: PMTU exceptions	1
	pmtu_ipv4_ovs_vxlan6_exception	IPv4, OVS vxlan6: PMTU exceptions	1
	pmtu_ipv6_ovs_vxlan6_exception	IPv6, OVS vxlan6: PMTU exceptions	1
	pmtu_ipv4_ovs_geneve4_exception	IPv4, OVS geneve4: PMTU exceptions	1
	pmtu_ipv6_ovs_geneve4_exception	IPv6, OVS geneve4: PMTU exceptions	1
	pmtu_ipv4_ovs_geneve6_exception	IPv4, OVS geneve6: PMTU exceptions	1
	pmtu_ipv6_ovs_geneve6_exception	IPv6, OVS geneve6: PMTU exceptions	1
	pmtu_ipv4_fou4_exception	IPv4 over fou4: PMTU exceptions		1
	pmtu_ipv6_fou4_exception	IPv6 over fou4: PMTU exceptions		1
	pmtu_ipv4_fou6_exception	IPv4 over fou6: PMTU exceptions		1
	pmtu_ipv6_fou6_exception	IPv6 over fou6: PMTU exceptions		1
	pmtu_ipv4_gue4_exception	IPv4 over gue4: PMTU exceptions		1
	pmtu_ipv6_gue4_exception	IPv6 over gue4: PMTU exceptions		1
	pmtu_ipv4_gue6_exception	IPv4 over gue6: PMTU exceptions		1
	pmtu_ipv6_gue6_exception	IPv6 over gue6: PMTU exceptions		1
	pmtu_ipv4_ipv4_exception	IPv4 over IPv4: PMTU exceptions		1
	pmtu_ipv6_ipv4_exception	IPv6 over IPv4: PMTU exceptions		1
	pmtu_ipv4_ipv6_exception	IPv4 over IPv6: PMTU exceptions		1
	pmtu_ipv6_ipv6_exception	IPv6 over IPv6: PMTU exceptions		1
	pmtu_vti6_exception		vti6: PMTU exceptions			0
	pmtu_vti4_exception		vti4: PMTU exceptions			0
	pmtu_vti4_default_mtu		vti4: default MTU assignment		0
	pmtu_vti6_default_mtu		vti6: default MTU assignment		0
	pmtu_vti4_link_add_mtu		vti4: MTU setting on link creation	0
	pmtu_vti6_link_add_mtu		vti6: MTU setting on link creation	0
	pmtu_vti6_link_change_mtu	vti6: MTU changes on link changes	0
	cleanup_ipv4_exception		ipv4: cleanup of cached exceptions	1
	cleanup_ipv6_exception		ipv6: cleanup of cached exceptions	1
	list_flush_ipv4_exception	ipv4: list and flush cached exceptions	1
	list_flush_ipv6_exception	ipv6: list and flush cached exceptions	1
	pmtu_ipv4_route_change		ipv4: PMTU exception w/route replace	1
	pmtu_ipv6_route_change		ipv6: PMTU exception w/route replace	1"

NS_A="ns-A"
NS_B="ns-B"
NS_C="ns-C"
NS_R1="ns-R1"
NS_R2="ns-R2"
ns_a="ip netns exec ${NS_A}"
ns_b="ip netns exec ${NS_B}"
ns_c="ip netns exec ${NS_C}"
ns_r1="ip netns exec ${NS_R1}"
ns_r2="ip netns exec ${NS_R2}"

# Addressing and routing for tests with routers: four network segments, with
# index SEGMENT between 1 and 4, a common prefix (PREFIX4 or PREFIX6) and an
# identifier ID, which is 1 for hosts (A and B), 2 for routers (R1 and R2).
# Addresses are:
# - IPv4: PREFIX4.SEGMENT.ID (/24)
# - IPv6: PREFIX6:SEGMENT::ID (/64)
prefix4="10.0"
prefix6="fc00"
a_r1=1
a_r2=2
b_r1=3
b_r2=4
#	ns	peer	segment
routing_addrs="
	A	R1	${a_r1}
	A	R2	${a_r2}
	B	R1	${b_r1}
	B	R2	${b_r2}
"
# Traffic from A to B goes through R1 by default, and through R2, if destined to
# B's address on the b_r2 segment.
# Traffic from B to A goes through R1.
#	ns	destination		gateway
routes="
	A	default			${prefix4}.${a_r1}.2
	A	${prefix4}.${b_r2}.1	${prefix4}.${a_r2}.2
	B	default			${prefix4}.${b_r1}.2

	A	default			${prefix6}:${a_r1}::2
	A	${prefix6}:${b_r2}::1	${prefix6}:${a_r2}::2
	B	default			${prefix6}:${b_r1}::2
"

USE_NH="no"
#	ns	family	nh id	   destination		gateway
nexthops="
	A	4	41	${prefix4}.${a_r1}.2	veth_A-R1
	A	4	42	${prefix4}.${a_r2}.2	veth_A-R2
	B	4	41	${prefix4}.${b_r1}.2	veth_B-R1

	A	6	61	${prefix6}:${a_r1}::2	veth_A-R1
	A	6	62	${prefix6}:${a_r2}::2	veth_A-R2
	B	6	61	${prefix6}:${b_r1}::2	veth_B-R1
"

# nexthop id correlates to id in nexthops config above
#	ns    family	prefix			nh id
routes_nh="
	A	4	default			41
	A	4	${prefix4}.${b_r2}.1	42
	B	4	default			41

	A	6	default			61
	A	6	${prefix6}:${b_r2}::1	62
	B	6	default			61
"

veth4_a_addr="192.168.1.1"
veth4_b_addr="192.168.1.2"
veth4_c_addr="192.168.2.10"
veth4_mask="24"
veth6_a_addr="fd00:1::a"
veth6_b_addr="fd00:1::b"
veth6_c_addr="fd00:2::c"
veth6_mask="64"

tunnel4_a_addr="192.168.2.1"
tunnel4_b_addr="192.168.2.2"
tunnel4_mask="24"
tunnel6_a_addr="fd00:2::a"
tunnel6_b_addr="fd00:2::b"
tunnel6_mask="64"

dummy6_0_prefix="fc00:1000::"
dummy6_1_prefix="fc00:1001::"
dummy6_mask="64"

err_buf=
tcpdump_pids=

err() {
	err_buf="${err_buf}${1}
"
}

err_flush() {
	echo -n "${err_buf}"
	err_buf=
}

run_cmd() {
	cmd="$*"

	if [ "$VERBOSE" = "1" ]; then
		printf "    COMMAND: $cmd\n"
	fi

	out="$($cmd 2>&1)"
	rc=$?
	if [ "$VERBOSE" = "1" -a -n "$out" ]; then
		echo "    $out"
		echo
	fi

	return $rc
}

# Find the auto-generated name for this namespace
nsname() {
	eval echo \$NS_$1
}

setup_fou_or_gue() {
	outer="${1}"
	inner="${2}"
	encap="${3}"

	if [ "${outer}" = "4" ]; then
		modprobe fou || return 2
		a_addr="${prefix4}.${a_r1}.1"
		b_addr="${prefix4}.${b_r1}.1"
		if [ "${inner}" = "4" ]; then
			type="ipip"
			ipproto="4"
		else
			type="sit"
			ipproto="41"
		fi
	else
		modprobe fou6 || return 2
		a_addr="${prefix6}:${a_r1}::1"
		b_addr="${prefix6}:${b_r1}::1"
		if [ "${inner}" = "4" ]; then
			type="ip6tnl"
			mode="mode ipip6"
			ipproto="4 -6"
		else
			type="ip6tnl"
			mode="mode ip6ip6"
			ipproto="41 -6"
		fi
	fi

	run_cmd ${ns_a} ip fou add port 5555 ipproto ${ipproto} || return 2
	run_cmd ${ns_a} ip link add ${encap}_a type ${type} ${mode} local ${a_addr} remote ${b_addr} encap ${encap} encap-sport auto encap-dport 5556 || return 2

	run_cmd ${ns_b} ip fou add port 5556 ipproto ${ipproto}
	run_cmd ${ns_b} ip link add ${encap}_b type ${type} ${mode} local ${b_addr} remote ${a_addr} encap ${encap} encap-sport auto encap-dport 5555

	if [ "${inner}" = "4" ]; then
		run_cmd ${ns_a} ip addr add ${tunnel4_a_addr}/${tunnel4_mask} dev ${encap}_a
		run_cmd ${ns_b} ip addr add ${tunnel4_b_addr}/${tunnel4_mask} dev ${encap}_b
	else
		run_cmd ${ns_a} ip addr add ${tunnel6_a_addr}/${tunnel6_mask} dev ${encap}_a
		run_cmd ${ns_b} ip addr add ${tunnel6_b_addr}/${tunnel6_mask} dev ${encap}_b
	fi

	run_cmd ${ns_a} ip link set ${encap}_a up
	run_cmd ${ns_b} ip link set ${encap}_b up
}

setup_fou44() {
	setup_fou_or_gue 4 4 fou
}

setup_fou46() {
	setup_fou_or_gue 4 6 fou
}

setup_fou64() {
	setup_fou_or_gue 6 4 fou
}

setup_fou66() {
	setup_fou_or_gue 6 6 fou
}

setup_gue44() {
	setup_fou_or_gue 4 4 gue
}

setup_gue46() {
	setup_fou_or_gue 4 6 gue
}

setup_gue64() {
	setup_fou_or_gue 6 4 gue
}

setup_gue66() {
	setup_fou_or_gue 6 6 gue
}

setup_ipvX_over_ipvY() {
	inner=${1}
	outer=${2}

	if [ "${outer}" -eq 4 ]; then
		a_addr="${prefix4}.${a_r1}.1"
		b_addr="${prefix4}.${b_r1}.1"
		if [ "${inner}" -eq 4 ]; then
			type="ipip"
			mode="ipip"
		else
			type="sit"
			mode="ip6ip"
		fi
	else
		a_addr="${prefix6}:${a_r1}::1"
		b_addr="${prefix6}:${b_r1}::1"
		type="ip6tnl"
		if [ "${inner}" -eq 4 ]; then
			mode="ipip6"
		else
			mode="ip6ip6"
		fi
	fi

	run_cmd ${ns_a} ip link add ip_a type ${type} local ${a_addr} remote ${b_addr} mode ${mode} || return 2
	run_cmd ${ns_b} ip link add ip_b type ${type} local ${b_addr} remote ${a_addr} mode ${mode}

	run_cmd ${ns_a} ip link set ip_a up
	run_cmd ${ns_b} ip link set ip_b up

	if [ "${inner}" = "4" ]; then
		run_cmd ${ns_a} ip addr add ${tunnel4_a_addr}/${tunnel4_mask} dev ip_a
		run_cmd ${ns_b} ip addr add ${tunnel4_b_addr}/${tunnel4_mask} dev ip_b
	else
		run_cmd ${ns_a} ip addr add ${tunnel6_a_addr}/${tunnel6_mask} dev ip_a
		run_cmd ${ns_b} ip addr add ${tunnel6_b_addr}/${tunnel6_mask} dev ip_b
	fi
}

setup_ip4ip4() {
	setup_ipvX_over_ipvY 4 4
}

setup_ip6ip4() {
	setup_ipvX_over_ipvY 6 4
}

setup_ip4ip6() {
	setup_ipvX_over_ipvY 4 6
}

setup_ip6ip6() {
	setup_ipvX_over_ipvY 6 6
}

setup_namespaces() {
	for n in ${NS_A} ${NS_B} ${NS_C} ${NS_R1} ${NS_R2}; do
		ip netns add ${n} || return 1

		# Disable DAD, so that we don't have to wait to use the
		# configured IPv6 addresses
		ip netns exec ${n} sysctl -q net/ipv6/conf/default/accept_dad=0
	done
}

setup_veth() {
	run_cmd ${ns_a} ip link add veth_a type veth peer name veth_b || return 1
	run_cmd ${ns_a} ip link set veth_b netns ${NS_B}

	run_cmd ${ns_a} ip addr add ${veth4_a_addr}/${veth4_mask} dev veth_a
	run_cmd ${ns_b} ip addr add ${veth4_b_addr}/${veth4_mask} dev veth_b

	run_cmd ${ns_a} ip addr add ${veth6_a_addr}/${veth6_mask} dev veth_a
	run_cmd ${ns_b} ip addr add ${veth6_b_addr}/${veth6_mask} dev veth_b

	run_cmd ${ns_a} ip link set veth_a up
	run_cmd ${ns_b} ip link set veth_b up
}

setup_vti() {
	proto=${1}
	veth_a_addr="${2}"
	veth_b_addr="${3}"
	vti_a_addr="${4}"
	vti_b_addr="${5}"
	vti_mask=${6}

	[ ${proto} -eq 6 ] && vti_type="vti6" || vti_type="vti"

	run_cmd ${ns_a} ip link add vti${proto}_a type ${vti_type} local ${veth_a_addr} remote ${veth_b_addr} key 10 || return 1
	run_cmd ${ns_b} ip link add vti${proto}_b type ${vti_type} local ${veth_b_addr} remote ${veth_a_addr} key 10

	run_cmd ${ns_a} ip addr add ${vti_a_addr}/${vti_mask} dev vti${proto}_a
	run_cmd ${ns_b} ip addr add ${vti_b_addr}/${vti_mask} dev vti${proto}_b

	run_cmd ${ns_a} ip link set vti${proto}_a up
	run_cmd ${ns_b} ip link set vti${proto}_b up
}

setup_vti4() {
	setup_vti 4 ${veth4_a_addr} ${veth4_b_addr} ${tunnel4_a_addr} ${tunnel4_b_addr} ${tunnel4_mask}
}

setup_vti6() {
	setup_vti 6 ${veth6_a_addr} ${veth6_b_addr} ${tunnel6_a_addr} ${tunnel6_b_addr} ${tunnel6_mask}
}

setup_vxlan_or_geneve() {
	type="${1}"
	a_addr="${2}"
	b_addr="${3}"
	opts="${4}"
	br_if_a="${5}"

	if [ "${type}" = "vxlan" ]; then
		opts="${opts} ttl 64 dstport 4789"
		opts_a="local ${a_addr}"
		opts_b="local ${b_addr}"
	else
		opts_a=""
		opts_b=""
	fi

	run_cmd ${ns_a} ip link add ${type}_a type ${type} id 1 ${opts_a} remote ${b_addr} ${opts} || return 1
	run_cmd ${ns_b} ip link add ${type}_b type ${type} id 1 ${opts_b} remote ${a_addr} ${opts}

	if [ -n "${br_if_a}" ]; then
		run_cmd ${ns_a} ip addr add ${tunnel4_a_addr}/${tunnel4_mask} dev ${br_if_a}
		run_cmd ${ns_a} ip addr add ${tunnel6_a_addr}/${tunnel6_mask} dev ${br_if_a}
		run_cmd ${ns_a} ip link set ${type}_a master ${br_if_a}
	else
		run_cmd ${ns_a} ip addr add ${tunnel4_a_addr}/${tunnel4_mask} dev ${type}_a
		run_cmd ${ns_a} ip addr add ${tunnel6_a_addr}/${tunnel6_mask} dev ${type}_a
	fi

	run_cmd ${ns_b} ip addr add ${tunnel4_b_addr}/${tunnel4_mask} dev ${type}_b
	run_cmd ${ns_b} ip addr add ${tunnel6_b_addr}/${tunnel6_mask} dev ${type}_b

	run_cmd ${ns_a} ip link set ${type}_a up
	run_cmd ${ns_b} ip link set ${type}_b up
}

setup_geneve4() {
	setup_vxlan_or_geneve geneve ${prefix4}.${a_r1}.1  ${prefix4}.${b_r1}.1  "df set"
}

setup_vxlan4() {
	setup_vxlan_or_geneve vxlan  ${prefix4}.${a_r1}.1  ${prefix4}.${b_r1}.1  "df set"
}

setup_geneve6() {
	setup_vxlan_or_geneve geneve ${prefix6}:${a_r1}::1 ${prefix6}:${b_r1}::1 ""
}

setup_vxlan6() {
	setup_vxlan_or_geneve vxlan  ${prefix6}:${a_r1}::1 ${prefix6}:${b_r1}::1 ""
}

setup_bridged_geneve4() {
	setup_vxlan_or_geneve geneve ${prefix4}.${a_r1}.1  ${prefix4}.${b_r1}.1  "df set" "br0"
}

setup_bridged_vxlan4() {
	setup_vxlan_or_geneve vxlan  ${prefix4}.${a_r1}.1  ${prefix4}.${b_r1}.1  "df set" "br0"
}

setup_bridged_geneve6() {
	setup_vxlan_or_geneve geneve ${prefix6}:${a_r1}::1 ${prefix6}:${b_r1}::1 "" "br0"
}

setup_bridged_vxlan6() {
	setup_vxlan_or_geneve vxlan  ${prefix6}:${a_r1}::1 ${prefix6}:${b_r1}::1 "" "br0"
}

setup_xfrm() {
	proto=${1}
	veth_a_addr="${2}"
	veth_b_addr="${3}"

	run_cmd ${ns_a} ip -${proto} xfrm state add src ${veth_a_addr} dst ${veth_b_addr} spi 0x1000 proto esp aead 'rfc4106(gcm(aes))' 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode tunnel || return 1
	run_cmd ${ns_a} ip -${proto} xfrm state add src ${veth_b_addr} dst ${veth_a_addr} spi 0x1001 proto esp aead 'rfc4106(gcm(aes))' 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode tunnel
	run_cmd ${ns_a} ip -${proto} xfrm policy add dir out mark 10 tmpl src ${veth_a_addr} dst ${veth_b_addr} proto esp mode tunnel
	run_cmd ${ns_a} ip -${proto} xfrm policy add dir in mark 10 tmpl src ${veth_b_addr} dst ${veth_a_addr} proto esp mode tunnel

	run_cmd ${ns_b} ip -${proto} xfrm state add src ${veth_a_addr} dst ${veth_b_addr} spi 0x1000 proto esp aead 'rfc4106(gcm(aes))' 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode tunnel
	run_cmd ${ns_b} ip -${proto} xfrm state add src ${veth_b_addr} dst ${veth_a_addr} spi 0x1001 proto esp aead 'rfc4106(gcm(aes))' 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode tunnel
	run_cmd ${ns_b} ip -${proto} xfrm policy add dir out mark 10 tmpl src ${veth_b_addr} dst ${veth_a_addr} proto esp mode tunnel
	run_cmd ${ns_b} ip -${proto} xfrm policy add dir in mark 10 tmpl src ${veth_a_addr} dst ${veth_b_addr} proto esp mode tunnel
}

setup_xfrm4() {
	setup_xfrm 4 ${veth4_a_addr} ${veth4_b_addr}
}

setup_xfrm6() {
	setup_xfrm 6 ${veth6_a_addr} ${veth6_b_addr}
}

setup_routing_old() {
	for i in ${routes}; do
		[ "${ns}" = "" ]	&& ns="${i}"		&& continue
		[ "${addr}" = "" ]	&& addr="${i}"		&& continue
		[ "${gw}" = "" ]	&& gw="${i}"

		ns_name="$(nsname ${ns})"

		ip -n ${ns_name} route add ${addr} via ${gw}

		ns=""; addr=""; gw=""
	done
}

setup_routing_new() {
	for i in ${nexthops}; do
		[ "${ns}" = "" ]	&& ns="${i}"		&& continue
		[ "${fam}" = "" ]	&& fam="${i}"		&& continue
		[ "${nhid}" = "" ]	&& nhid="${i}"		&& continue
		[ "${gw}" = "" ]	&& gw="${i}"		&& continue
		[ "${dev}" = "" ]	&& dev="${i}"

		ns_name="$(nsname ${ns})"

		ip -n ${ns_name} -${fam} nexthop add id ${nhid} via ${gw} dev ${dev}

		ns=""; fam=""; nhid=""; gw=""; dev=""

	done

	for i in ${routes_nh}; do
		[ "${ns}" = "" ]	&& ns="${i}"		&& continue
		[ "${fam}" = "" ]	&& fam="${i}"		&& continue
		[ "${addr}" = "" ]	&& addr="${i}"		&& continue
		[ "${nhid}" = "" ]	&& nhid="${i}"

		ns_name="$(nsname ${ns})"

		ip -n ${ns_name} -${fam} route add ${addr} nhid ${nhid}

		ns=""; fam=""; addr=""; nhid=""
	done
}

setup_routing() {
	for i in ${NS_R1} ${NS_R2}; do
		ip netns exec ${i} sysctl -q net/ipv4/ip_forward=1
		ip netns exec ${i} sysctl -q net/ipv6/conf/all/forwarding=1
	done

	for i in ${routing_addrs}; do
		[ "${ns}" = "" ]	&& ns="${i}"		&& continue
		[ "${peer}" = "" ]	&& peer="${i}"		&& continue
		[ "${segment}" = "" ]	&& segment="${i}"

		ns_name="$(nsname ${ns})"
		peer_name="$(nsname ${peer})"
		if="veth_${ns}-${peer}"
		ifpeer="veth_${peer}-${ns}"

		# Create veth links
		ip link add ${if} up netns ${ns_name} type veth peer name ${ifpeer} netns ${peer_name} || return 1
		ip -n ${peer_name} link set dev ${ifpeer} up

		# Add addresses
		ip -n ${ns_name}   addr add ${prefix4}.${segment}.1/24  dev ${if}
		ip -n ${ns_name}   addr add ${prefix6}:${segment}::1/64 dev ${if}

		ip -n ${peer_name} addr add ${prefix4}.${segment}.2/24  dev ${ifpeer}
		ip -n ${peer_name} addr add ${prefix6}:${segment}::2/64 dev ${ifpeer}

		ns=""; peer=""; segment=""
	done

	if [ "$USE_NH" = "yes" ]; then
		setup_routing_new
	else
		setup_routing_old
	fi

	return 0
}

setup_bridge() {
	run_cmd ${ns_a} ip link add br0 type bridge || return 2
	run_cmd ${ns_a} ip link set br0 up

	run_cmd ${ns_c} ip link add veth_C-A type veth peer name veth_A-C
	run_cmd ${ns_c} ip link set veth_A-C netns ns-A

	run_cmd ${ns_a} ip link set veth_A-C up
	run_cmd ${ns_c} ip link set veth_C-A up
	run_cmd ${ns_c} ip addr add ${veth4_c_addr}/${veth4_mask} dev veth_C-A
	run_cmd ${ns_c} ip addr add ${veth6_c_addr}/${veth6_mask} dev veth_C-A
	run_cmd ${ns_a} ip link set veth_A-C master br0
}

setup_ovs_vxlan_or_geneve() {
	type="${1}"
	a_addr="${2}"
	b_addr="${3}"

	if [ "${type}" = "vxlan" ]; then
		opts="${opts} ttl 64 dstport 4789"
		opts_b="local ${b_addr}"
	fi

	run_cmd ovs-vsctl add-port ovs_br0 ${type}_a -- \
		set interface ${type}_a type=${type} \
		options:remote_ip=${b_addr} options:key=1 options:csum=true || return 1

	run_cmd ${ns_b} ip link add ${type}_b type ${type} id 1 ${opts_b} remote ${a_addr} ${opts} || return 1

	run_cmd ${ns_b} ip addr add ${tunnel4_b_addr}/${tunnel4_mask} dev ${type}_b
	run_cmd ${ns_b} ip addr add ${tunnel6_b_addr}/${tunnel6_mask} dev ${type}_b

	run_cmd ${ns_b} ip link set ${type}_b up
}

setup_ovs_geneve4() {
	setup_ovs_vxlan_or_geneve geneve ${prefix4}.${a_r1}.1  ${prefix4}.${b_r1}.1
}

setup_ovs_vxlan4() {
	setup_ovs_vxlan_or_geneve vxlan  ${prefix4}.${a_r1}.1  ${prefix4}.${b_r1}.1
}

setup_ovs_geneve6() {
	setup_ovs_vxlan_or_geneve geneve ${prefix6}:${a_r1}::1 ${prefix6}:${b_r1}::1
}

setup_ovs_vxlan6() {
	setup_ovs_vxlan_or_geneve vxlan  ${prefix6}:${a_r1}::1 ${prefix6}:${b_r1}::1
}

setup_ovs_bridge() {
	run_cmd ovs-vsctl add-br ovs_br0 || return 2
	run_cmd ip link set ovs_br0 up

	run_cmd ${ns_c} ip link add veth_C-A type veth peer name veth_A-C
	run_cmd ${ns_c} ip link set veth_A-C netns 1

	run_cmd         ip link set veth_A-C up
	run_cmd ${ns_c} ip link set veth_C-A up
	run_cmd ${ns_c} ip addr add ${veth4_c_addr}/${veth4_mask} dev veth_C-A
	run_cmd ${ns_c} ip addr add ${veth6_c_addr}/${veth6_mask} dev veth_C-A
	run_cmd ovs-vsctl add-port ovs_br0 veth_A-C

	# Move veth_A-R1 to init
	run_cmd ${ns_a} ip link set veth_A-R1 netns 1
	run_cmd ip addr add ${prefix4}.${a_r1}.1/${veth4_mask} dev veth_A-R1
	run_cmd ip addr add ${prefix6}:${a_r1}::1/${veth6_mask} dev veth_A-R1
	run_cmd ip link set veth_A-R1 up
	run_cmd ip route add ${prefix4}.${b_r1}.1 via ${prefix4}.${a_r1}.2
	run_cmd ip route add ${prefix6}:${b_r1}::1 via ${prefix6}:${a_r1}::2
}

setup() {
	[ "$(id -u)" -ne 0 ] && echo "  need to run as root" && return $ksft_skip

	for arg do
		eval setup_${arg} || { echo "  ${arg} not supported"; return 1; }
	done
}

trace() {
	[ $TRACING -eq 0 ] && return

	for arg do
		[ "${ns_cmd}" = "" ] && ns_cmd="${arg}" && continue
		${ns_cmd} tcpdump --immediate-mode -s 0 -i "${arg}" -w "${name}_${arg}.pcap" 2> /dev/null &
		tcpdump_pids="${tcpdump_pids} $!"
		ns_cmd=
	done
	sleep 1
}

cleanup() {
	for pid in ${tcpdump_pids}; do
		kill ${pid}
	done
	tcpdump_pids=

	for n in ${NS_A} ${NS_B} ${NS_C} ${NS_R1} ${NS_R2}; do
		ip netns del ${n} 2> /dev/null
	done

	ip link del veth_A-C			2>/dev/null
	ip link del veth_A-R1			2>/dev/null
	ovs-vsctl --if-exists del-port vxlan_a	2>/dev/null
	ovs-vsctl --if-exists del-br ovs_br0	2>/dev/null
}

mtu() {
	ns_cmd="${1}"
	dev="${2}"
	mtu="${3}"

	${ns_cmd} ip link set dev ${dev} mtu ${mtu}
}

mtu_parse() {
	input="${1}"

	next=0
	for i in ${input}; do
		[ ${next} -eq 1 -a "${i}" = "lock" ] && next=2 && continue
		[ ${next} -eq 1 ] && echo "${i}" && return
		[ ${next} -eq 2 ] && echo "lock ${i}" && return
		[ "${i}" = "mtu" ] && next=1
	done
}

link_get() {
	ns_cmd="${1}"
	name="${2}"

	${ns_cmd} ip link show dev "${name}"
}

link_get_mtu() {
	ns_cmd="${1}"
	name="${2}"

	mtu_parse "$(link_get "${ns_cmd}" ${name})"
}

route_get_dst_exception() {
	ns_cmd="${1}"
	dst="${2}"

	${ns_cmd} ip route get "${dst}"
}

route_get_dst_pmtu_from_exception() {
	ns_cmd="${1}"
	dst="${2}"

	mtu_parse "$(route_get_dst_exception "${ns_cmd}" ${dst})"
}

check_pmtu_value() {
	expected="${1}"
	value="${2}"
	event="${3}"

	[ "${expected}" = "any" ] && [ -n "${value}" ] && return 0
	[ "${value}" = "${expected}" ] && return 0
	[ -z "${value}" ] &&    err "  PMTU exception wasn't created after ${event}" && return 1
	[ -z "${expected}" ] && err "  PMTU exception shouldn't exist after ${event}" && return 1
	err "  found PMTU exception with incorrect MTU ${value}, expected ${expected}, after ${event}"
	return 1
}

test_pmtu_ipvX() {
	family=${1}

	setup namespaces routing || return 2
	trace "${ns_a}"  veth_A-R1    "${ns_r1}" veth_R1-A \
	      "${ns_r1}" veth_R1-B    "${ns_b}"  veth_B-R1 \
	      "${ns_a}"  veth_A-R2    "${ns_r2}" veth_R2-A \
	      "${ns_r2}" veth_R2-B    "${ns_b}"  veth_B-R2

	if [ ${family} -eq 4 ]; then
		ping=ping
		dst1="${prefix4}.${b_r1}.1"
		dst2="${prefix4}.${b_r2}.1"
	else
		ping=${ping6}
		dst1="${prefix6}:${b_r1}::1"
		dst2="${prefix6}:${b_r2}::1"
	fi

	# Set up initial MTU values
	mtu "${ns_a}"  veth_A-R1 2000
	mtu "${ns_r1}" veth_R1-A 2000
	mtu "${ns_r1}" veth_R1-B 1400
	mtu "${ns_b}"  veth_B-R1 1400

	mtu "${ns_a}"  veth_A-R2 2000
	mtu "${ns_r2}" veth_R2-A 2000
	mtu "${ns_r2}" veth_R2-B 1500
	mtu "${ns_b}"  veth_B-R2 1500

	# Create route exceptions
	run_cmd ${ns_a} ${ping} -q -M want -i 0.1 -w 1 -s 1800 ${dst1}
	run_cmd ${ns_a} ${ping} -q -M want -i 0.1 -w 1 -s 1800 ${dst2}

	# Check that exceptions have been created with the correct PMTU
	pmtu_1="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst1})"
	check_pmtu_value "1400" "${pmtu_1}" "exceeding MTU" || return 1
	pmtu_2="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst2})"
	check_pmtu_value "1500" "${pmtu_2}" "exceeding MTU" || return 1

	# Decrease local MTU below PMTU, check for PMTU decrease in route exception
	mtu "${ns_a}"  veth_A-R1 1300
	mtu "${ns_r1}" veth_R1-A 1300
	pmtu_1="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst1})"
	check_pmtu_value "1300" "${pmtu_1}" "decreasing local MTU" || return 1
	# Second exception shouldn't be modified
	pmtu_2="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst2})"
	check_pmtu_value "1500" "${pmtu_2}" "changing local MTU on a link not on this path" || return 1

	# Increase MTU, check for PMTU increase in route exception
	mtu "${ns_a}"  veth_A-R1 1700
	mtu "${ns_r1}" veth_R1-A 1700
	pmtu_1="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst1})"
	check_pmtu_value "1700" "${pmtu_1}" "increasing local MTU" || return 1
	# Second exception shouldn't be modified
	pmtu_2="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst2})"
	check_pmtu_value "1500" "${pmtu_2}" "changing local MTU on a link not on this path" || return 1

	# Skip PMTU locking tests for IPv6
	[ $family -eq 6 ] && return 0

	# Decrease remote MTU on path via R2, get new exception
	mtu "${ns_r2}" veth_R2-B 400
	mtu "${ns_b}"  veth_B-R2 400
	run_cmd ${ns_a} ${ping} -q -M want -i 0.1 -w 1 -s 1400 ${dst2}
	pmtu_2="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst2})"
	check_pmtu_value "lock 552" "${pmtu_2}" "exceeding MTU, with MTU < min_pmtu" || return 1

	# Decrease local MTU below PMTU
	mtu "${ns_a}"  veth_A-R2 500
	mtu "${ns_r2}" veth_R2-A 500
	pmtu_2="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst2})"
	check_pmtu_value "500" "${pmtu_2}" "decreasing local MTU" || return 1

	# Increase local MTU
	mtu "${ns_a}"  veth_A-R2 1500
	mtu "${ns_r2}" veth_R2-A 1500
	pmtu_2="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst2})"
	check_pmtu_value "1500" "${pmtu_2}" "increasing local MTU" || return 1

	# Get new exception
	run_cmd ${ns_a} ${ping} -q -M want -i 0.1 -w 1 -s 1400 ${dst2}
	pmtu_2="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst2})"
	check_pmtu_value "lock 552" "${pmtu_2}" "exceeding MTU, with MTU < min_pmtu" || return 1
}

test_pmtu_ipv4_exception() {
	test_pmtu_ipvX 4
}

test_pmtu_ipv6_exception() {
	test_pmtu_ipvX 6
}

test_pmtu_ipvX_over_vxlanY_or_geneveY_exception() {
	type=${1}
	family=${2}
	outer_family=${3}
	ll_mtu=4000

	if [ ${outer_family} -eq 4 ]; then
		setup namespaces routing ${type}4 || return 2
		#                      IPv4 header   UDP header   VXLAN/GENEVE header   Ethernet header
		exp_mtu=$((${ll_mtu} - 20          - 8          - 8                   - 14))
	else
		setup namespaces routing ${type}6 || return 2
		#                      IPv6 header   UDP header   VXLAN/GENEVE header   Ethernet header
		exp_mtu=$((${ll_mtu} - 40          - 8          - 8                   - 14))
	fi

	trace "${ns_a}" ${type}_a    "${ns_b}"  ${type}_b \
	      "${ns_a}" veth_A-R1    "${ns_r1}" veth_R1-A \
	      "${ns_b}" veth_B-R1    "${ns_r1}" veth_R1-B

	if [ ${family} -eq 4 ]; then
		ping=ping
		dst=${tunnel4_b_addr}
	else
		ping=${ping6}
		dst=${tunnel6_b_addr}
	fi

	# Create route exception by exceeding link layer MTU
	mtu "${ns_a}"  veth_A-R1 $((${ll_mtu} + 1000))
	mtu "${ns_r1}" veth_R1-A $((${ll_mtu} + 1000))
	mtu "${ns_b}"  veth_B-R1 ${ll_mtu}
	mtu "${ns_r1}" veth_R1-B ${ll_mtu}

	mtu "${ns_a}" ${type}_a $((${ll_mtu} + 1000))
	mtu "${ns_b}" ${type}_b $((${ll_mtu} + 1000))
	run_cmd ${ns_a} ${ping} -q -M want -i 0.1 -w 1 -s $((${ll_mtu} + 500)) ${dst}

	# Check that exception was created
	pmtu="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst})"
	check_pmtu_value ${exp_mtu} "${pmtu}" "exceeding link layer MTU on ${type} interface"
}

test_pmtu_ipv4_vxlan4_exception() {
	test_pmtu_ipvX_over_vxlanY_or_geneveY_exception vxlan  4 4
}

test_pmtu_ipv6_vxlan4_exception() {
	test_pmtu_ipvX_over_vxlanY_or_geneveY_exception vxlan  6 4
}

test_pmtu_ipv4_geneve4_exception() {
	test_pmtu_ipvX_over_vxlanY_or_geneveY_exception geneve 4 4
}

test_pmtu_ipv6_geneve4_exception() {
	test_pmtu_ipvX_over_vxlanY_or_geneveY_exception geneve 6 4
}

test_pmtu_ipv4_vxlan6_exception() {
	test_pmtu_ipvX_over_vxlanY_or_geneveY_exception vxlan  4 6
}

test_pmtu_ipv6_vxlan6_exception() {
	test_pmtu_ipvX_over_vxlanY_or_geneveY_exception vxlan  6 6
}

test_pmtu_ipv4_geneve6_exception() {
	test_pmtu_ipvX_over_vxlanY_or_geneveY_exception geneve 4 6
}

test_pmtu_ipv6_geneve6_exception() {
	test_pmtu_ipvX_over_vxlanY_or_geneveY_exception geneve 6 6
}

test_pmtu_ipvX_over_bridged_vxlanY_or_geneveY_exception() {
	type=${1}
	family=${2}
	outer_family=${3}
	ll_mtu=4000

	if [ ${outer_family} -eq 4 ]; then
		setup namespaces routing bridge bridged_${type}4 || return 2
		#                      IPv4 header   UDP header   VXLAN/GENEVE header   Ethernet header
		exp_mtu=$((${ll_mtu} - 20          - 8          - 8                   - 14))
	else
		setup namespaces routing bridge bridged_${type}6 || return 2
		#                      IPv6 header   UDP header   VXLAN/GENEVE header   Ethernet header
		exp_mtu=$((${ll_mtu} - 40          - 8          - 8                   - 14))
	fi

	trace "${ns_a}" ${type}_a    "${ns_b}"  ${type}_b \
	      "${ns_a}" veth_A-R1    "${ns_r1}" veth_R1-A \
	      "${ns_b}" veth_B-R1    "${ns_r1}" veth_R1-B \
	      "${ns_a}" br0          "${ns_a}"  veth-A-C  \
	      "${ns_c}" veth_C-A

	if [ ${family} -eq 4 ]; then
		ping=ping
		dst=${tunnel4_b_addr}
	else
		ping=${ping6}
		dst=${tunnel6_b_addr}
	fi

	# Create route exception by exceeding link layer MTU
	mtu "${ns_a}"  veth_A-R1 $((${ll_mtu} + 1000))
	mtu "${ns_a}"  br0       $((${ll_mtu} + 1000))
	mtu "${ns_a}"  veth_A-C  $((${ll_mtu} + 1000))
	mtu "${ns_c}"  veth_C-A  $((${ll_mtu} + 1000))
	mtu "${ns_r1}" veth_R1-A $((${ll_mtu} + 1000))
	mtu "${ns_b}"  veth_B-R1 ${ll_mtu}
	mtu "${ns_r1}" veth_R1-B ${ll_mtu}

	mtu "${ns_a}" ${type}_a $((${ll_mtu} + 1000))
	mtu "${ns_b}" ${type}_b $((${ll_mtu} + 1000))

	run_cmd ${ns_c} ${ping} -q -M want -i 0.1 -c 10 -s $((${ll_mtu} + 500)) ${dst} || return 1
	run_cmd ${ns_a} ${ping} -q -M want -i 0.1 -w 1  -s $((${ll_mtu} + 500)) ${dst} || return 1

	# Check that exceptions were created
	pmtu="$(route_get_dst_pmtu_from_exception "${ns_c}" ${dst})"
	check_pmtu_value ${exp_mtu} "${pmtu}" "exceeding link layer MTU on bridged ${type} interface"
	pmtu="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst})"
	check_pmtu_value ${exp_mtu} "${pmtu}" "exceeding link layer MTU on locally bridged ${type} interface"
}

test_pmtu_ipv4_br_vxlan4_exception() {
	test_pmtu_ipvX_over_bridged_vxlanY_or_geneveY_exception vxlan  4 4
}

test_pmtu_ipv6_br_vxlan4_exception() {
	test_pmtu_ipvX_over_bridged_vxlanY_or_geneveY_exception vxlan  6 4
}

test_pmtu_ipv4_br_geneve4_exception() {
	test_pmtu_ipvX_over_bridged_vxlanY_or_geneveY_exception geneve 4 4
}

test_pmtu_ipv6_br_geneve4_exception() {
	test_pmtu_ipvX_over_bridged_vxlanY_or_geneveY_exception geneve 6 4
}

test_pmtu_ipv4_br_vxlan6_exception() {
	test_pmtu_ipvX_over_bridged_vxlanY_or_geneveY_exception vxlan  4 6
}

test_pmtu_ipv6_br_vxlan6_exception() {
	test_pmtu_ipvX_over_bridged_vxlanY_or_geneveY_exception vxlan  6 6
}

test_pmtu_ipv4_br_geneve6_exception() {
	test_pmtu_ipvX_over_bridged_vxlanY_or_geneveY_exception geneve 4 6
}

test_pmtu_ipv6_br_geneve6_exception() {
	test_pmtu_ipvX_over_bridged_vxlanY_or_geneveY_exception geneve 6 6
}

test_pmtu_ipvX_over_ovs_vxlanY_or_geneveY_exception() {
	type=${1}
	family=${2}
	outer_family=${3}
	ll_mtu=4000

	if [ ${outer_family} -eq 4 ]; then
		setup namespaces routing ovs_bridge ovs_${type}4 || return 2
		#                      IPv4 header   UDP header   VXLAN/GENEVE header   Ethernet header
		exp_mtu=$((${ll_mtu} - 20          - 8          - 8                   - 14))
	else
		setup namespaces routing ovs_bridge ovs_${type}6 || return 2
		#                      IPv6 header   UDP header   VXLAN/GENEVE header   Ethernet header
		exp_mtu=$((${ll_mtu} - 40          - 8          - 8                   - 14))
	fi

	if [ "${type}" = "vxlan" ]; then
		tun_a="vxlan_sys_4789"
	elif [ "${type}" = "geneve" ]; then
		tun_a="genev_sys_6081"
	fi

	trace ""        "${tun_a}"  "${ns_b}"  ${type}_b \
	      ""        veth_A-R1   "${ns_r1}" veth_R1-A \
	      "${ns_b}" veth_B-R1   "${ns_r1}" veth_R1-B \
	      ""        ovs_br0     ""         veth-A-C  \
	      "${ns_c}" veth_C-A

	if [ ${family} -eq 4 ]; then
		ping=ping
		dst=${tunnel4_b_addr}
	else
		ping=${ping6}
		dst=${tunnel6_b_addr}
	fi

	# Create route exception by exceeding link layer MTU
	mtu ""         veth_A-R1 $((${ll_mtu} + 1000))
	mtu ""         ovs_br0   $((${ll_mtu} + 1000))
	mtu ""         veth_A-C  $((${ll_mtu} + 1000))
	mtu "${ns_c}"  veth_C-A  $((${ll_mtu} + 1000))
	mtu "${ns_r1}" veth_R1-A $((${ll_mtu} + 1000))
	mtu "${ns_b}"  veth_B-R1 ${ll_mtu}
	mtu "${ns_r1}" veth_R1-B ${ll_mtu}

	mtu ""        ${tun_a}  $((${ll_mtu} + 1000))
	mtu "${ns_b}" ${type}_b $((${ll_mtu} + 1000))

	run_cmd ${ns_c} ${ping} -q -M want -i 0.1 -c 20 -s $((${ll_mtu} + 500)) ${dst} || return 1

	# Check that exceptions were created
	pmtu="$(route_get_dst_pmtu_from_exception "${ns_c}" ${dst})"
	check_pmtu_value ${exp_mtu} "${pmtu}" "exceeding link layer MTU on Open vSwitch ${type} interface"
}

test_pmtu_ipv4_ovs_vxlan4_exception() {
	test_pmtu_ipvX_over_ovs_vxlanY_or_geneveY_exception vxlan  4 4
}

test_pmtu_ipv6_ovs_vxlan4_exception() {
	test_pmtu_ipvX_over_ovs_vxlanY_or_geneveY_exception vxlan  6 4
}

test_pmtu_ipv4_ovs_geneve4_exception() {
	test_pmtu_ipvX_over_ovs_vxlanY_or_geneveY_exception geneve 4 4
}

test_pmtu_ipv6_ovs_geneve4_exception() {
	test_pmtu_ipvX_over_ovs_vxlanY_or_geneveY_exception geneve 6 4
}

test_pmtu_ipv4_ovs_vxlan6_exception() {
	test_pmtu_ipvX_over_ovs_vxlanY_or_geneveY_exception vxlan  4 6
}

test_pmtu_ipv6_ovs_vxlan6_exception() {
	test_pmtu_ipvX_over_ovs_vxlanY_or_geneveY_exception vxlan  6 6
}

test_pmtu_ipv4_ovs_geneve6_exception() {
	test_pmtu_ipvX_over_ovs_vxlanY_or_geneveY_exception geneve 4 6
}

test_pmtu_ipv6_ovs_geneve6_exception() {
	test_pmtu_ipvX_over_ovs_vxlanY_or_geneveY_exception geneve 6 6
}

test_pmtu_ipvX_over_fouY_or_gueY() {
	inner_family=${1}
	outer_family=${2}
	encap=${3}
	ll_mtu=4000

	setup namespaces routing ${encap}${outer_family}${inner_family} || return 2
	trace "${ns_a}" ${encap}_a   "${ns_b}"  ${encap}_b \
	      "${ns_a}" veth_A-R1    "${ns_r1}" veth_R1-A \
	      "${ns_b}" veth_B-R1    "${ns_r1}" veth_R1-B

	if [ ${inner_family} -eq 4 ]; then
		ping=ping
		dst=${tunnel4_b_addr}
	else
		ping=${ping6}
		dst=${tunnel6_b_addr}
	fi

	if [ "${encap}" = "gue" ]; then
		encap_overhead=4
	else
		encap_overhead=0
	fi

	if [ ${outer_family} -eq 4 ]; then
		#                      IPv4 header   UDP header
		exp_mtu=$((${ll_mtu} - 20          - 8         - ${encap_overhead}))
	else
		#                      IPv6 header   Option 4   UDP header
		exp_mtu=$((${ll_mtu} - 40          - 8        - 8       - ${encap_overhead}))
	fi

	# Create route exception by exceeding link layer MTU
	mtu "${ns_a}"  veth_A-R1 $((${ll_mtu} + 1000))
	mtu "${ns_r1}" veth_R1-A $((${ll_mtu} + 1000))
	mtu "${ns_b}"  veth_B-R1 ${ll_mtu}
	mtu "${ns_r1}" veth_R1-B ${ll_mtu}

	mtu "${ns_a}" ${encap}_a $((${ll_mtu} + 1000))
	mtu "${ns_b}" ${encap}_b $((${ll_mtu} + 1000))
	run_cmd ${ns_a} ${ping} -q -M want -i 0.1 -w 1 -s $((${ll_mtu} + 500)) ${dst}

	# Check that exception was created
	pmtu="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst})"
	check_pmtu_value ${exp_mtu} "${pmtu}" "exceeding link layer MTU on ${encap} interface"
}

test_pmtu_ipv4_fou4_exception() {
	test_pmtu_ipvX_over_fouY_or_gueY 4 4 fou
}

test_pmtu_ipv6_fou4_exception() {
	test_pmtu_ipvX_over_fouY_or_gueY 6 4 fou
}

test_pmtu_ipv4_fou6_exception() {
	test_pmtu_ipvX_over_fouY_or_gueY 4 6 fou
}

test_pmtu_ipv6_fou6_exception() {
	test_pmtu_ipvX_over_fouY_or_gueY 6 6 fou
}

test_pmtu_ipv4_gue4_exception() {
	test_pmtu_ipvX_over_fouY_or_gueY 4 4 gue
}

test_pmtu_ipv6_gue4_exception() {
	test_pmtu_ipvX_over_fouY_or_gueY 6 4 gue
}

test_pmtu_ipv4_gue6_exception() {
	test_pmtu_ipvX_over_fouY_or_gueY 4 6 gue
}

test_pmtu_ipv6_gue6_exception() {
	test_pmtu_ipvX_over_fouY_or_gueY 6 6 gue
}

test_pmtu_ipvX_over_ipvY_exception() {
	inner=${1}
	outer=${2}
	ll_mtu=4000

	setup namespaces routing ip${inner}ip${outer} || return 2

	trace "${ns_a}" ip_a         "${ns_b}"  ip_b  \
	      "${ns_a}" veth_A-R1    "${ns_r1}" veth_R1-A \
	      "${ns_b}" veth_B-R1    "${ns_r1}" veth_R1-B

	if [ ${inner} -eq 4 ]; then
		ping=ping
		dst=${tunnel4_b_addr}
	else
		ping=${ping6}
		dst=${tunnel6_b_addr}
	fi

	if [ ${outer} -eq 4 ]; then
		#                      IPv4 header
		exp_mtu=$((${ll_mtu} - 20))
	else
		#                      IPv6 header   Option 4
		exp_mtu=$((${ll_mtu} - 40          - 8))
	fi

	# Create route exception by exceeding link layer MTU
	mtu "${ns_a}"  veth_A-R1 $((${ll_mtu} + 1000))
	mtu "${ns_r1}" veth_R1-A $((${ll_mtu} + 1000))
	mtu "${ns_b}"  veth_B-R1 ${ll_mtu}
	mtu "${ns_r1}" veth_R1-B ${ll_mtu}

	mtu "${ns_a}" ip_a $((${ll_mtu} + 1000)) || return
	mtu "${ns_b}" ip_b $((${ll_mtu} + 1000)) || return
	run_cmd ${ns_a} ${ping} -q -M want -i 0.1 -w 1 -s $((${ll_mtu} + 500)) ${dst}

	# Check that exception was created
	pmtu="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst})"
	check_pmtu_value ${exp_mtu} "${pmtu}" "exceeding link layer MTU on ip${inner}ip${outer} interface"
}

test_pmtu_ipv4_ipv4_exception() {
	test_pmtu_ipvX_over_ipvY_exception 4 4
}

test_pmtu_ipv6_ipv4_exception() {
	test_pmtu_ipvX_over_ipvY_exception 6 4
}

test_pmtu_ipv4_ipv6_exception() {
	test_pmtu_ipvX_over_ipvY_exception 4 6
}

test_pmtu_ipv6_ipv6_exception() {
	test_pmtu_ipvX_over_ipvY_exception 6 6
}

test_pmtu_vti4_exception() {
	setup namespaces veth vti4 xfrm4 || return 2
	trace "${ns_a}" veth_a    "${ns_b}" veth_b \
	      "${ns_a}" vti4_a    "${ns_b}" vti4_b

	veth_mtu=1500
	vti_mtu=$((veth_mtu - 20))

	#                                SPI   SN   IV  ICV   pad length   next header
	esp_payload_rfc4106=$((vti_mtu - 4   - 4  - 8 - 16  - 1          - 1))
	ping_payload=$((esp_payload_rfc4106 - 28))

	mtu "${ns_a}" veth_a ${veth_mtu}
	mtu "${ns_b}" veth_b ${veth_mtu}
	mtu "${ns_a}" vti4_a ${vti_mtu}
	mtu "${ns_b}" vti4_b ${vti_mtu}

	# Send DF packet without exceeding link layer MTU, check that no
	# exception is created
	run_cmd ${ns_a} ping -q -M want -i 0.1 -w 1 -s ${ping_payload} ${tunnel4_b_addr}
	pmtu="$(route_get_dst_pmtu_from_exception "${ns_a}" ${tunnel4_b_addr})"
	check_pmtu_value "" "${pmtu}" "sending packet smaller than PMTU (IP payload length ${esp_payload_rfc4106})" || return 1

	# Now exceed link layer MTU by one byte, check that exception is created
	# with the right PMTU value
	run_cmd ${ns_a} ping -q -M want -i 0.1 -w 1 -s $((ping_payload + 1)) ${tunnel4_b_addr}
	pmtu="$(route_get_dst_pmtu_from_exception "${ns_a}" ${tunnel4_b_addr})"
	check_pmtu_value "${esp_payload_rfc4106}" "${pmtu}" "exceeding PMTU (IP payload length $((esp_payload_rfc4106 + 1)))"
}

test_pmtu_vti6_exception() {
	setup namespaces veth vti6 xfrm6 || return 2
	trace "${ns_a}" veth_a    "${ns_b}" veth_b \
	      "${ns_a}" vti6_a    "${ns_b}" vti6_b
	fail=0

	# Create route exception by exceeding link layer MTU
	mtu "${ns_a}" veth_a 4000
	mtu "${ns_b}" veth_b 4000
	mtu "${ns_a}" vti6_a 5000
	mtu "${ns_b}" vti6_b 5000
	run_cmd ${ns_a} ${ping6} -q -i 0.1 -w 1 -s 60000 ${tunnel6_b_addr}

	# Check that exception was created
	pmtu="$(route_get_dst_pmtu_from_exception "${ns_a}" ${tunnel6_b_addr})"
	check_pmtu_value any "${pmtu}" "creating tunnel exceeding link layer MTU" || return 1

	# Decrease tunnel MTU, check for PMTU decrease in route exception
	mtu "${ns_a}" vti6_a 3000
	pmtu="$(route_get_dst_pmtu_from_exception "${ns_a}" ${tunnel6_b_addr})"
	check_pmtu_value "3000" "${pmtu}" "decreasing tunnel MTU" || fail=1

	# Increase tunnel MTU, check for PMTU increase in route exception
	mtu "${ns_a}" vti6_a 9000
	pmtu="$(route_get_dst_pmtu_from_exception "${ns_a}" ${tunnel6_b_addr})"
	check_pmtu_value "9000" "${pmtu}" "increasing tunnel MTU" || fail=1

	return ${fail}
}

test_pmtu_vti4_default_mtu() {
	setup namespaces veth vti4 || return 2

	# Check that MTU of vti device is MTU of veth minus IPv4 header length
	veth_mtu="$(link_get_mtu "${ns_a}" veth_a)"
	vti4_mtu="$(link_get_mtu "${ns_a}" vti4_a)"
	if [ $((veth_mtu - vti4_mtu)) -ne 20 ]; then
		err "  vti MTU ${vti4_mtu} is not veth MTU ${veth_mtu} minus IPv4 header length"
		return 1
	fi
}

test_pmtu_vti6_default_mtu() {
	setup namespaces veth vti6 || return 2

	# Check that MTU of vti device is MTU of veth minus IPv6 header length
	veth_mtu="$(link_get_mtu "${ns_a}" veth_a)"
	vti6_mtu="$(link_get_mtu "${ns_a}" vti6_a)"
	if [ $((veth_mtu - vti6_mtu)) -ne 40 ]; then
		err "  vti MTU ${vti6_mtu} is not veth MTU ${veth_mtu} minus IPv6 header length"
		return 1
	fi
}

test_pmtu_vti4_link_add_mtu() {
	setup namespaces || return 2

	run_cmd ${ns_a} ip link add vti4_a type vti local ${veth4_a_addr} remote ${veth4_b_addr} key 10
	[ $? -ne 0 ] && err "  vti not supported" && return 2
	run_cmd ${ns_a} ip link del vti4_a

	fail=0

	min=68
	max=$((65535 - 20))
	# Check invalid values first
	for v in $((min - 1)) $((max + 1)); do
		run_cmd ${ns_a} ip link add vti4_a mtu ${v} type vti local ${veth4_a_addr} remote ${veth4_b_addr} key 10
		# This can fail, or MTU can be adjusted to a proper value
		[ $? -ne 0 ] && continue
		mtu="$(link_get_mtu "${ns_a}" vti4_a)"
		if [ ${mtu} -lt ${min} -o ${mtu} -gt ${max} ]; then
			err "  vti tunnel created with invalid MTU ${mtu}"
			fail=1
		fi
		run_cmd ${ns_a} ip link del vti4_a
	done

	# Now check valid values
	for v in ${min} 1300 ${max}; do
		run_cmd ${ns_a} ip link add vti4_a mtu ${v} type vti local ${veth4_a_addr} remote ${veth4_b_addr} key 10
		mtu="$(link_get_mtu "${ns_a}" vti4_a)"
		run_cmd ${ns_a} ip link del vti4_a
		if [ "${mtu}" != "${v}" ]; then
			err "  vti MTU ${mtu} doesn't match configured value ${v}"
			fail=1
		fi
	done

	return ${fail}
}

test_pmtu_vti6_link_add_mtu() {
	setup namespaces || return 2

	run_cmd ${ns_a} ip link add vti6_a type vti6 local ${veth6_a_addr} remote ${veth6_b_addr} key 10
	[ $? -ne 0 ] && err "  vti6 not supported" && return 2
	run_cmd ${ns_a} ip link del vti6_a

	fail=0

	min=68			# vti6 can carry IPv4 packets too
	max=$((65535 - 40))
	# Check invalid values first
	for v in $((min - 1)) $((max + 1)); do
		run_cmd ${ns_a} ip link add vti6_a mtu ${v} type vti6 local ${veth6_a_addr} remote ${veth6_b_addr} key 10
		# This can fail, or MTU can be adjusted to a proper value
		[ $? -ne 0 ] && continue
		mtu="$(link_get_mtu "${ns_a}" vti6_a)"
		if [ ${mtu} -lt ${min} -o ${mtu} -gt ${max} ]; then
			err "  vti6 tunnel created with invalid MTU ${v}"
			fail=1
		fi
		run_cmd ${ns_a} ip link del vti6_a
	done

	# Now check valid values
	for v in 68 1280 1300 $((65535 - 40)); do
		run_cmd ${ns_a} ip link add vti6_a mtu ${v} type vti6 local ${veth6_a_addr} remote ${veth6_b_addr} key 10
		mtu="$(link_get_mtu "${ns_a}" vti6_a)"
		run_cmd ${ns_a} ip link del vti6_a
		if [ "${mtu}" != "${v}" ]; then
			err "  vti6 MTU ${mtu} doesn't match configured value ${v}"
			fail=1
		fi
	done

	return ${fail}
}

test_pmtu_vti6_link_change_mtu() {
	setup namespaces || return 2

	run_cmd ${ns_a} ip link add dummy0 mtu 1500 type dummy
	[ $? -ne 0 ] && err "  dummy not supported" && return 2
	run_cmd ${ns_a} ip link add dummy1 mtu 3000 type dummy
	run_cmd ${ns_a} ip link set dummy0 up
	run_cmd ${ns_a} ip link set dummy1 up

	run_cmd ${ns_a} ip addr add ${dummy6_0_prefix}1/${dummy6_mask} dev dummy0
	run_cmd ${ns_a} ip addr add ${dummy6_1_prefix}1/${dummy6_mask} dev dummy1

	fail=0

	# Create vti6 interface bound to device, passing MTU, check it
	run_cmd ${ns_a} ip link add vti6_a mtu 1300 type vti6 remote ${dummy6_0_prefix}2 local ${dummy6_0_prefix}1
	mtu="$(link_get_mtu "${ns_a}" vti6_a)"
	if [ ${mtu} -ne 1300 ]; then
		err "  vti6 MTU ${mtu} doesn't match configured value 1300"
		fail=1
	fi

	# Move to another device with different MTU, without passing MTU, check
	# MTU is adjusted
	run_cmd ${ns_a} ip link set vti6_a type vti6 remote ${dummy6_1_prefix}2 local ${dummy6_1_prefix}1
	mtu="$(link_get_mtu "${ns_a}" vti6_a)"
	if [ ${mtu} -ne $((3000 - 40)) ]; then
		err "  vti MTU ${mtu} is not dummy MTU 3000 minus IPv6 header length"
		fail=1
	fi

	# Move it back, passing MTU, check MTU is not overridden
	run_cmd ${ns_a} ip link set vti6_a mtu 1280 type vti6 remote ${dummy6_0_prefix}2 local ${dummy6_0_prefix}1
	mtu="$(link_get_mtu "${ns_a}" vti6_a)"
	if [ ${mtu} -ne 1280 ]; then
		err "  vti6 MTU ${mtu} doesn't match configured value 1280"
		fail=1
	fi

	return ${fail}
}

check_command() {
	cmd=${1}

	if ! which ${cmd} > /dev/null 2>&1; then
		err "  missing required command: '${cmd}'"
		return 1
	fi
	return 0
}

test_cleanup_vxlanX_exception() {
	outer="${1}"
	encap="vxlan"
	ll_mtu=4000

	check_command taskset || return 2
	cpu_list=$(grep -m 2 processor /proc/cpuinfo | cut -d ' ' -f 2)

	setup namespaces routing ${encap}${outer} || return 2
	trace "${ns_a}" ${encap}_a   "${ns_b}"  ${encap}_b \
	      "${ns_a}" veth_A-R1    "${ns_r1}" veth_R1-A \
	      "${ns_b}" veth_B-R1    "${ns_r1}" veth_R1-B

	# Create route exception by exceeding link layer MTU
	mtu "${ns_a}"  veth_A-R1 $((${ll_mtu} + 1000))
	mtu "${ns_r1}" veth_R1-A $((${ll_mtu} + 1000))
	mtu "${ns_b}"  veth_B-R1 ${ll_mtu}
	mtu "${ns_r1}" veth_R1-B ${ll_mtu}

	mtu "${ns_a}" ${encap}_a $((${ll_mtu} + 1000))
	mtu "${ns_b}" ${encap}_b $((${ll_mtu} + 1000))

	# Fill exception cache for multiple CPUs (2)
	# we can always use inner IPv4 for that
	for cpu in ${cpu_list}; do
		run_cmd taskset --cpu-list ${cpu} ${ns_a} ping -q -M want -i 0.1 -w 1 -s $((${ll_mtu} + 500)) ${tunnel4_b_addr}
	done

	${ns_a} ip link del dev veth_A-R1 &
	iplink_pid=$!
	sleep 1
	if [ "$(cat /proc/${iplink_pid}/cmdline 2>/dev/null | tr -d '\0')" = "iplinkdeldevveth_A-R1" ]; then
		err "  can't delete veth device in a timely manner, PMTU dst likely leaked"
		return 1
	fi
}

test_cleanup_ipv6_exception() {
	test_cleanup_vxlanX_exception 6
}

test_cleanup_ipv4_exception() {
	test_cleanup_vxlanX_exception 4
}

run_test() {
	(
	tname="$1"
	tdesc="$2"

	unset IFS

	# Since cleanup() relies on variables modified by this subshell, it
	# has to run in this context.
	trap cleanup EXIT

	if [ "$VERBOSE" = "1" ]; then
		printf "\n##########################################################################\n\n"
	fi

	eval test_${tname}
	ret=$?

	if [ $ret -eq 0 ]; then
		printf "TEST: %-60s  [ OK ]\n" "${tdesc}"
	elif [ $ret -eq 1 ]; then
		printf "TEST: %-60s  [FAIL]\n" "${tdesc}"
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			echo
			echo "Pausing. Hit enter to continue"
			read a
		fi
		err_flush
		exit 1
	elif [ $ret -eq 2 ]; then
		printf "TEST: %-60s  [SKIP]\n" "${tdesc}"
		err_flush
	fi

	return $ret
	)
	ret=$?
	[ $ret -ne 0 ] && exitcode=1

	return $ret
}

run_test_nh() {
	tname="$1"
	tdesc="$2"

	USE_NH=yes
	run_test "${tname}" "${tdesc} - nexthop objects"
	USE_NH=no
}

test_list_flush_ipv4_exception() {
	setup namespaces routing || return 2
	trace "${ns_a}"  veth_A-R1    "${ns_r1}" veth_R1-A \
	      "${ns_r1}" veth_R1-B    "${ns_b}"  veth_B-R1 \
	      "${ns_a}"  veth_A-R2    "${ns_r2}" veth_R2-A \
	      "${ns_r2}" veth_R2-B    "${ns_b}"  veth_B-R2

	dst_prefix1="${prefix4}.${b_r1}."
	dst2="${prefix4}.${b_r2}.1"

	# Set up initial MTU values
	mtu "${ns_a}"  veth_A-R1 2000
	mtu "${ns_r1}" veth_R1-A 2000
	mtu "${ns_r1}" veth_R1-B 1500
	mtu "${ns_b}"  veth_B-R1 1500

	mtu "${ns_a}"  veth_A-R2 2000
	mtu "${ns_r2}" veth_R2-A 2000
	mtu "${ns_r2}" veth_R2-B 1500
	mtu "${ns_b}"  veth_B-R2 1500

	fail=0

	# Add 100 addresses for veth endpoint on B reached by default A route
	for i in $(seq 100 199); do
		run_cmd ${ns_b} ip addr add "${dst_prefix1}${i}" dev veth_B-R1
	done

	# Create 100 cached route exceptions for path via R1, one via R2. Note
	# that with IPv4 we need to actually cause a route lookup that matches
	# the exception caused by ICMP, in order to actually have a cached
	# route, so we need to ping each destination twice
	for i in $(seq 100 199); do
		run_cmd ${ns_a} ping -q -M want -i 0.1 -c 2 -s 1800 "${dst_prefix1}${i}"
	done
	run_cmd ${ns_a} ping -q -M want -i 0.1 -c 2 -s 1800 "${dst2}"

	if [ "$(${ns_a} ip -oneline route list cache | wc -l)" -ne 101 ]; then
		err "  can't list cached exceptions"
		fail=1
	fi

	run_cmd ${ns_a} ip route flush cache
	pmtu1="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst_prefix}1)"
	pmtu2="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst_prefix}2)"
	if [ -n "${pmtu1}" ] || [ -n "${pmtu2}" ] || \
	   [ -n "$(${ns_a} ip route list cache)" ]; then
		err "  can't flush cached exceptions"
		fail=1
	fi

	return ${fail}
}

test_list_flush_ipv6_exception() {
	setup namespaces routing || return 2
	trace "${ns_a}"  veth_A-R1    "${ns_r1}" veth_R1-A \
	      "${ns_r1}" veth_R1-B    "${ns_b}"  veth_B-R1 \
	      "${ns_a}"  veth_A-R2    "${ns_r2}" veth_R2-A \
	      "${ns_r2}" veth_R2-B    "${ns_b}"  veth_B-R2

	dst_prefix1="${prefix6}:${b_r1}::"
	dst2="${prefix6}:${b_r2}::1"

	# Set up initial MTU values
	mtu "${ns_a}"  veth_A-R1 2000
	mtu "${ns_r1}" veth_R1-A 2000
	mtu "${ns_r1}" veth_R1-B 1500
	mtu "${ns_b}"  veth_B-R1 1500

	mtu "${ns_a}"  veth_A-R2 2000
	mtu "${ns_r2}" veth_R2-A 2000
	mtu "${ns_r2}" veth_R2-B 1500
	mtu "${ns_b}"  veth_B-R2 1500

	fail=0

	# Add 100 addresses for veth endpoint on B reached by default A route
	for i in $(seq 100 199); do
		run_cmd ${ns_b} ip addr add "${dst_prefix1}${i}" dev veth_B-R1
	done

	# Create 100 cached route exceptions for path via R1, one via R2
	for i in $(seq 100 199); do
		run_cmd ${ns_a} ping -q -M want -i 0.1 -w 1 -s 1800 "${dst_prefix1}${i}"
	done
	run_cmd ${ns_a} ping -q -M want -i 0.1 -w 1 -s 1800 "${dst2}"
	if [ "$(${ns_a} ip -oneline -6 route list cache | wc -l)" -ne 101 ]; then
		err "  can't list cached exceptions"
		fail=1
	fi

	run_cmd ${ns_a} ip -6 route flush cache
	pmtu1="$(route_get_dst_pmtu_from_exception "${ns_a}" "${dst_prefix1}100")"
	pmtu2="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst2})"
	if [ -n "${pmtu1}" ] || [ -n "${pmtu2}" ] || \
	   [ -n "$(${ns_a} ip -6 route list cache)" ]; then
		err "  can't flush cached exceptions"
		fail=1
	fi

	return ${fail}
}

test_pmtu_ipvX_route_change() {
	family=${1}

	setup namespaces routing || return 2
	trace "${ns_a}"  veth_A-R1    "${ns_r1}" veth_R1-A \
	      "${ns_r1}" veth_R1-B    "${ns_b}"  veth_B-R1 \
	      "${ns_a}"  veth_A-R2    "${ns_r2}" veth_R2-A \
	      "${ns_r2}" veth_R2-B    "${ns_b}"  veth_B-R2

	if [ ${family} -eq 4 ]; then
		ping=ping
		dst1="${prefix4}.${b_r1}.1"
		dst2="${prefix4}.${b_r2}.1"
		gw="${prefix4}.${a_r1}.2"
	else
		ping=${ping6}
		dst1="${prefix6}:${b_r1}::1"
		dst2="${prefix6}:${b_r2}::1"
		gw="${prefix6}:${a_r1}::2"
	fi

	# Set up initial MTU values
	mtu "${ns_a}"  veth_A-R1 2000
	mtu "${ns_r1}" veth_R1-A 2000
	mtu "${ns_r1}" veth_R1-B 1400
	mtu "${ns_b}"  veth_B-R1 1400

	mtu "${ns_a}"  veth_A-R2 2000
	mtu "${ns_r2}" veth_R2-A 2000
	mtu "${ns_r2}" veth_R2-B 1500
	mtu "${ns_b}"  veth_B-R2 1500

	# Create route exceptions
	run_cmd ${ns_a} ${ping} -q -M want -i 0.1 -w 1 -s 1800 ${dst1}
	run_cmd ${ns_a} ${ping} -q -M want -i 0.1 -w 1 -s 1800 ${dst2}

	# Check that exceptions have been created with the correct PMTU
	pmtu_1="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst1})"
	check_pmtu_value "1400" "${pmtu_1}" "exceeding MTU" || return 1
	pmtu_2="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst2})"
	check_pmtu_value "1500" "${pmtu_2}" "exceeding MTU" || return 1

	# Replace the route from A to R1
	run_cmd ${ns_a} ip route change default via ${gw}

	# Delete the device in A
	run_cmd ${ns_a} ip link del "veth_A-R1"
}

test_pmtu_ipv4_route_change() {
	test_pmtu_ipvX_route_change 4
}

test_pmtu_ipv6_route_change() {
	test_pmtu_ipvX_route_change 6
}

usage() {
	echo
	echo "$0 [OPTIONS] [TEST]..."
	echo "If no TEST argument is given, all tests will be run."
	echo
	echo "Options"
	echo "  --trace: capture traffic to TEST_INTERFACE.pcap"
	echo
	echo "Available tests${tests}"
	exit 1
}

################################################################################
#
exitcode=0
desc=0

while getopts :ptv o
do
	case $o in
	p) PAUSE_ON_FAIL=yes;;
	v) VERBOSE=1;;
	t) if which tcpdump > /dev/null 2>&1; then
		TRACING=1
	   else
		echo "=== tcpdump not available, tracing disabled"
	   fi
	   ;;
	*) usage;;
	esac
done
shift $(($OPTIND-1))

IFS="	
"

for arg do
	# Check first that all requested tests are available before running any
	command -v > /dev/null "test_${arg}" || { echo "=== Test ${arg} not found"; usage; }
done

trap cleanup EXIT

# start clean
cleanup

HAVE_NH=no
ip nexthop ls >/dev/null 2>&1
[ $? -eq 0 ] && HAVE_NH=yes

name=""
desc=""
rerun_nh=0
for t in ${tests}; do
	[ "${name}" = "" ]	&& name="${t}"	&& continue
	[ "${desc}" = "" ]	&& desc="${t}"	&& continue

	if [ "${HAVE_NH}" = "yes" ]; then
		rerun_nh="${t}"
	fi

	run_this=1
	for arg do
		[ "${arg}" != "${arg#--*}" ] && continue
		[ "${arg}" = "${name}" ] && run_this=1 && break
		run_this=0
	done
	if [ $run_this -eq 1 ]; then
		run_test "${name}" "${desc}"
		# if test was skipped no need to retry with nexthop objects
		[ $? -eq 2 ] && rerun_nh=0

		if [ "${rerun_nh}" = "1" ]; then
			run_test_nh "${name}" "${desc}"
		fi
	fi
	name=""
	desc=""
	rerun_nh=0
done

exit ${exitcode}
