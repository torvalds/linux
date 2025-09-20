#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test for "tc action mirred egress mirror" when the underlay route points at a
# team device.
#
# +----------------------+                             +----------------------+
# | H1                   |                             |                   H2 |
# |    + $h1.333         |                             |        $h1.555 +     |
# |    | 192.0.2.1/28    |                             |  192.0.2.18/28 |     |
# +----|-----------------+                             +----------------|-----+
#      |                                $h1                             |
#      +---------------------------------+------------------------------+
#                                        |
# +--------------------------------------|------------------------------------+
# | SW                                   o---> mirror                         |
# |                                      |                                    |
# |   +----------------------------------+------------------------------+     |
# |   |                                $swp1                            |     |
# |   + $swp1.333                                             $swp1.555 +     |
# |     192.0.2.2/28                                      192.0.2.17/28       |
# |                                                                           |
# |                                                                           |
# |   + gt4 (gretap)      ,-> + lag1 (team)                                   |
# |     loc=192.0.2.129   |   | 192.0.2.129/28                                |
# |     rem=192.0.2.130 --'   |                                               |
# |     ttl=100               |                                               |
# |     tos=inherit           |                                               |
# |      _____________________|______________________                         |
# |     /                                            \                        |
# |    /                                              \                       |
# |   + $swp3                                          + $swp4                |
# +---|------------------------------------------------|----------------------+
#     |                                                |
# +---|------------------------------------------------|----------------------+
# |   + $h3                                            + $h4               H3 |
# |    \                                              /                       |
# |     \____________________________________________/                        |
# |                           |                                               |
# |                           + lag2 (team)  ------>   + gt4-dst (gretap)     |
# |                             192.0.2.130/28           loc=192.0.2.130      |
# |                                                      rem=192.0.2.129      |
# |                                                      ttl=100              |
# |                                                      tos=inherit          |
# |                                                                           |
# |                                                                           |
# |                                                                           |
# |                                                                           |
# +---------------------------------------------------------------------------+

ALL_TESTS="
	test_mirror_gretap_first
	test_mirror_gretap_second
"

REQUIRE_TEAMD="yes"
NUM_NETIFS=6
source lib.sh
source mirror_lib.sh

vlan_host_create()
{
	local if_name=$1; shift
	local vid=$1; shift
	local vrf_name=$1; shift
	local ips=("${@}")

	vrf_create $vrf_name
	ip link set dev $vrf_name up
	vlan_create $if_name $vid $vrf_name "${ips[@]}"
}

vlan_host_destroy()
{
	local if_name=$1; shift
	local vid=$1; shift
	local vrf_name=$1; shift

	vlan_destroy $if_name $vid
	ip link set dev $vrf_name down
	vrf_destroy $vrf_name
}

h1_create()
{
	vlan_host_create $h1 333 vrf-h1 192.0.2.1/28
	ip -4 route add 192.0.2.16/28 vrf vrf-h1 nexthop via 192.0.2.2
}

h1_destroy()
{
	ip -4 route del 192.0.2.16/28 vrf vrf-h1
	vlan_host_destroy $h1 333 vrf-h1
}

h2_create()
{
	vlan_host_create $h1 555 vrf-h2 192.0.2.18/28
	ip -4 route add 192.0.2.0/28 vrf vrf-h2 nexthop via 192.0.2.17
}

h2_destroy()
{
	ip -4 route del 192.0.2.0/28 vrf vrf-h2
	vlan_host_destroy $h1 555 vrf-h2
}

h3_create_team()
{
	team_create lag2 lacp $h3 $h4
	__simple_if_init lag2 vrf-h3 192.0.2.130/32
	ip -4 route add vrf vrf-h3 192.0.2.129/32 dev lag2
}

h3_destroy_team()
{
	ip -4 route del vrf vrf-h3 192.0.2.129/32 dev lag2
	__simple_if_fini lag2 192.0.2.130/32
	team_destroy lag2

	ip link set dev $h3 down
	ip link set dev $h4 down
}

h3_create()
{
	vrf_create vrf-h3
	ip link set dev vrf-h3 up
	h3_create_team

	tunnel_create gt4-dst gretap 192.0.2.130 192.0.2.129 \
		      ttl 100 tos inherit
	ip link set dev gt4-dst master vrf-h3
	tc qdisc add dev gt4-dst clsact
}

h3_destroy()
{
	tc qdisc del dev gt4-dst clsact
	ip link set dev gt4-dst nomaster
	tunnel_destroy gt4-dst

	h3_destroy_team
	ip link set dev vrf-h3 down
	vrf_destroy vrf-h3
}

switch_create()
{
	ip link set dev $swp1 up
	tc qdisc add dev $swp1 clsact
	vlan_create $swp1 333 "" 192.0.2.2/28
	vlan_create $swp1 555 "" 192.0.2.17/28

	tunnel_create gt4 gretap 192.0.2.129 192.0.2.130 \
		      ttl 100 tos inherit

	ip link set dev $swp3 up
	ip link set dev $swp4 up
	team_create lag1 lacp $swp3 $swp4
	__addr_add_del lag1 add 192.0.2.129/32
	ip -4 route add 192.0.2.130/32 dev lag1
}

switch_destroy()
{
	ip -4 route del 192.0.2.130/32 dev lag1
	__addr_add_del lag1 del 192.0.2.129/32
	team_destroy lag1

	ip link set dev $swp4 down
	ip link set dev $swp3 down

	tunnel_destroy gt4

	vlan_destroy $swp1 555
	vlan_destroy $swp1 333
	tc qdisc del dev $swp1 clsact
	ip link set dev $swp1 down
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp3=${NETIFS[p3]}
	h3=${NETIFS[p4]}

	swp4=${NETIFS[p5]}
	h4=${NETIFS[p6]}

	vrf_prepare

	ip link set dev $h1 up
	h1_create
	h2_create
	h3_create
	switch_create
}

cleanup()
{
	pre_cleanup

	switch_destroy
	h3_destroy
	h2_destroy
	h1_destroy
	ip link set dev $h1 down

	vrf_cleanup
}

test_lag_slave()
{
	local up_dev=$1; shift
	local down_dev=$1; shift
	local what=$1; shift

	RET=0

	mirror_install $swp1 ingress gt4 \
		       "proto 802.1q flower vlan_id 333"
	vlan_capture_install gt4-dst "vlan_ethtype ipv4 ip_proto icmp type 8"

	# Move $down_dev away from the team. That will prompt change in
	# txability of the connected device, without changing its upness. The
	# driver should notice the txability change and move the traffic to the
	# other slave.
	ip link set dev $down_dev nomaster
	sleep 2
	mirror_test vrf-h1 192.0.2.1 192.0.2.18 gt4-dst 100 10

	# Test lack of connectivity when neither slave is txable.
	ip link set dev $up_dev nomaster
	sleep 2
	mirror_test vrf-h1 192.0.2.1 192.0.2.18 gt4-dst 100 0

	vlan_capture_uninstall gt4-dst
	mirror_uninstall $swp1 ingress

	# Recreate H3's team device, because mlxsw, which this test is
	# predominantly mean to test, requires a bottom-up construction and
	# doesn't allow enslavement to a device that already has an upper.
	h3_destroy_team
	h3_create_team
	# Wait for ${h,swp}{3,4}.
	setup_wait

	log_test "$what"
}

test_mirror_gretap_first()
{
	test_lag_slave $h3 $h4 "mirror to gretap: LAG first slave"
}

test_mirror_gretap_second()
{
	test_lag_slave $h4 $h3 "mirror to gretap: LAG second slave"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
