#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ipv6=false

source ./hsr_common.sh

do_complete_ping_test()
{
	echo "INFO: Initial validation ping (HSR-SAN/RedBox)."
	# Each node has to be able to reach each one.
	do_ping "${ns1}" 100.64.0.2
	do_ping "${ns2}" 100.64.0.1
	# Ping between SANs (test bridge)
	do_ping "${ns4}" 100.64.0.51
	do_ping "${ns5}" 100.64.0.41
	# Ping from SANs to hsr1 (via hsr2) (and opposite)
	do_ping "${ns3}" 100.64.0.1
	do_ping "${ns1}" 100.64.0.3
	do_ping "${ns1}" 100.64.0.41
	do_ping "${ns4}" 100.64.0.1
	do_ping "${ns1}" 100.64.0.51
	do_ping "${ns5}" 100.64.0.1
	stop_if_error "Initial validation failed."

	# Wait for MGNT HSR frames being received and nodes being
	# merged.
	sleep 5

	echo "INFO: Longer ping test (HSR-SAN/RedBox)."
	# Ping from SAN to hsr1 (via hsr2)
	do_ping_long "${ns3}" 100.64.0.1
	# Ping from hsr1 (via hsr2) to SANs (and opposite)
	do_ping_long "${ns1}" 100.64.0.3
	do_ping_long "${ns1}" 100.64.0.41
	do_ping_long "${ns4}" 100.64.0.1
	do_ping_long "${ns1}" 100.64.0.51
	do_ping_long "${ns5}" 100.64.0.1
	stop_if_error "Longer ping test failed."

	echo "INFO: All good."
}

setup_hsr_interfaces()
{
	local HSRv="$1"

	echo "INFO: preparing interfaces for HSRv${HSRv} (HSR-SAN/RedBox)."
#
# IPv4 addresses (100.64.X.Y/24), and [X.Y] is presented on below diagram:
#
#
# |NS1                     |               |NS4                |
# |       [0.1]            |               |                   |
# |    /-- hsr1 --\        |               |    [0.41]         |
# | ns1eth1     ns1eth2    |               |    ns4eth1 (SAN)  |
# |------------------------|               |-------------------|
#      |            |                                |
#      |            |                                |
#      |            |                                |
# |------------------------|   |-------------------------------|
# | ns2eth1     ns2eth2    |   |                  ns3eth2      |
# |    \-- hsr2 --/        |   |                 /             |
# |      [0.2] \           |   |                /              |  |------------|
# |             ns2eth3    |---| ns3eth1 -- ns3br1 -- ns3eth3--|--| ns5eth1    |
# |             (interlink)|   | [0.3]      [0.11]             |  | [0.51]     |
# |NS2 (RedBOX)            |   |NS3 (BR)                       |  | NS5 (SAN)  |
#
#
	# Check if iproute2 supports adding interlink port to hsrX device
	ip link help hsr | grep -q INTERLINK
	[ $? -ne 0 ] && { echo "iproute2: HSR interlink interface not supported!"; exit 0; }

	# Create interfaces for name spaces
	ip link add ns1eth1 netns "${ns1}" type veth peer name ns2eth1 netns "${ns2}"
	ip link add ns1eth2 netns "${ns1}" type veth peer name ns2eth2 netns "${ns2}"
	ip link add ns2eth3 netns "${ns2}" type veth peer name ns3eth1 netns "${ns3}"
	ip link add ns3eth2 netns "${ns3}" type veth peer name ns4eth1 netns "${ns4}"
	ip link add ns3eth3 netns "${ns3}" type veth peer name ns5eth1 netns "${ns5}"

	sleep 1

	ip -n "${ns1}" link set ns1eth1 up
	ip -n "${ns1}" link set ns1eth2 up

	ip -n "${ns2}" link set ns2eth1 up
	ip -n "${ns2}" link set ns2eth2 up
	ip -n "${ns2}" link set ns2eth3 up

	ip -n "${ns3}" link add name ns3br1 type bridge
	ip -n "${ns3}" link set ns3br1 up
	ip -n "${ns3}" link set ns3eth1 master ns3br1 up
	ip -n "${ns3}" link set ns3eth2 master ns3br1 up
	ip -n "${ns3}" link set ns3eth3 master ns3br1 up

	ip -n "${ns4}" link set ns4eth1 up
	ip -n "${ns5}" link set ns5eth1 up

	ip -net "$ns1" link set address 00:11:22:00:01:01 dev ns1eth1
	ip -net "$ns1" link set address 00:11:22:00:01:02 dev ns1eth2

	ip -net "$ns2" link set address 00:11:22:00:02:01 dev ns2eth1
	ip -net "$ns2" link set address 00:11:22:00:02:02 dev ns2eth2
	ip -net "$ns2" link set address 00:11:22:00:02:03 dev ns2eth3

	ip -net "$ns3" link set address 00:11:22:00:03:11 dev ns3eth1
	ip -net "$ns3" link set address 00:11:22:00:03:11 dev ns3eth2
	ip -net "$ns3" link set address 00:11:22:00:03:11 dev ns3eth3
	ip -net "$ns3" link set address 00:11:22:00:03:11 dev ns3br1

	ip -net "$ns4" link set address 00:11:22:00:04:01 dev ns4eth1
	ip -net "$ns5" link set address 00:11:22:00:05:01 dev ns5eth1

	ip -net "${ns1}" link add name hsr1 type hsr slave1 ns1eth1 slave2 ns1eth2 supervision 45 version ${HSRv} proto 0
	ip -net "${ns2}" link add name hsr2 type hsr slave1 ns2eth1 slave2 ns2eth2 interlink ns2eth3 supervision 45 version ${HSRv} proto 0

	ip -n "${ns1}" addr add 100.64.0.1/24 dev hsr1
	ip -n "${ns2}" addr add 100.64.0.2/24 dev hsr2
	ip -n "${ns3}" addr add 100.64.0.11/24 dev ns3br1
	ip -n "${ns3}" addr add 100.64.0.3/24 dev ns3eth1
	ip -n "${ns4}" addr add 100.64.0.41/24 dev ns4eth1
	ip -n "${ns5}" addr add 100.64.0.51/24 dev ns5eth1

	ip -n "${ns1}" link set hsr1 up
	ip -n "${ns2}" link set hsr2 up
}

check_prerequisites
setup_ns ns1 ns2 ns3 ns4 ns5

trap cleanup_all_ns EXIT

setup_hsr_interfaces 1
do_complete_ping_test

exit $ret
