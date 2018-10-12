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
#	.--------------R1--------------.	a_r2: 1500
#	A                               B	a_r3: 2000
#	'--------------R2--------------'	a_r4: 1400
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

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

# Some systems don't have a ping6 binary anymore
which ping6 > /dev/null 2>&1 && ping6=$(which ping6) || ping6=$(which ping)

tests="
	pmtu_ipv4_exception		ipv4: PMTU exceptions
	pmtu_ipv6_exception		ipv6: PMTU exceptions
	pmtu_vti6_exception		vti6: PMTU exceptions
	pmtu_vti4_exception		vti4: PMTU exceptions
	pmtu_vti4_default_mtu		vti4: default MTU assignment
	pmtu_vti6_default_mtu		vti6: default MTU assignment
	pmtu_vti4_link_add_mtu		vti4: MTU setting on link creation
	pmtu_vti6_link_add_mtu		vti6: MTU setting on link creation
	pmtu_vti6_link_change_mtu	vti6: MTU changes on link changes"

NS_A="ns-$(mktemp -u XXXXXX)"
NS_B="ns-$(mktemp -u XXXXXX)"
NS_R1="ns-$(mktemp -u XXXXXX)"
NS_R2="ns-$(mktemp -u XXXXXX)"
ns_a="ip netns exec ${NS_A}"
ns_b="ip netns exec ${NS_B}"
ns_r1="ip netns exec ${NS_R1}"
ns_r2="ip netns exec ${NS_R2}"

# Addressing and routing for tests with routers: four network segments, with
# index SEGMENT between 1 and 4, a common prefix (PREFIX4 or PREFIX6) and an
# identifier ID, which is 1 for hosts (A and B), 2 for routers (R1 and R2).
# Addresses are:
# - IPv4: PREFIX4.SEGMENT.ID (/24)
# - IPv6: PREFIX6:SEGMENT::ID (/64)
prefix4="192.168"
prefix6="fd00"
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

veth4_a_addr="192.168.1.1"
veth4_b_addr="192.168.1.2"
veth4_mask="24"
veth6_a_addr="fd00:1::a"
veth6_b_addr="fd00:1::b"
veth6_mask="64"

vti4_a_addr="192.168.2.1"
vti4_b_addr="192.168.2.2"
vti4_mask="24"
vti6_a_addr="fd00:2::a"
vti6_b_addr="fd00:2::b"
vti6_mask="64"

dummy6_0_addr="fc00:1000::0"
dummy6_1_addr="fc00:1001::0"
dummy6_mask="64"

cleanup_done=1
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

# Find the auto-generated name for this namespace
nsname() {
	eval echo \$NS_$1
}

setup_namespaces() {
	for n in ${NS_A} ${NS_B} ${NS_R1} ${NS_R2}; do
		ip netns add ${n} || return 1
	done
}

setup_veth() {
	${ns_a} ip link add veth_a type veth peer name veth_b || return 1
	${ns_a} ip link set veth_b netns ${NS_B}

	${ns_a} ip addr add ${veth4_a_addr}/${veth4_mask} dev veth_a
	${ns_b} ip addr add ${veth4_b_addr}/${veth4_mask} dev veth_b

	${ns_a} ip addr add ${veth6_a_addr}/${veth6_mask} dev veth_a
	${ns_b} ip addr add ${veth6_b_addr}/${veth6_mask} dev veth_b

	${ns_a} ip link set veth_a up
	${ns_b} ip link set veth_b up
}

setup_vti() {
	proto=${1}
	veth_a_addr="${2}"
	veth_b_addr="${3}"
	vti_a_addr="${4}"
	vti_b_addr="${5}"
	vti_mask=${6}

	[ ${proto} -eq 6 ] && vti_type="vti6" || vti_type="vti"

	${ns_a} ip link add vti${proto}_a type ${vti_type} local ${veth_a_addr} remote ${veth_b_addr} key 10 || return 1
	${ns_b} ip link add vti${proto}_b type ${vti_type} local ${veth_b_addr} remote ${veth_a_addr} key 10

	${ns_a} ip addr add ${vti_a_addr}/${vti_mask} dev vti${proto}_a
	${ns_b} ip addr add ${vti_b_addr}/${vti_mask} dev vti${proto}_b

	${ns_a} ip link set vti${proto}_a up
	${ns_b} ip link set vti${proto}_b up

	sleep 1
}

