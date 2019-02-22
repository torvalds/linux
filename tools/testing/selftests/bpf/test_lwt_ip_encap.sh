#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Setup/topology:
#
#    NS1             NS2             NS3
#   veth1 <---> veth2   veth3 <---> veth4 (the top route)
#   veth5 <---> veth6   veth7 <---> veth8 (the bottom route)
#
#   each vethN gets IPv[4|6]_N address
#
#   IPv*_SRC = IPv*_1
#   IPv*_DST = IPv*_4
#
#   all tests test pings from IPv*_SRC to IPv*_DST
#
#   by default, routes are configured to allow packets to go
#   IP*_1 <=> IP*_2 <=> IP*_3 <=> IP*_4 (the top route)
#
#   a GRE device is installed in NS3 with IPv*_GRE, and
#   NS1/NS2 are configured to route packets to IPv*_GRE via IP*_8
#   (the bottom route)
#
# Tests:
#
#   1. routes NS2->IPv*_DST are brought down, so the only way a ping
#      from IP*_SRC to IP*_DST can work is via IPv*_GRE
#
#   2a. in an egress test, a bpf LWT_XMIT program is installed on veth1
#       that encaps the packets with an IP/GRE header to route to IPv*_GRE
#
#       ping: SRC->[encap at veth1:egress]->GRE:decap->DST
#       ping replies go DST->SRC directly
#
#   2b. in an ingress test, a bpf LWT_IN program is installed on veth2
#       that encaps the packets with an IP/GRE header to route to IPv*_GRE
#
#       ping: SRC->[encap at veth2:ingress]->GRE:decap->DST
#       ping replies go DST->SRC directly

if [[ $EUID -ne 0 ]]; then
	echo "This script must be run as root"
	echo "FAIL"
	exit 1
fi

readonly NS1="ns1-$(mktemp -u XXXXXX)"
readonly NS2="ns2-$(mktemp -u XXXXXX)"
readonly NS3="ns3-$(mktemp -u XXXXXX)"

readonly IPv4_1="172.16.1.100"
readonly IPv4_2="172.16.2.100"
readonly IPv4_3="172.16.3.100"
readonly IPv4_4="172.16.4.100"
readonly IPv4_5="172.16.5.100"
readonly IPv4_6="172.16.6.100"
readonly IPv4_7="172.16.7.100"
readonly IPv4_8="172.16.8.100"
readonly IPv4_GRE="172.16.16.100"

readonly IPv4_SRC=$IPv4_1
readonly IPv4_DST=$IPv4_4

readonly IPv6_1="fb01::1"
readonly IPv6_2="fb02::1"
readonly IPv6_3="fb03::1"
readonly IPv6_4="fb04::1"
readonly IPv6_5="fb05::1"
readonly IPv6_6="fb06::1"
readonly IPv6_7="fb07::1"
readonly IPv6_8="fb08::1"
readonly IPv6_GRE="fb10::1"

readonly IPv6_SRC=$IPv6_1
readonly IPv6_DST=$IPv6_4

TEST_STATUS=0
TESTS_SUCCEEDED=0
TESTS_FAILED=0

process_test_results()
{
	if [[ "${TEST_STATUS}" -eq 0 ]] ; then
		echo "PASS"
		TESTS_SUCCEEDED=$((TESTS_SUCCEEDED+1))
	else
		echo "FAIL"
		TESTS_FAILED=$((TESTS_FAILED+1))
	fi
}

print_test_summary_and_exit()
{
	echo "passed tests: ${TESTS_SUCCEEDED}"
	echo "failed tests: ${TESTS_FAILED}"
	if [ "${TESTS_FAILED}" -eq "0" ] ; then
		exit 0
	else
		exit 1
	fi
}

