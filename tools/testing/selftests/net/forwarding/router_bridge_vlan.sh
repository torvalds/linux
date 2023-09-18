#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +------------------------------------------------+   +----------------------+
# | H1 (vrf)                                       |   |             H2 (vrf) |
# |    + $h1.555           + $h1.777               |   |  + $h2               |
# |    | 192.0.2.1/28      | 192.0.2.17/28         |   |  | 192.0.2.130/28    |
# |    | 2001:db8:1::1/64  | 2001:db8:3::1/64      |   |  | 192.0.2.146/28    |
# |    | .-----------------'                       |   |  | 2001:db8:2::2/64  |
# |    |/                                          |   |  | 2001:db8:4::2/64  |
# |    + $h1                                       |   |  |                   |
# +----|-------------------------------------------+   +--|-------------------+
#      |                                                  |
# +----|--------------------------------------------------|-------------------+
# | SW |                                                  |                   |
# | +--|-------------------------------+                  + $swp2             |
# | |  + $swp1                         |                    192.0.2.129/28    |
# | |    vid 555 777                   |                    192.0.2.145/28    |
# | |                                  |                    2001:db8:2::1/64  |
# | |  + BR1 (802.1q)                  |                    2001:db8:4::1/64  |
# | |    vid 555 pvid untagged         |                                      |
# | |    192.0.2.2/28                  |                                      |
# | |    192.0.2.18/28                 |                                      |
# | |    2001:db8:1::2/64              |                                      |
# | |    2001:db8:3::2/64              |                                      |
# | +----------------------------------+                                      |
# +---------------------------------------------------------------------------+

ALL_TESTS="
	ping_ipv4
	ping_ipv6
	vlan
	config_777
	ping_ipv4_fails
	ping_ipv6_fails
	ping_ipv4_777
	ping_ipv6_777
	config_555
	ping_ipv4
	ping_ipv6
"
NUM_NETIFS=4
source lib.sh

h1_create()
{
	simple_if_init $h1

	vlan_create $h1 555 v$h1 192.0.2.1/28 2001:db8:1::1/64
	ip -4 route add 192.0.2.128/28 vrf v$h1 nexthop via 192.0.2.2
	ip -6 route add 2001:db8:2::/64 vrf v$h1 nexthop via 2001:db8:1::2

	vlan_create $h1 777 v$h1 192.0.2.17/28 2001:db8:3::1/64
	ip -4 route add 192.0.2.144/28 vrf v$h1 nexthop via 192.0.2.18
	ip -6 route add 2001:db8:4::/64 vrf v$h1 nexthop via 2001:db8:3::2
}

h1_destroy()
{
	ip -6 route del 2001:db8:4::/64 vrf v$h1
	ip -4 route del 192.0.2.144/28 vrf v$h1
	vlan_destroy $h1 777

	ip -6 route del 2001:db8:2::/64 vrf v$h1
	ip -4 route del 192.0.2.128/28 vrf v$h1
	vlan_destroy $h1 555

	simple_if_fini $h1
}

h2_create()
{
	simple_if_init $h2 192.0.2.130/28 2001:db8:2::2/64 \
			   192.0.2.146/28 2001:db8:4::2/64
	ip -4 route add 192.0.2.0/28 vrf v$h2 nexthop via 192.0.2.129
	ip -4 route add 192.0.2.16/28 vrf v$h2 nexthop via 192.0.2.145
	ip -6 route add 2001:db8:1::/64 vrf v$h2 nexthop via 2001:db8:2::1
	ip -6 route add 2001:db8:3::/64 vrf v$h2 nexthop via 2001:db8:4::1
}

h2_destroy()
{
	ip -6 route del 2001:db8:3::/64 vrf v$h2
	ip -6 route del 2001:db8:1::/64 vrf v$h2
	ip -4 route del 192.0.2.16/28 vrf v$h2
	ip -4 route del 192.0.2.0/28 vrf v$h2
	simple_if_fini $h2 192.0.2.146/28 2001:db8:4::2/64 \
			   192.0.2.130/28 2001:db8:2::2/64
}

router_create()
{
	ip link add name br1 type bridge vlan_filtering 1 vlan_default_pvid 0
	ip link set dev br1 up

	ip link set dev $swp1 master br1
	ip link set dev $swp1 up

	bridge vlan add dev br1 vid 555 self pvid untagged
	bridge vlan add dev $swp1 vid 555
	bridge vlan add dev $swp1 vid 777

	__addr_add_del br1 add 192.0.2.2/28 2001:db8:1::2/64
	__addr_add_del br1 add 192.0.2.18/28 2001:db8:3::2/64

	ip link set dev $swp2 up
	__addr_add_del $swp2 add 192.0.2.129/28 2001:db8:2::1/64
	__addr_add_del $swp2 add 192.0.2.145/28 2001:db8:4::1/64
}

router_destroy()
{
	__addr_add_del $swp2 del 192.0.2.145/28 2001:db8:4::1/64
	__addr_add_del $swp2 del 192.0.2.129/28 2001:db8:2::1/64
	ip link set dev $swp2 down

	__addr_add_del br1 del 192.0.2.18/28 2001:db8:3::2/64
	__addr_add_del br1 del 192.0.2.2/28 2001:db8:1::2/64
	ip link set dev $swp1 down
	ip link set dev $swp1 nomaster

	ip link del dev br1
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	vrf_prepare

	h1_create
	h2_create

	router_create

	forwarding_enable
}

config_555()
{
	log_info "Configure VLAN 555 as PVID"

	bridge vlan add dev br1 vid 555 self pvid untagged
	bridge vlan del dev br1 vid 777 self
	sleep 2
}

config_777()
{
	log_info "Configure VLAN 777 as PVID"

	bridge vlan add dev br1 vid 777 self pvid untagged
	bridge vlan del dev br1 vid 555 self
	sleep 2
}

cleanup()
{
	pre_cleanup

	forwarding_restore

	router_destroy

	h2_destroy
	h1_destroy

	vrf_cleanup
}

vlan()
{
	RET=0

	bridge vlan add dev br1 vid 333 self
	check_err $? "Can't add a non-PVID VLAN"
	bridge vlan del dev br1 vid 333 self
	check_err $? "Can't remove a non-PVID VLAN"

	log_test "vlan"
}

ping_ipv4()
{
	ping_test $h1.555 192.0.2.130
}

ping_ipv6()
{
	ping6_test $h1.555 2001:db8:2::2
}

ping_ipv4_fails()
{
	ping_test_fails $h1.555 192.0.2.130 ": via 555"
}

ping_ipv6_fails()
{
	ping6_test_fails $h1.555 2001:db8:2::2 ": via 555"
}

ping_ipv4_777()
{
	ping_test $h1.777 192.0.2.146 ": via 777"
}

ping_ipv6_777()
{
	ping6_test $h1.777 2001:db8:4::2 ": via 777"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