setup_vti4() {
	setup_vti 4 ${veth4_a_addr} ${veth4_b_addr} ${vti4_a_addr} ${vti4_b_addr} ${vti4_mask}
}

setup_vti6() {
	setup_vti 6 ${veth6_a_addr} ${veth6_b_addr} ${vti6_a_addr} ${vti6_b_addr} ${vti6_mask}
}

setup_xfrm() {
	proto=${1}
	veth_a_addr="${2}"
	veth_b_addr="${3}"

	${ns_a} ip -${proto} xfrm state add src ${veth_a_addr} dst ${veth_b_addr} spi 0x1000 proto esp aead "rfc4106(gcm(aes))" 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode tunnel || return 1
	${ns_a} ip -${proto} xfrm state add src ${veth_b_addr} dst ${veth_a_addr} spi 0x1001 proto esp aead "rfc4106(gcm(aes))" 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode tunnel
	${ns_a} ip -${proto} xfrm policy add dir out mark 10 tmpl src ${veth_a_addr} dst ${veth_b_addr} proto esp mode tunnel
	${ns_a} ip -${proto} xfrm policy add dir in mark 10 tmpl src ${veth_b_addr} dst ${veth_a_addr} proto esp mode tunnel

	${ns_b} ip -${proto} xfrm state add src ${veth_a_addr} dst ${veth_b_addr} spi 0x1000 proto esp aead "rfc4106(gcm(aes))" 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode tunnel
	${ns_b} ip -${proto} xfrm state add src ${veth_b_addr} dst ${veth_a_addr} spi 0x1001 proto esp aead "rfc4106(gcm(aes))" 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode tunnel
	${ns_b} ip -${proto} xfrm policy add dir out mark 10 tmpl src ${veth_b_addr} dst ${veth_a_addr} proto esp mode tunnel
	${ns_b} ip -${proto} xfrm policy add dir in mark 10 tmpl src ${veth_a_addr} dst ${veth_b_addr} proto esp mode tunnel
}

setup_xfrm4() {
	setup_xfrm 4 ${veth4_a_addr} ${veth4_b_addr}
}

setup_xfrm6() {
	setup_xfrm 6 ${veth6_a_addr} ${veth6_b_addr}
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

	for i in ${routes}; do
		[ "${ns}" = "" ]	&& ns="${i}"		&& continue
		[ "${addr}" = "" ]	&& addr="${i}"		&& continue
		[ "${gw}" = "" ]	&& gw="${i}"

		ns_name="$(nsname ${ns})"

		ip -n ${ns_name} route add ${addr} via ${gw}

		ns=""; addr=""; gw=""
	done
}

setup() {
	[ "$(id -u)" -ne 0 ] && echo "  need to run as root" && return $ksft_skip

	cleanup_done=0
	for arg do
		eval setup_${arg} || { echo "  ${arg} not supported"; return 1; }
	done
}

trace() {
	[ $tracing -eq 0 ] && return

	for arg do
		[ "${ns_cmd}" = "" ] && ns_cmd="${arg}" && continue
		${ns_cmd} tcpdump -s 0 -i "${arg}" -w "${name}_${arg}.pcap" 2> /dev/null &
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

	[ ${cleanup_done} -eq 1 ] && return
	for n in ${NS_A} ${NS_B} ${NS_R1} ${NS_R2}; do
		ip netns del ${n} 2> /dev/null
	done
	cleanup_done=1
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
	${ns_a} ${ping} -q -M want -i 0.1 -w 2 -s 1800 ${dst1} > /dev/null
	${ns_a} ${ping} -q -M want -i 0.1 -w 2 -s 1800 ${dst2} > /dev/null

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
	${ns_a} ${ping} -q -M want -i 0.1 -w 2 -s 1400 ${dst2} > /dev/null
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
	${ns_a} ${ping} -q -M want -i 0.1 -w 2 -s 1400 ${dst2} > /dev/null
	pmtu_2="$(route_get_dst_pmtu_from_exception "${ns_a}" ${dst2})"
	check_pmtu_value "lock 552" "${pmtu_2}" "exceeding MTU, with MTU < min_pmtu" || return 1
}

