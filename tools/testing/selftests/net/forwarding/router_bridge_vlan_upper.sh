#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +------------------------+                           +----------------------+
# | H1 (vrf)               |                           |             H2 (vrf) |
# |    + $h1.555           |                           |  + $h2.777           |
# |    | 192.0.2.1/28      |                           |  | 192.0.2.18/28     |
# |    | 2001:db8:1::1/64  |                           |  | 2001:db8:2::2/64  |
# |    |                   |                           |  |                   |
# |    + $h1               |                           |  + $h2               |
# +----|-------------------+                           +--|-------------------+
#      |                                                  |
# +----|--------------------------------------------------|-------------------+
# | SW |                                                  |                   |
# | +--|--------------------------------------------------|-----------------+ |
# | |  + $swp1                   BR1 (802.1q)             + $swp2           | |
# | |                                                                       | |
# | +------+------------------------------------------+---------------------+ |
# |        |                                          |                       |
# |        + br1.555                                  + br1.777               |
# |          192.0.2.2/28                               192.0.2.17/28         |
# |          2001:db8:1::2/64                           2001:db8:2::1/64      |
# +---------------------------------------------------------------------------+

ALL_TESTS="
	ping_ipv4
	ping_ipv6
	respin_config
	ping_ipv4
	ping_ipv6
"
NUM_NETIFS=4
source lib.sh

h1_create()
{
	simple_if_init $h1
	vlan_create $h1 555 v$h1 192.0.2.1/28 2001:db8:1::1/64
	ip -4 route add 192.0.2.16/28 vrf v$h1 nexthop via 192.0.2.2
	ip -6 route add 2001:db8:2::/64 vrf v$h1 nexthop via 2001:db8:1::2
}

h1_destroy()
{
	ip -6 route del 2001:db8:2::/64 vrf v$h1
	ip -4 route del 192.0.2.16/28 vrf v$h1
	vlan_destroy $h1 555
	simple_if_fini $h1
}

h2_create()
{
	simple_if_init $h2
	vlan_create $h2 777 v$h2 192.0.2.18/28 2001:db8:2::2/64
	ip -4 route add 192.0.2.0/28 vrf v$h2 nexthop via 192.0.2.17
	ip -6 route add 2001:db8:1::/64 vrf v$h2 nexthop via 2001:db8:2::1
}

h2_destroy()
{
	ip -6 route del 2001:db8:1::/64 vrf v$h2
	ip -4 route del 192.0.2.0/28 vrf v$h2
	vlan_destroy $h2 777
	simple_if_fini $h2
}

router_create()
{
	ip link add name br1 address $(mac_get $swp1) \
		type bridge vlan_filtering 1
	ip link set dev br1 up

	ip link set dev $swp1 master br1
	ip link set dev $swp2 master br1
	ip link set dev $swp1 up
	ip link set dev $swp2 up

	bridge vlan add dev br1 vid 555 self
	bridge vlan add dev br1 vid 777 self
	bridge vlan add dev $swp1 vid 555
	bridge vlan add dev $swp2 vid 777

	vlan_create br1 555 "" 192.0.2.2/28 2001:db8:1::2/64
	vlan_create br1 777 "" 192.0.2.17/28 2001:db8:2::1/64
}

router_destroy()
{
	vlan_destroy br1 777
	vlan_destroy br1 555

	bridge vlan del dev $swp2 vid 777
	bridge vlan del dev $swp1 vid 555
	bridge vlan del dev br1 vid 777 self
	bridge vlan del dev br1 vid 555 self

	ip link set dev $swp2 down nomaster
	ip link set dev $swp1 down nomaster

	ip link set dev br1 down
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

cleanup()
{
	pre_cleanup

	forwarding_restore

	router_destroy

	h2_destroy
	h1_destroy

	vrf_cleanup
}

ping_ipv4()
{
	ping_test $h1 192.0.2.18
}

ping_ipv6()
{
	ping6_test $h1 2001:db8:2::2
}

respin_config()
{
	log_info "Remaster bridge slave"

	ip link set dev $swp2 nomaster
	ip link set dev $swp1 nomaster

	sleep 2

	ip link set dev $swp1 master br1
	ip link set dev $swp2 master br1

	bridge vlan add dev $swp1 vid 555
	bridge vlan add dev $swp2 vid 777
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
