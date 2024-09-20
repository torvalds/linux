#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# 2 namespaces: one host and one router. Use arping from the host to send a
# garp to the router. Router accepts or ignores based on its arp_accept
# or accept_untracked_na configuration.

source lib.sh

TESTS="arp ndisc"

ROUTER_INTF="veth-router"
ROUTER_ADDR="10.0.10.1"
ROUTER_ADDR_V6="2001:db8:abcd:0012::1"

HOST_INTF="veth-host"
HOST_ADDR="10.0.10.2"
HOST_ADDR_V6="2001:db8:abcd:0012::2"

SUBNET_WIDTH=24
PREFIX_WIDTH_V6=64

cleanup() {
	cleanup_ns ${HOST_NS} ${ROUTER_NS}
}

cleanup_v6() {
	cleanup_ns ${HOST_NS_V6} ${ROUTER_NS_V6}
}

setup() {
	set -e
	local arp_accept=$1

	# Set up two namespaces
	setup_ns HOST_NS ROUTER_NS

	# Set up interfaces veth0 and veth1, which are pairs in separate
	# namespaces. veth0 is veth-router, veth1 is veth-host.
	# first, set up the inteface's link to the namespace
	# then, set the interface "up"
	ip netns exec ${ROUTER_NS} ip link add name ${ROUTER_INTF} \
		type veth peer name ${HOST_INTF}

	ip netns exec ${ROUTER_NS} ip link set dev ${ROUTER_INTF} up
	ip netns exec ${ROUTER_NS} ip link set dev ${HOST_INTF} netns ${HOST_NS}

	ip netns exec ${HOST_NS} ip link set dev ${HOST_INTF} up
	ip netns exec ${ROUTER_NS} ip addr add ${ROUTER_ADDR}/${SUBNET_WIDTH} \
		dev ${ROUTER_INTF}

	ip netns exec ${HOST_NS} ip addr add ${HOST_ADDR}/${SUBNET_WIDTH} \
		dev ${HOST_INTF}
	ip netns exec ${HOST_NS} ip route add default via ${HOST_ADDR} \
		dev ${HOST_INTF}
	ip netns exec ${ROUTER_NS} ip route add default via ${ROUTER_ADDR} \
		dev ${ROUTER_INTF}

	ROUTER_CONF=net.ipv4.conf.${ROUTER_INTF}
	ip netns exec ${ROUTER_NS} sysctl -w \
		${ROUTER_CONF}.arp_accept=${arp_accept} >/dev/null 2>&1
	set +e
}

setup_v6() {
	set -e
	local accept_untracked_na=$1

	# Set up two namespaces
	setup_ns HOST_NS_V6 ROUTER_NS_V6

	# Set up interfaces veth0 and veth1, which are pairs in separate
	# namespaces. veth0 is veth-router, veth1 is veth-host.
	# first, set up the inteface's link to the namespace
	# then, set the interface "up"
	ip -n ${ROUTER_NS_V6} link add name ${ROUTER_INTF} \
		type veth peer name ${HOST_INTF} netns ${HOST_NS_V6}

	# Add tc rule to filter out host na message
	tc -n ${ROUTER_NS_V6} qdisc add dev ${ROUTER_INTF} clsact
	tc -n ${ROUTER_NS_V6} filter add dev ${ROUTER_INTF} \
		ingress protocol ipv6 pref 1 handle 101 \
		flower src_ip ${HOST_ADDR_V6} ip_proto icmpv6 type 136 skip_hw action pass

	HOST_CONF=net.ipv6.conf.${HOST_INTF}
	ip netns exec ${HOST_NS_V6} sysctl -qw ${HOST_CONF}.ndisc_notify=1
	ip netns exec ${HOST_NS_V6} sysctl -qw ${HOST_CONF}.disable_ipv6=0
	ROUTER_CONF=net.ipv6.conf.${ROUTER_INTF}
	ip netns exec ${ROUTER_NS_V6} sysctl -w \
		${ROUTER_CONF}.forwarding=1 >/dev/null 2>&1
	ip netns exec ${ROUTER_NS_V6} sysctl -w \
		${ROUTER_CONF}.drop_unsolicited_na=0 >/dev/null 2>&1
	ip netns exec ${ROUTER_NS_V6} sysctl -w \
		${ROUTER_CONF}.accept_untracked_na=${accept_untracked_na} \
		>/dev/null 2>&1

	ip -n ${ROUTER_NS_V6} link set dev ${ROUTER_INTF} up
	ip -n ${HOST_NS_V6} link set dev ${HOST_INTF} up
	ip -n ${ROUTER_NS_V6} addr add ${ROUTER_ADDR_V6}/${PREFIX_WIDTH_V6} \
		dev ${ROUTER_INTF} nodad
	ip -n ${HOST_NS_V6} addr add ${HOST_ADDR_V6}/${PREFIX_WIDTH_V6} \
		dev ${HOST_INTF}
	set +e
}

