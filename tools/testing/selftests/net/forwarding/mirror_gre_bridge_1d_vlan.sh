#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test uses standard topology for testing gretap. See
# mirror_gre_topo_lib.sh for more details.
#
# Test for "tc action mirred egress mirror" when the underlay route points at a
# bridge device without vlan filtering (802.1d). The device attached to that
# bridge is a VLAN.

ALL_TESTS="
	test_gretap
	test_ip6gretap
	test_gretap_stp
	test_ip6gretap_stp
"

NUM_NETIFS=6
source lib.sh
source mirror_lib.sh
source mirror_gre_lib.sh
source mirror_gre_topo_lib.sh

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	swp3=${NETIFS[p5]}
	h3=${NETIFS[p6]}

	vrf_prepare
	mirror_gre_topo_create

	ip link add name br2 address $(mac_get $swp3) \
		type bridge vlan_filtering 0
	ip link set dev br2 up

	vlan_create $swp3 555

	ip link set dev $swp3.555 master br2
	ip route add 192.0.2.130/32 dev br2
	ip -6 route add 2001:db8:2::2/128 dev br2

	ip address add dev br2 192.0.2.129/32
	ip address add dev br2 2001:db8:2::1/128

	vlan_create $h3 555 v$h3 192.0.2.130/28 2001:db8:2::2/64
}

cleanup()
{
	pre_cleanup

	vlan_destroy $h3 555
	ip link del dev br2
	vlan_destroy $swp3 555

	mirror_gre_topo_destroy
	vrf_cleanup
}

test_vlan_match()
{
	local tundev=$1; shift
	local vlan_match=$1; shift
	local what=$1; shift

	full_test_span_gre_dir_vlan $tundev ingress "$vlan_match" 8 0 "$what"
	full_test_span_gre_dir_vlan $tundev egress "$vlan_match" 0 8 "$what"
}

test_gretap()
{
	test_vlan_match gt4 'skip_hw vlan_id 555 vlan_ethtype ip' \
			"mirror to gretap"
}

test_ip6gretap()
{
	test_vlan_match gt6 'skip_hw vlan_id 555 vlan_ethtype ipv6' \
			"mirror to ip6gretap"
}

test_gretap_stp()
{
	# Sometimes after mirror installation, the neighbor's state is not valid.
	# The reason is that there is no SW datapath activity related to the
	# neighbor for the remote GRE address. Therefore whether the corresponding
	# neighbor will be valid is a matter of luck, and the test is thus racy.
	# Set the neighbor's state to permanent, so it would be always valid.
	ip neigh replace 192.0.2.130 lladdr $(mac_get $h3) \
		nud permanent dev br2
	full_test_span_gre_stp gt4 $swp3.555 "mirror to gretap"
}

test_ip6gretap_stp()
{
	ip neigh replace 2001:db8:2::2 lladdr $(mac_get $h3) \
		nud permanent dev br2
	full_test_span_gre_stp gt6 $swp3.555 "mirror to ip6gretap"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
