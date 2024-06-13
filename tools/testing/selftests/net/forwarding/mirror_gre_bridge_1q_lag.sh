#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test for "tc action mirred egress mirror" when the underlay route points at a
# bridge device with vlan filtering (802.1q), and the egress device is a team
# device.
#
# +----------------------+                             +----------------------+
# | H1                   |                             |                   H2 |
# |     + $h1.333        |                             |        $h1.555 +     |
# |     | 192.0.2.1/28   |                             |  192.0.2.18/28 |     |
# +-----|----------------+                             +----------------|-----+
#       |                               $h1                             |
#       +--------------------------------+------------------------------+
#                                        |
# +--------------------------------------|------------------------------------+
# | SW                                   o---> mirror                         |
# |                                      |                                    |
# |     +--------------------------------+------------------------------+     |
# |     |                              $swp1                            |     |
# |     + $swp1.333                                           $swp1.555 +     |
# |       192.0.2.2/28                                    192.0.2.17/28       |
# |                                                                           |
# | +-----------------------------------------------------------------------+ |
# | |                        BR1 (802.1q)                                   | |
# | |     + lag (team)       192.0.2.129/28                                 | |
# | |    / \                 2001:db8:2::1/64                               | |
# | +---/---\---------------------------------------------------------------+ |
# |    /     \                                                            ^   |
# |   |       \                                        + gt4 (gretap)     |   |
# |   |        \                                         loc=192.0.2.129  |   |
# |   |         \                                        rem=192.0.2.130 -+   |
# |   |          \                                       ttl=100              |
# |   |           \                                      tos=inherit          |
# |   |            \                                                          |
# |   |             \_________________________________                        |
# |   |                                               \                       |
# |   + $swp3                                          + $swp4                |
# +---|------------------------------------------------|----------------------+
#     |                                                |
# +---|----------------------+                     +---|----------------------+
# |   + $h3               H3 |                     |   + $h4               H4 |
# |     192.0.2.130/28       |                     |     192.0.2.130/28       |
# |     2001:db8:2::2/64     |                     |     2001:db8:2::2/64     |
# +--------------------------+                     +--------------------------+

ALL_TESTS="
	test_mirror_gretap_first
	test_mirror_gretap_second
"

NUM_NETIFS=6
source lib.sh
source mirror_lib.sh
source mirror_gre_lib.sh

require_command $ARPING

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

h3_create()
{
	simple_if_init $h3 192.0.2.130/28
	tc qdisc add dev $h3 clsact
}

h3_destroy()
{
	tc qdisc del dev $h3 clsact
	simple_if_fini $h3 192.0.2.130/28
}

h4_create()
{
	simple_if_init $h4 192.0.2.130/28
	tc qdisc add dev $h4 clsact
}

h4_destroy()
{
	tc qdisc del dev $h4 clsact
	simple_if_fini $h4 192.0.2.130/28
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

	ip link add name br1 type bridge vlan_filtering 1

	team_create lag loadbalance $swp3 $swp4
	ip link set dev lag master br1

	ip link set dev br1 up
	__addr_add_del br1 add 192.0.2.129/32
	ip -4 route add 192.0.2.130/32 dev br1
}

switch_destroy()
{
	ip link set dev lag nomaster
	team_destroy lag

	ip -4 route del 192.0.2.130/32 dev br1
	__addr_add_del br1 del 192.0.2.129/32
	ip link set dev br1 down
	ip link del dev br1

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
	h4_create
	switch_create

	forwarding_enable

	trap_install $h3 ingress
	trap_install $h4 ingress
}

cleanup()
{
	pre_cleanup

	trap_uninstall $h4 ingress
	trap_uninstall $h3 ingress

	forwarding_restore

	switch_destroy
	h4_destroy
	h3_destroy
	h2_destroy
	h1_destroy
	ip link set dev $h1 down

	vrf_cleanup
}

test_lag_slave()
{
	local host_dev=$1; shift
	local up_dev=$1; shift
	local down_dev=$1; shift
	local what=$1; shift

	RET=0

	tc filter add dev $swp1 ingress pref 999 \
		proto 802.1q flower vlan_ethtype arp $tcflags \
		action pass
	mirror_install $swp1 ingress gt4 \
		"proto 802.1q flower vlan_id 333 $tcflags"

	# Test connectivity through $up_dev when $down_dev is set down.
	ip link set dev $down_dev down
	ip neigh flush dev br1
	setup_wait_dev $up_dev
	setup_wait_dev $host_dev
	$ARPING -I br1 192.0.2.130 -qfc 1
	sleep 2
	mirror_test vrf-h1 192.0.2.1 192.0.2.18 $host_dev 1 10

	# Test lack of connectivity when both slaves are down.
	ip link set dev $up_dev down
	sleep 2
	mirror_test vrf-h1 192.0.2.1 192.0.2.18 $h3 1 0
	mirror_test vrf-h1 192.0.2.1 192.0.2.18 $h4 1 0

	ip link set dev $up_dev up
	ip link set dev $down_dev up
	mirror_uninstall $swp1 ingress
	tc filter del dev $swp1 ingress pref 999

	log_test "$what ($tcflags)"
}

test_mirror_gretap_first()
{
	test_lag_slave $h3 $swp3 $swp4 "mirror to gretap: LAG first slave"
}

test_mirror_gretap_second()
{
	test_lag_slave $h4 $swp4 $swp3 "mirror to gretap: LAG second slave"
}

test_all()
{
	slow_path_trap_install $swp1 ingress
	slow_path_trap_install $swp1 egress

	tests_run

	slow_path_trap_uninstall $swp1 egress
	slow_path_trap_uninstall $swp1 ingress
}

trap cleanup EXIT

setup_prepare
setup_wait

tcflags="skip_hw"
test_all

if ! tc_offload_check; then
	echo "WARN: Could not test offloaded functionality"
else
	tcflags="skip_sw"
	test_all
fi

exit $EXIT_STATUS