setup()
{
	set -e  # exit on error
	TEST_STATUS=0

	# create devices and namespaces
	ip netns add "${NS1}"
	ip netns add "${NS2}"
	ip netns add "${NS3}"

	ip link add veth1 type veth peer name veth2
	ip link add veth3 type veth peer name veth4
	ip link add veth5 type veth peer name veth6
	ip link add veth7 type veth peer name veth8

	ip netns exec ${NS2} sysctl -wq net.ipv4.ip_forward=1
	ip netns exec ${NS2} sysctl -wq net.ipv6.conf.all.forwarding=1

	ip link set veth1 netns ${NS1}
	ip link set veth2 netns ${NS2}
	ip link set veth3 netns ${NS2}
	ip link set veth4 netns ${NS3}
	ip link set veth5 netns ${NS1}
	ip link set veth6 netns ${NS2}
	ip link set veth7 netns ${NS2}
	ip link set veth8 netns ${NS3}

	# configure addesses: the top route (1-2-3-4)
	ip -netns ${NS1}    addr add ${IPv4_1}/24  dev veth1
	ip -netns ${NS2}    addr add ${IPv4_2}/24  dev veth2
	ip -netns ${NS2}    addr add ${IPv4_3}/24  dev veth3
	ip -netns ${NS3}    addr add ${IPv4_4}/24  dev veth4
	ip -netns ${NS1} -6 addr add ${IPv6_1}/128 nodad dev veth1
	ip -netns ${NS2} -6 addr add ${IPv6_2}/128 nodad dev veth2
	ip -netns ${NS2} -6 addr add ${IPv6_3}/128 nodad dev veth3
	ip -netns ${NS3} -6 addr add ${IPv6_4}/128 nodad dev veth4

	# configure addresses: the bottom route (5-6-7-8)
	ip -netns ${NS1}    addr add ${IPv4_5}/24  dev veth5
	ip -netns ${NS2}    addr add ${IPv4_6}/24  dev veth6
	ip -netns ${NS2}    addr add ${IPv4_7}/24  dev veth7
	ip -netns ${NS3}    addr add ${IPv4_8}/24  dev veth8
	ip -netns ${NS1} -6 addr add ${IPv6_5}/128 nodad dev veth5
	ip -netns ${NS2} -6 addr add ${IPv6_6}/128 nodad dev veth6
	ip -netns ${NS2} -6 addr add ${IPv6_7}/128 nodad dev veth7
	ip -netns ${NS3} -6 addr add ${IPv6_8}/128 nodad dev veth8


	ip -netns ${NS1} link set dev veth1 up
	ip -netns ${NS2} link set dev veth2 up
	ip -netns ${NS2} link set dev veth3 up
	ip -netns ${NS3} link set dev veth4 up
	ip -netns ${NS1} link set dev veth5 up
	ip -netns ${NS2} link set dev veth6 up
	ip -netns ${NS2} link set dev veth7 up
	ip -netns ${NS3} link set dev veth8 up

	# configure routes: IP*_SRC -> veth1/IP*_2 (= top route) default;
	# the bottom route to specific bottom addresses

	# NS1
	# top route
	ip -netns ${NS1}    route add ${IPv4_2}/32  dev veth1
	ip -netns ${NS1}    route add default dev veth1 via ${IPv4_2}  # go top by default
	ip -netns ${NS1} -6 route add ${IPv6_2}/128 dev veth1
	ip -netns ${NS1} -6 route add default dev veth1 via ${IPv6_2}  # go top by default
	# bottom route
	ip -netns ${NS1}    route add ${IPv4_6}/32  dev veth5
	ip -netns ${NS1}    route add ${IPv4_7}/32  dev veth5 via ${IPv4_6}
	ip -netns ${NS1}    route add ${IPv4_8}/32  dev veth5 via ${IPv4_6}
	ip -netns ${NS1} -6 route add ${IPv6_6}/128 dev veth5
	ip -netns ${NS1} -6 route add ${IPv6_7}/128 dev veth5 via ${IPv6_6}
	ip -netns ${NS1} -6 route add ${IPv6_8}/128 dev veth5 via ${IPv6_6}

	# NS2
	# top route
	ip -netns ${NS2}    route add ${IPv4_1}/32  dev veth2
	ip -netns ${NS2}    route add ${IPv4_4}/32  dev veth3
	ip -netns ${NS2} -6 route add ${IPv6_1}/128 dev veth2
	ip -netns ${NS2} -6 route add ${IPv6_4}/128 dev veth3
	# bottom route
	ip -netns ${NS2}    route add ${IPv4_5}/32  dev veth6
	ip -netns ${NS2}    route add ${IPv4_8}/32  dev veth7
	ip -netns ${NS2} -6 route add ${IPv6_5}/128 dev veth6
	ip -netns ${NS2} -6 route add ${IPv6_8}/128 dev veth7

	# NS3
	# top route
	ip -netns ${NS3}    route add ${IPv4_3}/32  dev veth4
	ip -netns ${NS3}    route add ${IPv4_1}/32  dev veth4 via ${IPv4_3}
	ip -netns ${NS3}    route add ${IPv4_2}/32  dev veth4 via ${IPv4_3}
	ip -netns ${NS3} -6 route add ${IPv6_3}/128 dev veth4
	ip -netns ${NS3} -6 route add ${IPv6_1}/128 dev veth4 via ${IPv6_3}
	ip -netns ${NS3} -6 route add ${IPv6_2}/128 dev veth4 via ${IPv6_3}
	# bottom route
	ip -netns ${NS3}    route add ${IPv4_7}/32  dev veth8
	ip -netns ${NS3}    route add ${IPv4_5}/32  dev veth8 via ${IPv4_7}
	ip -netns ${NS3}    route add ${IPv4_6}/32  dev veth8 via ${IPv4_7}
	ip -netns ${NS3} -6 route add ${IPv6_7}/128 dev veth8
	ip -netns ${NS3} -6 route add ${IPv6_5}/128 dev veth8 via ${IPv6_7}
	ip -netns ${NS3} -6 route add ${IPv6_6}/128 dev veth8 via ${IPv6_7}

	# configure IPv4 GRE device in NS3, and a route to it via the "bottom" route
	ip -netns ${NS3} tunnel add gre_dev mode gre remote ${IPv4_1} local ${IPv4_GRE} ttl 255
	ip -netns ${NS3} link set gre_dev up
	ip -netns ${NS3} addr add ${IPv4_GRE} nodad dev gre_dev
	ip -netns ${NS1} route add ${IPv4_GRE}/32 dev veth5 via ${IPv4_6}
	ip -netns ${NS2} route add ${IPv4_GRE}/32 dev veth7 via ${IPv4_8}


	# configure IPv6 GRE device in NS3, and a route to it via the "bottom" route
	ip -netns ${NS3} -6 tunnel add name gre6_dev mode ip6gre remote ${IPv6_1} local ${IPv6_GRE} ttl 255
	ip -netns ${NS3} link set gre6_dev up
	ip -netns ${NS3} -6 addr add ${IPv6_GRE} nodad dev gre6_dev
	ip -netns ${NS1} -6 route add ${IPv6_GRE}/128 dev veth5 via ${IPv6_6}
	ip -netns ${NS2} -6 route add ${IPv6_GRE}/128 dev veth7 via ${IPv6_8}

	# rp_filter gets confused by what these tests are doing, so disable it
	ip netns exec ${NS1} sysctl -wq net.ipv4.conf.all.rp_filter=0
	ip netns exec ${NS2} sysctl -wq net.ipv4.conf.all.rp_filter=0
	ip netns exec ${NS3} sysctl -wq net.ipv4.conf.all.rp_filter=0

	sleep 1  # reduce flakiness
	set +e
}