verify_arp() {
	local arp_accept=$1
	local same_subnet=$2

	neigh_show_output=$(ip netns exec ${ROUTER_NS} ip neigh get \
		${HOST_ADDR} dev ${ROUTER_INTF} 2>/dev/null)

	if [ ${arp_accept} -eq 1 ]; then
		# Neighbor entries expected
		[[ ${neigh_show_output} ]]
	elif [ ${arp_accept} -eq 2 ]; then
		if [ ${same_subnet} -eq 1 ]; then
			# Neighbor entries expected
			[[ ${neigh_show_output} ]]
		else
			[[ -z "${neigh_show_output}" ]]
		fi
	else
		[[ -z "${neigh_show_output}" ]]
	fi
 }

arp_test_gratuitous() {
	set -e
	local arp_accept=$1
	local same_subnet=$2

	if [ ${arp_accept} -eq 2 ]; then
		test_msg=("test_arp: "
			  "accept_arp=$1 "
			  "same_subnet=$2")
		if [ ${same_subnet} -eq 0 ]; then
			HOST_ADDR=10.0.11.3
		else
			HOST_ADDR=10.0.10.3
		fi
	else
		test_msg=("test_arp: "
			  "accept_arp=$1")
	fi
	# Supply arp_accept option to set up which sets it in sysctl
	setup ${arp_accept}
	ip netns exec ${HOST_NS} arping -A -I ${HOST_INTF} -U ${HOST_ADDR} -c1 2>&1 >/dev/null

	if verify_arp $1 $2; then
		printf "    TEST: %-60s  [ OK ]\n" "${test_msg[*]}"
	else
		printf "    TEST: %-60s  [FAIL]\n" "${test_msg[*]}"
	fi
	cleanup
	set +e
}

arp_test_gratuitous_combinations() {
	arp_test_gratuitous 0
	arp_test_gratuitous 1
	arp_test_gratuitous 2 0 # Second entry indicates subnet or not
	arp_test_gratuitous 2 1
}

verify_ndisc() {
	local accept_untracked_na=$1
	local same_subnet=$2

	neigh_show_output=$(ip -6 -netns ${ROUTER_NS_V6} neigh show \
		to ${HOST_ADDR_V6} dev ${ROUTER_INTF} nud stale)

	if [ ${accept_untracked_na} -eq 1 ]; then
		# Neighbour entry expected to be present
		[[ ${neigh_show_output} ]]
	elif [ ${accept_untracked_na} -eq 2 ]; then
		if [ ${same_subnet} -eq 1 ]; then
			[[ ${neigh_show_output} ]]
		else
			[[ -z "${neigh_show_output}" ]]
		fi
	else
		# Neighbour entry expected to be absent for all other cases
		[[ -z "${neigh_show_output}" ]]
	fi
}

ndisc_test_untracked_advertisements() {
	set -e
	test_msg=("test_ndisc: "
		  "accept_untracked_na=$1")

	local accept_untracked_na=$1
	local same_subnet=$2
	if [ ${accept_untracked_na} -eq 2 ]; then
		test_msg=("test_ndisc: "
			  "accept_untracked_na=$1 "
			  "same_subnet=$2")
		if [ ${same_subnet} -eq 0 ]; then
			# Not same subnet
			HOST_ADDR_V6=2000:db8:abcd:0013::4
		else
			HOST_ADDR_V6=2001:db8:abcd:0012::3
		fi
	fi
	setup_v6 $1
	slowwait_for_counter 15 1 \
		tc_rule_handle_stats_get "dev ${ROUTER_INTF} ingress" 101 ".packets" "-n ${ROUTER_NS_V6}"

	if verify_ndisc $1 $2; then
		printf "    TEST: %-60s  [ OK ]\n" "${test_msg[*]}"
	else
		printf "    TEST: %-60s  [FAIL]\n" "${test_msg[*]}"
	fi

	cleanup_v6
	set +e
}

ndisc_test_untracked_combinations() {
	ndisc_test_untracked_advertisements 0
	ndisc_test_untracked_advertisements 1
	ndisc_test_untracked_advertisements 2 0
	ndisc_test_untracked_advertisements 2 1
}

################################################################################
# usage

usage()
{
	cat <<EOF
usage: ${0##*/} OPTS

	-t <test>       Test(s) to run (default: all)
			(options: $TESTS)
EOF
}

################################################################################
# main

while getopts ":t:h" opt; do
	case $opt in
		t) TESTS=$OPTARG;;
		h) usage; exit 0;;
		*) usage; exit 1;;
	esac
done

if [ "$(id -u)" -ne 0 ];then
	echo "SKIP: Need root privileges"
	exit $ksft_skip;
fi

if [ ! -x "$(command -v ip)" ]; then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

if [ ! -x "$(command -v tcpdump)" ]; then
	echo "SKIP: Could not run test without tcpdump tool"
	exit $ksft_skip
fi

if [ ! -x "$(command -v arping)" ]; then
	echo "SKIP: Could not run test without arping tool"
	exit $ksft_skip
fi

# start clean
cleanup &> /dev/null
cleanup_v6 &> /dev/null

for t in $TESTS
do
	case $t in
	arp_test_gratuitous_combinations|arp) arp_test_gratuitous_combinations;;
	ndisc_test_untracked_combinations|ndisc) \
		ndisc_test_untracked_combinations;;
	help) echo "Test names: $TESTS"; exit 0;;
esac
done
