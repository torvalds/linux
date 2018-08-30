#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Check that route PMTU values match expectations, and that initial device MTU
# values are assigned correctly
#
# Tests currently implemented:
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

tests="
	pmtu_vti6_exception		vti6: PMTU exceptions
	pmtu_vti4_exception		vti4: PMTU exceptions
	pmtu_vti4_default_mtu		vti4: default MTU assignment
	pmtu_vti6_default_mtu		vti6: default MTU assignment
	pmtu_vti4_link_add_mtu		vti4: MTU setting on link creation
	pmtu_vti6_link_add_mtu		vti6: MTU setting on link creation
	pmtu_vti6_link_change_mtu	vti6: MTU changes on link changes"

NS_A="ns-$(mktemp -u XXXXXX)"
NS_B="ns-$(mktemp -u XXXXXX)"
ns_a="ip netns exec ${NS_A}"
ns_b="ip netns exec ${NS_B}"

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

err() {
	err_buf="${err_buf}${1}
"
}

err_flush() {
	echo -n "${err_buf}"
	err_buf=
}

setup_namespaces() {
	ip netns add ${NS_A} || return 1
	ip netns add ${NS_B}
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

setup() {
	[ "$(id -u)" -ne 0 ] && echo "  need to run as root" && return $ksft_skip

	cleanup_done=0
	for arg do
		eval setup_${arg} || { echo "  ${arg} not supported"; return 1; }
	done
}

cleanup() {
	[ ${cleanup_done} -eq 1 ] && return
	ip netns del ${NS_A} 2 > /dev/null
	ip netns del ${NS_B} 2 > /dev/null
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
		[ ${next} -eq 1 ] && echo "${i}" && return
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

test_pmtu_vti4_exception() {
	setup namespaces veth vti4 xfrm4 || return 2

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
	if [ "${pmtu}" != "" ]; then
		err "  unexpected exception created with PMTU ${pmtu} for IP payload length ${esp_payload_rfc4106}"
		return 1
	fi

	# Now exceed link layer MTU by one byte, check that exception is created
	${ns_a} ping -q -M want -i 0.1 -w 2 -s $((ping_payload + 1)) ${vti4_b_addr} > /dev/null
	pmtu="$(route_get_dst_pmtu_from_exception "${ns_a}" ${vti4_b_addr})"
	if [ "${pmtu}" = "" ]; then
		err "  exception not created for IP payload length $((esp_payload_rfc4106 + 1))"
		return 1
	fi

	# ...with the right PMTU value
	if [ ${pmtu} -ne ${esp_payload_rfc4106} ]; then
		err "  wrong PMTU ${pmtu} in exception, expected: ${esp_payload_rfc4106}"
		return 1
	fi
}

test_pmtu_vti6_exception() {
	setup namespaces veth vti6 xfrm6 || return 2
	fail=0

	# Create route exception by exceeding link layer MTU
	mtu "${ns_a}" veth_a 4000
	mtu "${ns_b}" veth_b 4000
	mtu "${ns_a}" vti6_a 5000
	mtu "${ns_b}" vti6_b 5000
	${ns_a} ping6 -q -i 0.1 -w 2 -s 60000 ${vti6_b_addr} > /dev/null

	# Check that exception was created
	if [ "$(route_get_dst_pmtu_from_exception "${ns_a}" ${vti6_b_addr})" = "" ]; then
		err "  tunnel exceeding link layer MTU didn't create route exception"
		return 1
	fi

	# Decrease tunnel MTU, check for PMTU decrease in route exception
	mtu "${ns_a}" vti6_a 3000

	if [ "$(route_get_dst_pmtu_from_exception "${ns_a}" ${vti6_b_addr})" -ne 3000 ]; then
		err "  decreasing tunnel MTU didn't decrease route exception PMTU"
		fail=1
	fi

	# Increase tunnel MTU, check for PMTU increase in route exception
	mtu "${ns_a}" vti6_a 9000
	if [ "$(route_get_dst_pmtu_from_exception "${ns_a}" ${vti6_b_addr})" -ne 9000 ]; then
		err "  increasing tunnel MTU didn't increase route exception PMTU"
		fail=1
	fi

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

trap cleanup EXIT

exitcode=0
desc=0
IFS="	
"
for t in ${tests}; do
	[ $desc -eq 0 ] && name="${t}" && desc=1 && continue || desc=0

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
