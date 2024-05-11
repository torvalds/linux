#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ipv6=false

source ./hsr_common.sh

do_complete_ping_test()
{
	echo "INFO: Initial validation ping (HSR-SAN/RedBox)."
	# Each node has to be able each one.
	do_ping "${ns1}" 100.64.0.2
	do_ping "${ns2}" 100.64.0.1
	# Ping from SAN to hsr1 (via hsr2)
	do_ping "${ns3}" 100.64.0.1
	do_ping "${ns1}" 100.64.0.3
	stop_if_error "Initial validation failed."

	# Wait for MGNT HSR frames being received and nodes being
	# merged.
	sleep 5

	echo "INFO: Longer ping test (HSR-SAN/RedBox)."
	# Ping from SAN to hsr1 (via hsr2)
	do_ping_long "${ns3}" 100.64.0.1
	# Ping from hsr1 (via hsr2) to SAN
	do_ping_long "${ns1}" 100.64.0.3
	stop_if_error "Longer ping test failed."

	echo "INFO: All good."
}

setup_hsr_interfaces()
{
	local HSRv="$1"

	echo "INFO: preparing interfaces for HSRv${HSRv} (HSR-SAN/RedBox)."

#       |NS1                     |
#       |                        |
#       |    /-- hsr1 --\        |
#       | ns1eth1     ns1eth2    |
#       |------------------------|
#            |            |
#            |            |
#            |            |
#       |------------------------|        |-----------|
#       | ns2eth1     ns2eth2    |        |           |
#       |    \-- hsr2 --/        |        |           |
#       |            \           |        |           |
#       |             ns2eth3    |--------| ns3eth1   |
#       |             (interlink)|        |           |
#       |NS2 (RedBOX)            |        |NS3 (SAN)  |
#
	# Check if iproute2 supports adding interlink port to hsrX device
	ip link help hsr | grep -q INTERLINK
	[ $? -ne 0 ] && { echo "iproute2: HSR interlink interface not supported!"; exit 0; }

	# Create interfaces for name spaces
	ip link add ns1eth1 netns "${ns1}" type veth peer name ns2eth1 netns "${ns2}"
	ip link add ns1eth2 netns "${ns1}" type veth peer name ns2eth2 netns "${ns2}"
	ip link add ns3eth1 netns "${ns3}" type veth peer name ns2eth3 netns "${ns2}"

	sleep 1

	ip -n "${ns1}" link set ns1eth1 up
	ip -n "${ns1}" link set ns1eth2 up

	ip -n "${ns2}" link set ns2eth1 up
	ip -n "${ns2}" link set ns2eth2 up
	ip -n "${ns2}" link set ns2eth3 up

	ip -n "${ns3}" link set ns3eth1 up

	ip -net "${ns1}" link add name hsr1 type hsr slave1 ns1eth1 slave2 ns1eth2 supervision 45 version ${HSRv} proto 0
	ip -net "${ns2}" link add name hsr2 type hsr slave1 ns2eth1 slave2 ns2eth2 interlink ns2eth3 supervision 45 version ${HSRv} proto 0

	ip -n "${ns1}" addr add 100.64.0.1/24 dev hsr1
	ip -n "${ns2}" addr add 100.64.0.2/24 dev hsr2
	ip -n "${ns3}" addr add 100.64.0.3/24 dev ns3eth1

	ip -n "${ns1}" link set hsr1 up
	ip -n "${ns2}" link set hsr2 up
}

check_prerequisites
setup_ns ns1 ns2 ns3

trap cleanup_all_ns EXIT

setup_hsr_interfaces 1
do_complete_ping_test

exit $ret
