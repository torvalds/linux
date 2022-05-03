#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright 2020 NXP

WAIT_TIME=1
NUM_NETIFS=4
lib_dir=$(dirname $0)/../../../net/forwarding
source $lib_dir/tc_common.sh
source $lib_dir/lib.sh

require_command tcpdump

#
#   +---------------------------------------------+
#   |       DUT ports         Generator ports     |
#   | +--------+ +--------+ +--------+ +--------+ |
#   | |        | |        | |        | |        | |
#   | |  eth0  | |  eth1  | |  eth2  | |  eth3  | |
#   | |        | |        | |        | |        | |
#   +-+--------+-+--------+-+--------+-+--------+-+
#          |         |           |          |
#          |         |           |          |
#          |         +-----------+          |
#          |                                |
#          +--------------------------------+

eth0=${NETIFS[p1]}
eth1=${NETIFS[p2]}
eth2=${NETIFS[p3]}
eth3=${NETIFS[p4]}

eth0_mac="de:ad:be:ef:00:00"
eth1_mac="de:ad:be:ef:00:01"
eth2_mac="de:ad:be:ef:00:02"
eth3_mac="de:ad:be:ef:00:03"

# Helpers to map a VCAP IS1 and VCAP IS2 lookup and policy to a chain number
# used by the kernel driver. The numbers are:
# VCAP IS1 lookup 0:            10000
# VCAP IS1 lookup 1:            11000
# VCAP IS1 lookup 2:            12000
# VCAP IS2 lookup 0 policy 0:   20000
# VCAP IS2 lookup 0 policy 1:   20001
# VCAP IS2 lookup 0 policy 255: 20255
# VCAP IS2 lookup 1 policy 0:   21000
# VCAP IS2 lookup 1 policy 1:   21001
# VCAP IS2 lookup 1 policy 255: 21255
IS1()
{
	local lookup=$1

	echo $((10000 + 1000 * lookup))
}

IS2()
{
	local lookup=$1
	local pag=$2

	echo $((20000 + 1000 * lookup + pag))
}

ES0()
{
	echo 0
}

# The Ocelot switches have a fixed ingress pipeline composed of:
#
# +----------------------------------------------+      +-----------------------------------------+
# |                   VCAP IS1                   |      |                  VCAP IS2               |
# |                                              |      |                                         |
# | +----------+    +----------+    +----------+ |      |            +----------+    +----------+ |
# | | Lookup 0 |    | Lookup 1 |    | Lookup 2 | | --+------> PAG 0: | Lookup 0 | -> | Lookup 1 | |
# | +----------+ -> +----------+ -> +----------+ |   |  |            +----------+    +----------+ |
# | |key&action|    |key&action|    |key&action| |   |  |            |key&action|    |key&action| |
# | |key&action|    |key&action|    |key&action| |   |  |            |    ..    |    |    ..    | |
# | |    ..    |    |    ..    |    |    ..    | |   |  |            +----------+    +----------+ |
# | +----------+    +----------+    +----------+ |   |  |                                         |
# |                                 selects PAG  |   |  |            +----------+    +----------+ |
# +----------------------------------------------+   +------> PAG 1: | Lookup 0 | -> | Lookup 1 | |
#                                                    |  |            +----------+    +----------+ |
#                                                    |  |            |key&action|    |key&action| |
#                                                    |  |            |    ..    |    |    ..    | |
#                                                    |  |            +----------+    +----------+ |
#                                                    |  |      ...                                |
#                                                    |  |                                         |
#                                                    |  |            +----------+    +----------+ |
#                                                    +----> PAG 254: | Lookup 0 | -> | Lookup 1 | |
#                                                    |  |            +----------+    +----------+ |
#                                                    |  |            |key&action|    |key&action| |
#                                                    |  |            |    ..    |    |    ..    | |
#                                                    |  |            +----------+    +----------+ |
#                                                    |  |                                         |
#                                                    |  |            +----------+    +----------+ |
#                                                    +----> PAG 255: | Lookup 0 | -> | Lookup 1 | |
#                                                       |            +----------+    +----------+ |
#                                                       |            |key&action|    |key&action| |
#                                                       |            |    ..    |    |    ..    | |
#                                                       |            +----------+    +----------+ |
#                                                       +-----------------------------------------+
#
# Both the VCAP IS1 (Ingress Stage 1) and IS2 (Ingress Stage 2) are indexed
# (looked up) multiple times: IS1 3 times, and IS2 2 times. Each filter
# (key and action pair) can be configured to only match during the first, or
# second, etc, lookup.
#
# During one TCAM lookup, the filter processing stops at the first entry that
# matches, then the pipeline jumps to the next lookup.
# The driver maps each individual lookup of each individual ingress TCAM to a
# separate chain number. For correct rule offloading, it is mandatory that each
# filter installed in one TCAM is terminated by a non-optional GOTO action to
# the next lookup from the fixed pipeline.
#
# A chain can only be used if there is a GOTO action correctly set up from the
# prior lookup in the processing pipeline. Setting up all chains is not
# mandatory.

# NOTE: VCAP IS1 currently uses only S1_NORMAL half keys and VCAP IS2
# dynamically chooses between MAC_ETYPE, ARP, IP4_TCP_UDP, IP4_OTHER, which are
# all half keys as well.

