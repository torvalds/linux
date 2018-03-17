#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Check that route PMTU values match expectations, and that initial device MTU
# values are assigned correctly
#
# Tests currently implemented:
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

tests="
	pmtu_vti6_exception	vti6: PMTU exceptions
	pmtu_vti4_default_mtu	vti4: default MTU assignment
	pmtu_vti6_default_mtu	vti6: default MTU assignment"

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
	${ns_a} ip -6 xfrm state add src ${veth6_a_addr} dst ${veth6_b_addr} spi 0x1000 proto esp aead "rfc4106(gcm(aes))" 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode tunnel || return 1
	${ns_a} ip -6 xfrm state add src ${veth6_b_addr} dst ${veth6_a_addr} spi 0x1001 proto esp aead "rfc4106(gcm(aes))" 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode tunnel
	${ns_a} ip -6 xfrm policy add dir out mark 10 tmpl src ${veth6_a_addr} dst ${veth6_b_addr} proto esp mode tunnel
	${ns_a} ip -6 xfrm policy add dir in mark 10 tmpl src ${veth6_b_addr} dst ${veth6_a_addr} proto esp mode tunnel

	${ns_b} ip -6 xfrm state add src ${veth6_a_addr} dst ${veth6_b_addr} spi 0x1000 proto esp aead "rfc4106(gcm(aes))" 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode tunnel
	${ns_b} ip -6 xfrm state add src ${veth6_b_addr} dst ${veth6_a_addr} spi 0x1001 proto esp aead "rfc4106(gcm(aes))" 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode tunnel
	${ns_b} ip -6 xfrm policy add dir out mark 10 tmpl src ${veth6_b_addr} dst ${veth6_a_addr} proto esp mode tunnel
	${ns_b} ip -6 xfrm policy add dir in mark 10 tmpl src ${veth6_a_addr} dst ${veth6_b_addr} proto esp mode tunnel
}

setup() {
	[ "$(id -u)" -ne 0 ] && echo "  need to run as root" && return 1

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

test_pmtu_vti6_exception() {
	setup namespaces veth vti6 xfrm || return 2
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