test_pmtu_ipv4_exception() {
	test_pmtu_ipvX 4
}

test_pmtu_ipv6_exception() {
	test_pmtu_ipvX 6
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
	${ns_a} ping -q -M want -i 0.1 -w 2 -s ${ping_payload} ${vti4_b_addr} > /dev/null
	pmtu="$(route_get_dst_pmtu_from_exception "${ns_a}" ${vti4_b_addr})"
	check_pmtu_value "" "${pmtu}" "sending packet smaller than PMTU (IP payload length ${esp_payload_rfc4106})" || return 1

	# Now exceed link layer MTU by one byte, check that exception is created
	# with the right PMTU value
	${ns_a} ping -q -M want -i 0.1 -w 2 -s $((ping_payload + 1)) ${vti4_b_addr} > /dev/null
	pmtu="$(route_get_dst_pmtu_from_exception "${ns_a}" ${vti4_b_addr})"
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
	${ns_a} ${ping6} -q -i 0.1 -w 2 -s 60000 ${vti6_b_addr} > /dev/null

	# Check that exception was created
	pmtu="$(route_get_dst_pmtu_from_exception "${ns_a}" ${vti6_b_addr})"
	check_pmtu_value any "${pmtu}" "creating tunnel exceeding link layer MTU" || return 1

	# Decrease tunnel MTU, check for PMTU decrease in route exception
	mtu "${ns_a}" vti6_a 3000
	pmtu="$(route_get_dst_pmtu_from_exception "${ns_a}" ${vti6_b_addr})"
	check_pmtu_value "3000" "${pmtu}" "decreasing tunnel MTU" || fail=1

	# Increase tunnel MTU, check for PMTU increase in route exception
	mtu "${ns_a}" vti6_a 9000
	pmtu="$(route_get_dst_pmtu_from_exception "${ns_a}" ${vti6_b_addr})"
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

	${ns_a} ip link add vti4_a type vti local ${veth4_a_addr} remote ${veth4_b_addr} key 10
	[ $? -ne 0 ] && err "  vti not supported" && return 2
	${ns_a} ip link del vti4_a

	fail=0

	min=68
	max=$((65535 - 20))
	# Check invalid values first
	for v in $((min - 1)) $((max + 1)); do
		${ns_a} ip link add vti4_a mtu ${v} type vti local ${veth4_a_addr} remote ${veth4_b_addr} key 10 2>/dev/null
		# This can fail, or MTU can be adjusted to a proper value
		[ $? -ne 0 ] && continue
		mtu="$(link_get_mtu "${ns_a}" vti4_a)"
		if [ ${mtu} -lt ${min} -o ${mtu} -gt ${max} ]; then
			err "  vti tunnel created with invalid MTU ${mtu}"
			fail=1
		fi
		${ns_a} ip link del vti4_a
	done

	# Now check valid values
	for v in ${min} 1300 ${max}; do
		${ns_a} ip link add vti4_a mtu ${v} type vti local ${veth4_a_addr} remote ${veth4_b_addr} key 10
		mtu="$(link_get_mtu "${ns_a}" vti4_a)"
		${ns_a} ip link del vti4_a
		if [ "${mtu}" != "${v}" ]; then
			err "  vti MTU ${mtu} doesn't match configured value ${v}"
			fail=1
		fi
	done

	return ${fail}
}

test_pmtu_vti6_link_add_mtu() {
	setup namespaces || return 2

	${ns_a} ip link add vti6_a type vti6 local ${veth6_a_addr} remote ${veth6_b_addr} key 10
	[ $? -ne 0 ] && err "  vti6 not supported" && return 2
	${ns_a} ip link del vti6_a

	fail=0

	min=68			# vti6 can carry IPv4 packets too
	max=$((65535 - 40))
	# Check invalid values first
	for v in $((min - 1)) $((max + 1)); do
		${ns_a} ip link add vti6_a mtu ${v} type vti6 local ${veth6_a_addr} remote ${veth6_b_addr} key 10 2>/dev/null
		# This can fail, or MTU can be adjusted to a proper value
		[ $? -ne 0 ] && continue
		mtu="$(link_get_mtu "${ns_a}" vti6_a)"
		if [ ${mtu} -lt ${min} -o ${mtu} -gt ${max} ]; then
			err "  vti6 tunnel created with invalid MTU ${v}"
			fail=1
		fi
		${ns_a} ip link del vti6_a
	done

	# Now check valid values
	for v in 68 1280 1300 $((65535 - 40)); do
		${ns_a} ip link add vti6_a mtu ${v} type vti6 local ${veth6_a_addr} remote ${veth6_b_addr} key 10
		mtu="$(link_get_mtu "${ns_a}" vti6_a)"
		${ns_a} ip link del vti6_a
		if [ "${mtu}" != "${v}" ]; then
			err "  vti6 MTU ${mtu} doesn't match configured value ${v}"
			fail=1
		fi
	done

	return ${fail}
}

