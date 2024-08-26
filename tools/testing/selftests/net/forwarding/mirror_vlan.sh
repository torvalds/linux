#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test uses standard topology for testing mirroring. See mirror_topo_lib.sh
# for more details.
#
# Test for "tc action mirred egress mirror" that mirrors to a vlan device.

ALL_TESTS="
	test_vlan
	test_tagged_vlan
"

NUM_NETIFS=6
source lib.sh
source mirror_lib.sh
source mirror_topo_lib.sh

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	swp3=${NETIFS[p5]}
	h3=${NETIFS[p6]}

	vrf_prepare
	mirror_topo_create

	vlan_create $swp3 555

	vlan_create $h3 555 v$h3
	matchall_sink_create $h3.555

	vlan_create $h1 111 v$h1 192.0.2.17/28
	bridge vlan add dev $swp1 vid 111

	vlan_create $h2 111 v$h2 192.0.2.18/28
	bridge vlan add dev $swp2 vid 111

	trap_install $h3 ingress
}

cleanup()
{
	pre_cleanup

	trap_uninstall $h3 ingress

	vlan_destroy $h2 111
	vlan_destroy $h1 111
	vlan_destroy $h3 555
	vlan_destroy $swp3 555

	mirror_topo_destroy
	vrf_cleanup
}

test_vlan_dir()
{
	local direction=$1; shift
	local forward_type=$1; shift
	local backward_type=$1; shift

	RET=0

	mirror_install $swp1 $direction $swp3.555 "matchall"
	test_span_dir "$h3.555" "$forward_type" "$backward_type"
	mirror_uninstall $swp1 $direction

	log_test "$direction mirror to vlan"
}

test_vlan()
{
	test_vlan_dir ingress 8 0
	test_vlan_dir egress 0 8
}

test_tagged_vlan_dir()
{
	local direction=$1; shift
	local forward_type=$1; shift
	local backward_type=$1; shift

	RET=0

	mirror_install $swp1 $direction $swp3.555 "matchall"
	do_test_span_vlan_dir_ips '>= 10' "$h3.555" 111 ip 192.0.2.17 192.0.2.18
	do_test_span_vlan_dir_ips  0 "$h3.555" 555 ip 192.0.2.17 192.0.2.18
	mirror_uninstall $swp1 $direction

	log_test "$direction mirror tagged to vlan"
}

test_tagged_vlan()
{
	test_tagged_vlan_dir ingress 8 0
	test_tagged_vlan_dir egress 0 8
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