cleanup()
{
	ip netns del ${NS1} 2> /dev/null
	ip netns del ${NS2} 2> /dev/null
	ip netns del ${NS3} 2> /dev/null
}

trap cleanup EXIT

remove_routes_to_gredev()
{
	ip -netns ${NS1} route del ${IPv4_GRE} dev veth5
	ip -netns ${NS2} route del ${IPv4_GRE} dev veth7
	ip -netns ${NS1} -6 route del ${IPv6_GRE}/128 dev veth5
	ip -netns ${NS2} -6 route del ${IPv6_GRE}/128 dev veth7
}

add_unreachable_routes_to_gredev()
{
	ip -netns ${NS1} route add unreachable ${IPv4_GRE}/32
	ip -netns ${NS2} route add unreachable ${IPv4_GRE}/32
	ip -netns ${NS1} -6 route add unreachable ${IPv6_GRE}/128
	ip -netns ${NS2} -6 route add unreachable ${IPv6_GRE}/128
}

test_ping()
{
	local readonly PROTO=$1
	local readonly EXPECTED=$2
	local RET=0

	if [ "${PROTO}" == "IPv4" ] ; then
		ip netns exec ${NS1} ping  -c 1 -W 1 -I ${IPv4_SRC} ${IPv4_DST} 2>&1 > /dev/null
		RET=$?
	elif [ "${PROTO}" == "IPv6" ] ; then
		ip netns exec ${NS1} ping6 -c 1 -W 6 -I ${IPv6_SRC} ${IPv6_DST} 2>&1 > /dev/null
		RET=$?
	else
		echo "    test_ping: unknown PROTO: ${PROTO}"
		TEST_STATUS=1
	fi

	if [ "0" != "${RET}" ]; then
		RET=1
	fi

	if [ "${EXPECTED}" != "${RET}" ] ; then
		echo "    test_ping failed: expected: ${EXPECTED}; got ${RET}"
		TEST_STATUS=1
	fi
}