test_pmtu_vti6_link_change_mtu() {
	setup namespaces || return 2

	${ns_a} ip link add dummy0 mtu 1500 type dummy
	[ $? -ne 0 ] && err "  dummy not supported" && return 2
	${ns_a} ip link add dummy1 mtu 3000 type dummy
	${ns_a} ip link set dummy0 up
	${ns_a} ip link set dummy1 up

	${ns_a} ip addr add ${dummy6_0_addr}/${dummy6_mask} dev dummy0
	${ns_a} ip addr add ${dummy6_1_addr}/${dummy6_mask} dev dummy1

	fail=0

	# Create vti6 interface bound to device, passing MTU, check it
	${ns_a} ip link add vti6_a mtu 1300 type vti6 remote ${dummy6_0_addr} local ${dummy6_0_addr}
	mtu="$(link_get_mtu "${ns_a}" vti6_a)"
	if [ ${mtu} -ne 1300 ]; then
		err "  vti6 MTU ${mtu} doesn't match configured value 1300"
		fail=1
	fi

	# Move to another device with different MTU, without passing MTU, check
	# MTU is adjusted
	${ns_a} ip link set vti6_a type vti6 remote ${dummy6_1_addr} local ${dummy6_1_addr}
	mtu="$(link_get_mtu "${ns_a}" vti6_a)"
	if [ ${mtu} -ne $((3000 - 40)) ]; then
		err "  vti MTU ${mtu} is not dummy MTU 3000 minus IPv6 header length"
		fail=1
	fi

	# Move it back, passing MTU, check MTU is not overridden
	${ns_a} ip link set vti6_a mtu 1280 type vti6 remote ${dummy6_0_addr} local ${dummy6_0_addr}
	mtu="$(link_get_mtu "${ns_a}" vti6_a)"
	if [ ${mtu} -ne 1280 ]; then
		err "  vti6 MTU ${mtu} doesn't match configured value 1280"
		fail=1
	fi

	return ${fail}
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

exitcode=0
desc=0
IFS="	
"

tracing=0
for arg do
	if [ "${arg}" != "${arg#--*}" ]; then
		opt="${arg#--}"
		if [ "${opt}" = "trace" ]; then
			if which tcpdump > /dev/null 2>&1; then
				tracing=1
			else
				echo "=== tcpdump not available, tracing disabled"
			fi
		else
			usage
		fi
	else
		# Check first that all requested tests are available before
		# running any
		command -v > /dev/null "test_${arg}" || { echo "=== Test ${arg} not found"; usage; }
	fi
done

trap cleanup EXIT

for t in ${tests}; do
	[ $desc -eq 0 ] && name="${t}" && desc=1 && continue || desc=0

	run_this=1
	for arg do
		[ "${arg}" != "${arg#--*}" ] && continue
		[ "${arg}" = "${name}" ] && run_this=1 && break
		run_this=0
	done
	[ $run_this -eq 0 ] && continue

	(
		unset IFS
		eval test_${name}
		ret=$?
		cleanup

		if [ $ret -eq 0 ]; then
			printf "TEST: %-60s  [ OK ]\n" "${t}"
		elif [ $ret -eq 1 ]; then
			printf "TEST: %-60s  [FAIL]\n" "${t}"
			err_flush
			exit 1
		elif [ $ret -eq 2 ]; then
			printf "TEST: %-60s  [SKIP]\n" "${t}"
			err_flush
		fi
	)
	[ $? -ne 0 ] && exitcode=1
done

exit ${exitcode}
