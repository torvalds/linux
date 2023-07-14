#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright 2020 NXP

WAIT_TIME=1
NUM_NETIFS=4
STABLE_MAC_ADDRS=yes
lib_dir=$(dirname $0)/../../../net/forwarding
source $lib_dir/tc_common.sh
source $lib_dir/lib.sh

require_command tcpdump

h1=${NETIFS[p1]}
swp1=${NETIFS[p2]}
swp2=${NETIFS[p3]}
h2=${NETIFS[p4]}

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
	ip link set $swp1 up
	ip link set $swp2 up
	ip link set $h2 up
	ip link set $h1 up

	create_tcam_skeleton $swp1

	ip link add br0 type bridge
	ip link set $swp1 master br0
	ip link set $swp2 master br0
	ip link set br0 up

	ip link add link $h1 name $h1.100 type vlan id 100
	ip link set $h1.100 up

	ip link add link $h1 name $h1.200 type vlan id 200
	ip link set $h1.200 up

	tc filter add dev $swp1 ingress chain $(IS1 1) pref 1 \
		protocol 802.1Q flower skip_sw vlan_id 100 \
		action vlan pop \
		action goto chain $(IS1 2)

	tc filter add dev $swp1 egress chain $(ES0) pref 1 \
		flower skip_sw indev $swp2 \
		action vlan push protocol 802.1Q id 100

	tc filter add dev $swp1 ingress chain $(IS1 0) pref 2 \
		protocol ipv4 flower skip_sw src_ip 10.1.1.2 \
		action skbedit priority 7 \
		action goto chain $(IS1 1)

	tc filter add dev $swp1 ingress chain $(IS2 0 0) pref 1 \
		protocol ipv4 flower skip_sw ip_proto udp dst_port 5201 \
		action police rate 50mbit burst 64k conform-exceed drop/pipe \
		action goto chain $(IS2 1 0)
}

cleanup()
{
	ip link del $h1.200
	ip link del $h1.100
	tc qdisc del dev $swp1 clsact
	ip link del br0
}

test_vlan_pop()
{
	local h1_mac=$(mac_get $h1)
	local h2_mac=$(mac_get $h2)

	RET=0

	tcpdump_start $h2

	# Work around Mausezahn VLAN builder bug
	# (https://github.com/netsniff-ng/netsniff-ng/issues/225) by using
	# an 8021q upper
	$MZ $h1.100 -q -c 1 -p 64 -a $h1_mac -b $h2_mac -t ip

	sleep 1

	tcpdump_stop $h2

	tcpdump_show $h2 | grep -q "$h1_mac > $h2_mac, ethertype IPv4"
	check_err "$?" "untagged reception"

	tcpdump_cleanup $h2

	log_test "VLAN pop"
}

test_vlan_push()
{
	local h1_mac=$(mac_get $h1)
	local h2_mac=$(mac_get $h2)

	RET=0

	tcpdump_start $h1.100

	$MZ $h2 -q -c 1 -p 64 -a $h2_mac -b $h1_mac -t ip

	sleep 1

	tcpdump_stop $h1.100

	tcpdump_show $h1.100 | grep -q "$h2_mac > $h1_mac"
	check_err "$?" "tagged reception"

	tcpdump_cleanup $h1.100

	log_test "VLAN push"
}

test_vlan_ingress_modify()
{
	local h1_mac=$(mac_get $h1)
	local h2_mac=$(mac_get $h2)

	RET=0

	ip link set br0 type bridge vlan_filtering 1
	bridge vlan add dev $swp1 vid 200
	bridge vlan add dev $swp1 vid 300
	bridge vlan add dev $swp2 vid 300

	tc filter add dev $swp1 ingress chain $(IS1 2) pref 3 \
		protocol 802.1Q flower skip_sw vlan_id 200 src_mac $h1_mac \
		action vlan modify id 300 \
		action goto chain $(IS2 0 0)

	tcpdump_start $h2

	$MZ $h1.200 -q -c 1 -p 64 -a $h1_mac -b $h2_mac -t ip

	sleep 1

	tcpdump_stop $h2

	tcpdump_show $h2 | grep -q "$h1_mac > $h2_mac, .* vlan 300"
	check_err "$?" "tagged reception"

	tcpdump_cleanup $h2

	tc filter del dev $swp1 ingress chain $(IS1 2) pref 3

	bridge vlan del dev $swp1 vid 200
	bridge vlan del dev $swp1 vid 300
	bridge vlan del dev $swp2 vid 300
	ip link set br0 type bridge vlan_filtering 0

	log_test "Ingress VLAN modification"
}

test_vlan_egress_modify()
{
	local h1_mac=$(mac_get $h1)
	local h2_mac=$(mac_get $h2)

	RET=0

	tc qdisc add dev $swp2 clsact

	ip link set br0 type bridge vlan_filtering 1
	bridge vlan add dev $swp1 vid 200
	bridge vlan add dev $swp2 vid 200

	tc filter add dev $swp2 egress chain $(ES0) pref 3 \
		protocol 802.1Q flower skip_sw vlan_id 200 vlan_prio 0 \
		action vlan modify id 300 priority 7

	tcpdump_start $h2

	$MZ $h1.200 -q -c 1 -p 64 -a $h1_mac -b $h2_mac -t ip

	sleep 1

	tcpdump_stop $h2

	tcpdump_show $h2 | grep -q "$h1_mac > $h2_mac, .* vlan 300"
	check_err "$?" "tagged reception"

	tcpdump_cleanup $h2

	tc filter del dev $swp2 egress chain $(ES0) pref 3
	tc qdisc del dev $swp2 clsact

	bridge vlan del dev $swp1 vid 200
	bridge vlan del dev $swp2 vid 200
	ip link set br0 type bridge vlan_filtering 0

	log_test "Egress VLAN modification"
}

test_skbedit_priority()
{
	local h1_mac=$(mac_get $h1)
	local h2_mac=$(mac_get $h2)
	local num_pkts=100

	before=$(ethtool_stats_get $swp1 'rx_green_prio_7')

	$MZ $h1 -q -c $num_pkts -p 64 -a $h1_mac -b $h2_mac -t ip -A 10.1.1.2

	after=$(ethtool_stats_get $swp1 'rx_green_prio_7')

	if [ $((after - before)) = $num_pkts ]; then
		RET=0
	else
		RET=1
	fi

	log_test "Frame prioritization"
}

trap cleanup EXIT

ALL_TESTS="
	test_vlan_pop
	test_vlan_push
	test_vlan_ingress_modify
	test_vlan_egress_modify
	test_skbedit_priority
"

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
