#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test topology:
#    - - - - - - - - - - - - - - - - - - -
#    | veth1         veth2         veth3 |  ns0
#     - -| - - - - - - | - - - - - - | - -
#    ---------     ---------     ---------
#    | veth0 |     | veth0 |     | veth0 |
#    ---------     ---------     ---------
#       ns1           ns2           ns3
#
# Test modules:
# XDP modes: generic, native, native + egress_prog
#
# Test cases:
#   ARP: Testing BPF_F_BROADCAST, the ingress interface also should receive
#   the redirects.
#      ns1 -> gw: ns1, ns2, ns3, should receive the arp request
#   IPv4: Testing BPF_F_BROADCAST | BPF_F_EXCLUDE_INGRESS, the ingress
#   interface should not receive the redirects.
#      ns1 -> gw: ns1 should not receive, ns2, ns3 should receive redirects.
#   IPv6: Testing none flag, all the pkts should be redirected back
#      ping test: ns1 -> ns2 (block), echo requests will be redirect back
#   egress_prog:
#      all src mac should be egress interface's mac

# netns numbers
NUM=3
IFACES=""
DRV_MODE="xdpgeneric xdpdrv xdpegress"
PASS=0
FAIL=0
LOG_DIR=$(mktemp -d)

test_pass()
{
	echo "Pass: $@"
	PASS=$((PASS + 1))
}

test_fail()
{
	echo "fail: $@"
	FAIL=$((FAIL + 1))
}

clean_up()
{
	for i in $(seq $NUM); do
		ip link del veth$i 2> /dev/null
		ip netns del ns$i 2> /dev/null
	done
	ip netns del ns0 2> /dev/null
}

# Kselftest framework requirement - SKIP code is 4.
check_env()
{
	ip link set dev lo xdpgeneric off &>/dev/null
	if [ $? -ne 0 ];then
		echo "selftests: [SKIP] Could not run test without the ip xdpgeneric support"
		exit 4
	fi

	which tcpdump &>/dev/null
	if [ $? -ne 0 ];then
		echo "selftests: [SKIP] Could not run test without tcpdump"
		exit 4
	fi
}

setup_ns()
{
	local mode=$1
	IFACES=""

	if [ "$mode" = "xdpegress" ]; then
		mode="xdpdrv"
	fi

	ip netns add ns0
	for i in $(seq $NUM); do
	        ip netns add ns$i
		ip -n ns$i link add veth0 index 2 type veth \
			peer name veth$i netns ns0 index $((1 + $i))
		ip -n ns0 link set veth$i up
		ip -n ns$i link set veth0 up

		ip -n ns$i addr add 192.0.2.$i/24 dev veth0
		ip -n ns$i addr add 2001:db8::$i/64 dev veth0
		# Add a neigh entry for IPv4 ping test
		ip -n ns$i neigh add 192.0.2.253 lladdr 00:00:00:00:00:01 dev veth0
		ip -n ns$i link set veth0 $mode obj \
			xdp_dummy.o sec xdp &> /dev/null || \
			{ test_fail "Unable to load dummy xdp" && exit 1; }
		IFACES="$IFACES veth$i"
		veth_mac[$i]=$(ip -n ns0 link show veth$i | awk '/link\/ether/ {print $2}')
	done
}

do_egress_tests()
{
	local mode=$1

	# mac test
	ip netns exec ns2 tcpdump -e -i veth0 -nn -l -e &> ${LOG_DIR}/mac_ns1-2_${mode}.log &
	ip netns exec ns3 tcpdump -e -i veth0 -nn -l -e &> ${LOG_DIR}/mac_ns1-3_${mode}.log &
	sleep 0.5
	ip netns exec ns1 ping 192.0.2.254 -i 0.1 -c 4 &> /dev/null
	sleep 0.5
	pkill tcpdump

	# mac check
	grep -q "${veth_mac[2]} > ff:ff:ff:ff:ff:ff" ${LOG_DIR}/mac_ns1-2_${mode}.log && \
	       test_pass "$mode mac ns1-2" || test_fail "$mode mac ns1-2"
	grep -q "${veth_mac[3]} > ff:ff:ff:ff:ff:ff" ${LOG_DIR}/mac_ns1-3_${mode}.log && \
		test_pass "$mode mac ns1-3" || test_fail "$mode mac ns1-3"
}