create_tcam_skeleton()
{
	local eth=$1

	tc qdisc add dev $eth clsact

	# VCAP IS1 is the Ingress Classification TCAM and can offload the
	# following actions:
	# - skbedit priority
	# - vlan pop
	# - vlan modify
	# - goto (only in lookup 2, the last IS1 lookup)
	tc filter add dev $eth ingress chain 0 pref 49152 flower \
		skip_sw action goto chain $(IS1 0)
	tc filter add dev $eth ingress chain $(IS1 0) pref 49152 \
		flower skip_sw action goto chain $(IS1 1)
	tc filter add dev $eth ingress chain $(IS1 1) pref 49152 \
		flower skip_sw action goto chain $(IS1 2)
	tc filter add dev $eth ingress chain $(IS1 2) pref 49152 \
		flower skip_sw action goto chain $(IS2 0 0)

	# VCAP IS2 is the Security Enforcement ingress TCAM and can offload the
	# following actions:
	# - trap
	# - drop
	# - police
	# The two VCAP IS2 lookups can be segmented into up to 256 groups of
	# rules, called Policies. A Policy is selected through the Policy
	# Association Group (PAG) action of VCAP IS1 (which is the
	# GOTO offload).
	tc filter add dev $eth ingress chain $(IS2 0 0) pref 49152 \
		flower skip_sw action goto chain $(IS2 1 0)
}

setup_prepare()
{
	create_tcam_skeleton $eth0

	ip link add br0 type bridge
	ip link set $eth0 master br0
	ip link set $eth1 master br0
	ip link set br0 up

	ip link add link $eth3 name $eth3.100 type vlan id 100
	ip link set $eth3.100 up

	ip link add link $eth3 name $eth3.200 type vlan id 200
	ip link set $eth3.200 up

	tc filter add dev $eth0 ingress chain $(IS1 1) pref 1 \
		protocol 802.1Q flower skip_sw vlan_id 100 \
		action vlan pop \
		action goto chain $(IS1 2)

	tc filter add dev $eth0 egress chain $(ES0) pref 1 \
		flower skip_sw indev $eth1 \
		action vlan push protocol 802.1Q id 100

	tc filter add dev $eth0 ingress chain $(IS1 0) pref 2 \
		protocol ipv4 flower skip_sw src_ip 10.1.1.2 \
		action skbedit priority 7 \
		action goto chain $(IS1 1)

	tc filter add dev $eth0 ingress chain $(IS2 0 0) pref 1 \
		protocol ipv4 flower skip_sw ip_proto udp dst_port 5201 \
		action police rate 50mbit burst 64k conform-exceed drop/pipe \
		action goto chain $(IS2 1 0)
}

cleanup()
{
	ip link del $eth3.200
	ip link del $eth3.100
	tc qdisc del dev $eth0 clsact
	ip link del br0
}

test_vlan_pop()
{
	printf "Testing VLAN pop..			"

	tcpdump_start $eth2

	# Work around Mausezahn VLAN builder bug
	# (https://github.com/netsniff-ng/netsniff-ng/issues/225) by using
	# an 8021q upper
	$MZ $eth3.100 -q -c 1 -p 64 -a $eth3_mac -b $eth2_mac -t ip

	sleep 1

	tcpdump_stop

	if tcpdump_show | grep -q "$eth3_mac > $eth2_mac, ethertype IPv4"; then
		echo "OK"
	else
		echo "FAIL"
	fi

	tcpdump_cleanup
}

test_vlan_push()
{
	printf "Testing VLAN push..			"

	tcpdump_start $eth3.100

	$MZ $eth2 -q -c 1 -p 64 -a $eth2_mac -b $eth3_mac -t ip

	sleep 1

	tcpdump_stop

	if tcpdump_show | grep -q "$eth2_mac > $eth3_mac"; then
		echo "OK"
	else
		echo "FAIL"
	fi

	tcpdump_cleanup
}

test_vlan_modify()
{
	printf "Testing VLAN modification..		"

	ip link set br0 type bridge vlan_filtering 1
	bridge vlan add dev $eth0 vid 200
	bridge vlan add dev $eth0 vid 300
	bridge vlan add dev $eth1 vid 300

	tc filter add dev $eth0 ingress chain $(IS1 2) pref 3 \
		protocol 802.1Q flower skip_sw vlan_id 200 \
		action vlan modify id 300 \
		action goto chain $(IS2 0 0)

	tcpdump_start $eth2

	$MZ $eth3.200 -q -c 1 -p 64 -a $eth3_mac -b $eth2_mac -t ip

	sleep 1

	tcpdump_stop

	if tcpdump_show | grep -q "$eth3_mac > $eth2_mac, .* vlan 300"; then
		echo "OK"
	else
		echo "FAIL"
	fi

	tcpdump_cleanup

	tc filter del dev $eth0 ingress chain $(IS1 2) pref 3

	bridge vlan del dev $eth0 vid 200
	bridge vlan del dev $eth0 vid 300
	bridge vlan del dev $eth1 vid 300
	ip link set br0 type bridge vlan_filtering 0
}

test_skbedit_priority()
{
	local num_pkts=100

	printf "Testing frame prioritization..		"

	before=$(ethtool_stats_get $eth0 'rx_green_prio_7')

	$MZ $eth3 -q -c $num_pkts -p 64 -a $eth3_mac -b $eth2_mac -t ip -A 10.1.1.2

	after=$(ethtool_stats_get $eth0 'rx_green_prio_7')

	if [ $((after - before)) = $num_pkts ]; then
		echo "OK"
	else
		echo "FAIL"
	fi
}

trap cleanup EXIT

ALL_TESTS="
	test_vlan_pop
	test_vlan_push
	test_vlan_modify
	test_skbedit_priority
"

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
