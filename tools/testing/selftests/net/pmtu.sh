#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Check that route PMTU values match expectations
#
# Tests currently implemented:
#
# - test_pmtu_vti6_exception
#	Set up vti6 tunnel on top of veth, with xfrm states and policies, in two
#	namespaces with matching endpoints. Check that route exception is
#	created by exceeding link layer MTU with ping to other endpoint. Then
#	decrease and increase MTU of tunnel, checking that route exception PMTU
#	changes accordingly

NS_A="ns-$(mktemp -u XXXXXX)"
NS_B="ns-$(mktemp -u XXXXXX)"
ns_a="ip netns exec ${NS_A}"
ns_b="ip netns exec ${NS_B}"

veth6_a_addr="fd00:1::a"
veth6_b_addr="fd00:1::b"
veth6_mask="64"

vti6_a_addr="fd00:2::a"
vti6_b_addr="fd00:2::b"
vti6_mask="64"

setup_namespaces() {
	ip netns add ${NS_A} || return 0
	ip netns add ${NS_B}

	return 1
}

setup_veth() {
	${ns_a} ip link add veth_a type veth peer name veth_b || return 0
	${ns_a} ip link set veth_b netns ${NS_B}

	${ns_a} ip addr add ${veth6_a_addr}/${veth6_mask} dev veth_a
	${ns_b} ip addr add ${veth6_b_addr}/${veth6_mask} dev veth_b

	${ns_a} ip link set veth_a up
	${ns_b} ip link set veth_b up

	return 1
}

setup_vti6() {
	${ns_a} ip link add vti_a type vti6 local ${veth6_a_addr} remote ${veth6_b_addr} key 10 || return 0
	${ns_b} ip link add vti_b type vti6 local ${veth6_b_addr} remote ${veth6_a_addr} key 10

	${ns_a} ip addr add ${vti6_a_addr}/${vti6_mask} dev vti_a
	${ns_b} ip addr add ${vti6_b_addr}/${vti6_mask} dev vti_b

	${ns_a} ip link set vti_a up
	${ns_b} ip link set vti_b up

	sleep 1

	return 1
}

setup_xfrm() {
	${ns_a} ip -6 xfrm state add src ${veth6_a_addr} dst ${veth6_b_addr} spi 0x1000 proto esp aead "rfc4106(gcm(aes))" 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode tunnel || return 0
	${ns_a} ip -6 xfrm state add src ${veth6_b_addr} dst ${veth6_a_addr} spi 0x1001 proto esp aead "rfc4106(gcm(aes))" 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode tunnel
	${ns_a} ip -6 xfrm policy add dir out mark 10 tmpl src ${veth6_a_addr} dst ${veth6_b_addr} proto esp mode tunnel
	${ns_a} ip -6 xfrm policy add dir in mark 10 tmpl src ${veth6_b_addr} dst ${veth6_a_addr} proto esp mode tunnel

	${ns_b} ip -6 xfrm state add src ${veth6_a_addr} dst ${veth6_b_addr} spi 0x1000 proto esp aead "rfc4106(gcm(aes))" 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode tunnel
	${ns_b} ip -6 xfrm state add src ${veth6_b_addr} dst ${veth6_a_addr} spi 0x1001 proto esp aead "rfc4106(gcm(aes))" 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f 128 mode tunnel
	${ns_b} ip -6 xfrm policy add dir out mark 10 tmpl src ${veth6_b_addr} dst ${veth6_a_addr} proto esp mode tunnel
	${ns_b} ip -6 xfrm policy add dir in mark 10 tmpl src ${veth6_a_addr} dst ${veth6_b_addr} proto esp mode tunnel

	return 1
}

setup() {
	tunnel_type="$1"

	[ "$(id -u)" -ne 0 ] && echo "SKIP: need to run as root" && exit 0

	setup_namespaces && echo "SKIP: namespaces not supported" && exit 0
	setup_veth && echo "SKIP: veth not supported" && exit 0

	case ${tunnel_type} in
	"vti6")
		setup_vti6 && echo "SKIP: vti6 not supported" && exit 0
		setup_xfrm && echo "SKIP: xfrm not supported" && exit 0
		;;
	*)
		;;
	esac
}

cleanup() {
	ip netns del ${NS_A} 2 > /dev/null
	ip netns del ${NS_B} 2 > /dev/null
}

mtu() {
	ns_cmd="${1}"
	dev="${2}"
	mtu="${3}"

	${ns_cmd} ip link set dev ${dev} mtu ${mtu}
}

route_get_dst_exception() {
	dst="${1}"

	${ns_a} ip route get "${dst}"
}

route_get_dst_pmtu_from_exception() {
	dst="${1}"

	exception="$(route_get_dst_exception ${dst})"
	next=0
	for i in ${exception}; do
		[ ${next} -eq 1 ] && echo "${i}" && return
		[ "${i}" = "mtu" ] && next=1
	done
}

test_pmtu_vti6_exception() {
	setup vti6

	# Create route exception by exceeding link layer MTU
	mtu "${ns_a}" veth_a 4000
	mtu "${ns_b}" veth_b 4000
	mtu "${ns_a}" vti_a 5000
	mtu "${ns_b}" vti_b 5000
	${ns_a} ping6 -q -i 0.1 -w 2 -s 60000 ${vti6_b_addr} > /dev/null

	# Check that exception was created
	if [ "$(route_get_dst_pmtu_from_exception ${vti6_b_addr})" = "" ]; then
		echo "FAIL: Tunnel exceeding link layer MTU didn't create route exception"
		exit 1
	fi

	# Decrease tunnel MTU, check for PMTU decrease in route exception
	mtu "${ns_a}" vti_a 3000

	if [ "$(route_get_dst_pmtu_from_exception ${vti6_b_addr})" -ne 3000 ]; then
		echo "FAIL: Decreasing tunnel MTU didn't decrease route exception PMTU"
		exit 1
	fi

	# Increase tunnel MTU, check for PMTU increase in route exception
	mtu "${ns_a}" vti_a 9000
	if [ "$(route_get_dst_pmtu_from_exception ${vti6_b_addr})" -ne 9000 ]; then
		echo "FAIL: Increasing tunnel MTU didn't increase route exception PMTU"
		exit 1
	fi

	echo "PASS"
}

trap cleanup EXIT

test_pmtu_vti6_exception

exit 0