do_ping_tests()
{
	local mode=$1

	# ping6 test: echo request should be redirect back to itself, not others
	ip netns exec ns1 ip neigh add 2001:db8::2 dev veth0 lladdr 00:00:00:00:00:02

	ip netns exec ns1 tcpdump -i veth0 -nn -l -e &> ${LOG_DIR}/ns1-1_${mode}.log &
	ip netns exec ns2 tcpdump -i veth0 -nn -l -e &> ${LOG_DIR}/ns1-2_${mode}.log &
	ip netns exec ns3 tcpdump -i veth0 -nn -l -e &> ${LOG_DIR}/ns1-3_${mode}.log &
	sleep 0.5
	# ARP test
	ip netns exec ns1 arping -q -c 2 -I veth0 192.0.2.254
	# IPv4 test
	ip netns exec ns1 ping 192.0.2.253 -i 0.1 -c 4 &> /dev/null
	# IPv6 test
	ip netns exec ns1 ping6 2001:db8::2 -i 0.1 -c 2 &> /dev/null
	sleep 0.5
	pkill tcpdump

	# All netns should receive the redirect arp requests
	[ $(grep -cF "who-has 192.0.2.254" ${LOG_DIR}/ns1-1_${mode}.log) -eq 4 ] && \
		test_pass "$mode arp(F_BROADCAST) ns1-1" || \
		test_fail "$mode arp(F_BROADCAST) ns1-1"
	[ $(grep -cF "who-has 192.0.2.254" ${LOG_DIR}/ns1-2_${mode}.log) -eq 2 ] && \
		test_pass "$mode arp(F_BROADCAST) ns1-2" || \
		test_fail "$mode arp(F_BROADCAST) ns1-2"
	[ $(grep -cF "who-has 192.0.2.254" ${LOG_DIR}/ns1-3_${mode}.log) -eq 2 ] && \
		test_pass "$mode arp(F_BROADCAST) ns1-3" || \
		test_fail "$mode arp(F_BROADCAST) ns1-3"

	# ns1 should not receive the redirect echo request, others should
	[ $(grep -c "ICMP echo request" ${LOG_DIR}/ns1-1_${mode}.log) -eq 4 ] && \
		test_pass "$mode IPv4 (F_BROADCAST|F_EXCLUDE_INGRESS) ns1-1" || \
		test_fail "$mode IPv4 (F_BROADCAST|F_EXCLUDE_INGRESS) ns1-1"
	[ $(grep -c "ICMP echo request" ${LOG_DIR}/ns1-2_${mode}.log) -eq 4 ] && \
		test_pass "$mode IPv4 (F_BROADCAST|F_EXCLUDE_INGRESS) ns1-2" || \
		test_fail "$mode IPv4 (F_BROADCAST|F_EXCLUDE_INGRESS) ns1-2"
	[ $(grep -c "ICMP echo request" ${LOG_DIR}/ns1-3_${mode}.log) -eq 4 ] && \
		test_pass "$mode IPv4 (F_BROADCAST|F_EXCLUDE_INGRESS) ns1-3" || \
		test_fail "$mode IPv4 (F_BROADCAST|F_EXCLUDE_INGRESS) ns1-3"

	# ns1 should receive the echo request, ns2 should not
	[ $(grep -c "ICMP6, echo request" ${LOG_DIR}/ns1-1_${mode}.log) -eq 4 ] && \
		test_pass "$mode IPv6 (no flags) ns1-1" || \
		test_fail "$mode IPv6 (no flags) ns1-1"
	[ $(grep -c "ICMP6, echo request" ${LOG_DIR}/ns1-2_${mode}.log) -eq 0 ] && \
		test_pass "$mode IPv6 (no flags) ns1-2" || \
		test_fail "$mode IPv6 (no flags) ns1-2"
}

do_tests()
{
	local mode=$1
	local drv_p

	case ${mode} in
		xdpdrv)  drv_p="-N";;
		xdpegress) drv_p="-X";;
		xdpgeneric) drv_p="-S";;
	esac

	ip netns exec ns0 ./xdp_redirect_multi $drv_p $IFACES &> ${LOG_DIR}/xdp_redirect_${mode}.log &
	xdp_pid=$!
	sleep 1
	if ! ps -p $xdp_pid > /dev/null; then
		test_fail "$mode xdp_redirect_multi start failed"
		return 1
	fi

	if [ "$mode" = "xdpegress" ]; then
		do_egress_tests $mode
	else
		do_ping_tests $mode
	fi

	kill $xdp_pid
}

trap clean_up EXIT

check_env

for mode in ${DRV_MODE}; do
	setup_ns $mode
	do_tests $mode
	clean_up
done
rm -rf ${LOG_DIR}

echo "Summary: PASS $PASS, FAIL $FAIL"
[ $FAIL -eq 0 ] && exit 0 || exit 1