test_egress()
{
	local readonly ENCAP=$1
	echo "starting egress ${ENCAP} encap test"
	setup

	# by default, pings work
	test_ping IPv4 0
	test_ping IPv6 0

	# remove NS2->DST routes, ping fails
	ip -netns ${NS2}    route del ${IPv4_DST}/32  dev veth3
	ip -netns ${NS2} -6 route del ${IPv6_DST}/128 dev veth3
	test_ping IPv4 1
	test_ping IPv6 1

	# install replacement routes (LWT/eBPF), pings succeed
	if [ "${ENCAP}" == "IPv4" ] ; then
		ip -netns ${NS1} route add ${IPv4_DST} encap bpf xmit obj test_lwt_ip_encap.o sec encap_gre dev veth1
		ip -netns ${NS1} -6 route add ${IPv6_DST} encap bpf xmit obj test_lwt_ip_encap.o sec encap_gre dev veth1
	elif [ "${ENCAP}" == "IPv6" ] ; then
		ip -netns ${NS1} route add ${IPv4_DST} encap bpf xmit obj test_lwt_ip_encap.o sec encap_gre6 dev veth1
		ip -netns ${NS1} -6 route add ${IPv6_DST} encap bpf xmit obj test_lwt_ip_encap.o sec encap_gre6 dev veth1
	else
		echo "    unknown encap ${ENCAP}"
		TEST_STATUS=1
	fi
	test_ping IPv4 0
	test_ping IPv6 0

	# a negative test: remove routes to GRE devices: ping fails
	remove_routes_to_gredev
	test_ping IPv4 1
	test_ping IPv6 1

	# another negative test
	add_unreachable_routes_to_gredev
	test_ping IPv4 1
	test_ping IPv6 1

	cleanup
	process_test_results
}

test_ingress()
{
	local readonly ENCAP=$1
	echo "starting ingress ${ENCAP} encap test"
	setup

	# need to wait a bit for IPv6 to autoconf, otherwise
	# ping6 sometimes fails with "unable to bind to address"

	# by default, pings work
	test_ping IPv4 0
	test_ping IPv6 0

	# remove NS2->DST routes, pings fail
	ip -netns ${NS2}    route del ${IPv4_DST}/32  dev veth3
	ip -netns ${NS2} -6 route del ${IPv6_DST}/128 dev veth3
	test_ping IPv4 1
	test_ping IPv6 1

	# install replacement routes (LWT/eBPF), pings succeed
	if [ "${ENCAP}" == "IPv4" ] ; then
		ip -netns ${NS2} route add ${IPv4_DST} encap bpf in obj test_lwt_ip_encap.o sec encap_gre dev veth2
		ip -netns ${NS2} -6 route add ${IPv6_DST} encap bpf in obj test_lwt_ip_encap.o sec encap_gre dev veth2
	elif [ "${ENCAP}" == "IPv6" ] ; then
		ip -netns ${NS2} route add ${IPv4_DST} encap bpf in obj test_lwt_ip_encap.o sec encap_gre6 dev veth2
		ip -netns ${NS2} -6 route add ${IPv6_DST} encap bpf in obj test_lwt_ip_encap.o sec encap_gre6 dev veth2
	else
		echo "FAIL: unknown encap ${ENCAP}"
	fi
	test_ping IPv4 0
	test_ping IPv6 0

	# a negative test: remove routes to GRE devices: ping fails
	remove_routes_to_gredev
	test_ping IPv4 1
	test_ping IPv6 1

	# another negative test
	add_unreachable_routes_to_gredev
	test_ping IPv4 1
	test_ping IPv6 1

	cleanup
	process_test_results
}

test_egress IPv4
test_egress IPv6
test_ingress IPv4
test_ingress IPv6

print_test_summary_and_exit
