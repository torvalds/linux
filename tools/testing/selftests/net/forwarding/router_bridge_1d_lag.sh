#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +--------------------------------------------+
# | H1 (vrf)                                   |
# |                                            |
# |    + LAG1.100          + LAG1.200          |
# |    | 192.0.2.1/28      | 192.0.2.17/28     |
# |    | 2001:db8:1::1/64  | 2001:db8:3:1/64   |
# |    \___________ _______/                   |
# |                v                           |
# |                + LAG1 (team)               |
# |                |                           |
# |            ____^____                       |
# |           /         \                      |
# |          + $h1       + $h4                 |
# |          |           |                     |
# +----------|-----------|---------------------+
#            |           |
# +----------|-----------|---------------------+
# | SW       |           |                     |
# |          + $swp1     + $swp4               |
# |           \____ ____/                      |
# |                v                           |
# |    LAG2 (team) +                           |
# |                |                           |
# |         _______^______________             |
# |        /                      \            |
# | +------|------------+ +-------|----------+ |
# | |      + LAG2.100   | |       + LAG2.200 | |
# | |                   | |                  | |
# | |  BR1 (802.1d)     | | BR2 (802.1d)     | |
# | |  192.0.2.2/28     | | 192.0.2.18/28    | |
# | |  2001:db8:1::2/64 | | 2001:db8:3:2/64  | |
# | |                   | |                  | |
# | +-------------------+ +------------------+ |
# |                                            |
# |  + LAG3.100             + LAG3.200         |
# |  | 192.0.2.129/28       | 192.0.2.145/28   |
# |  | 2001:db8:2::1/64     | 2001:db8:4::1/64 |
# |  |                      |                  |
# |  \_________ ___________/                   |
# |            v                               |
# |            + LAG3 (team)                   |
# |        ____|____                           |
# |       /         \                          |
# |       + $swp2   + $swp3                    |
# |       |         |                          |
# +-------|---------|--------------------------+
#         |         |
# +-------|---------|--------------------------+
# |       |         |                          |
# |       + $h2     + $h3                      |
# |       \____ ___/                           |
# |            |                               |
# |            + LAG4 (team)                   |
# |            |                               |
# |  __________^__________                     |
# | /                     \                    |
# | |                     |                    |
# | + LAG4.100            + LAG4.200           |
# |   192.0.2.130/28        192.0.2.146/28     |
# |   2001:db8:2::2/64      2001:db8:4::2/64   |
# |                                            |
# | H2 (vrf)                                   |
# +--------------------------------------------+

ALL_TESTS="
	ping_ipv4
	ping_ipv6

	$(: exercise remastering of LAG2 slaves )
	config_deslave_swp4
	config_wait
	ping_ipv4
	ping_ipv6
	config_enslave_swp4
	config_deslave_swp1
	config_wait
	ping_ipv4
	ping_ipv6
	config_deslave_swp4
	config_enslave_swp1
	config_enslave_swp4
	config_wait
	ping_ipv4
	ping_ipv6

	$(: exercise remastering of LAG2 itself )
	config_remaster_lag2
	config_wait
	ping_ipv4
	ping_ipv6

	$(: exercise remastering of LAG3 slaves )
	config_deslave_swp2
	config_wait
	ping_ipv4
	ping_ipv6
	config_enslave_swp2
	config_deslave_swp3
	config_wait
	ping_ipv4
	ping_ipv6
	config_deslave_swp2
	config_enslave_swp3
	config_enslave_swp2
	config_wait
	ping_ipv4
	ping_ipv6
"
NUM_NETIFS=8
source lib.sh

h1_create()
{
	team_create lag1 lacp
	ip link set dev lag1 addrgenmode none
	ip link set dev lag1 address $(mac_get $h1)
	ip link set dev $h1 master lag1
	ip link set dev $h4 master lag1
	simple_if_init lag1
	ip link set dev $h1 up
	ip link set dev $h4 up

	vlan_create lag1 100 vlag1 192.0.2.1/28 2001:db8:1::1/64
	vlan_create lag1 200 vlag1 192.0.2.17/28 2001:db8:3::1/64

	ip -4 route add 192.0.2.128/28 vrf vlag1 nexthop via 192.0.2.2
	ip -6 route add 2001:db8:2::/64 vrf vlag1 nexthop via 2001:db8:1::2

	ip -4 route add 192.0.2.144/28 vrf vlag1 nexthop via 192.0.2.18
	ip -6 route add 2001:db8:4::/64 vrf vlag1 nexthop via 2001:db8:3::2
}

h1_destroy()
{
	ip -6 route del 2001:db8:4::/64 vrf vlag1
	ip -4 route del 192.0.2.144/28 vrf vlag1

	ip -6 route del 2001:db8:2::/64 vrf vlag1
	ip -4 route del 192.0.2.128/28 vrf vlag1

	vlan_destroy lag1 200
	vlan_destroy lag1 100

	ip link set dev $h4 down
	ip link set dev $h1 down
	simple_if_fini lag1
	ip link set dev $h4 nomaster
	ip link set dev $h1 nomaster
	team_destroy lag1
}

h2_create()
{
	team_create lag4 lacp
	ip link set dev lag4 addrgenmode none
	ip link set dev lag4 address $(mac_get $h2)
	ip link set dev $h2 master lag4
	ip link set dev $h3 master lag4
	simple_if_init lag4
	ip link set dev $h2 up
	ip link set dev $h3 up

	vlan_create lag4 100 vlag4 192.0.2.130/28 2001:db8:2::2/64
	vlan_create lag4 200 vlag4 192.0.2.146/28 2001:db8:4::2/64

	ip -4 route add 192.0.2.0/28 vrf vlag4 nexthop via 192.0.2.129
	ip -6 route add 2001:db8:1::/64 vrf vlag4 nexthop via 2001:db8:2::1

	ip -4 route add 192.0.2.16/28 vrf vlag4 nexthop via 192.0.2.145
	ip -6 route add 2001:db8:3::/64 vrf vlag4 nexthop via 2001:db8:4::1
}

h2_destroy()
{
	ip -6 route del 2001:db8:3::/64 vrf vlag4
	ip -4 route del 192.0.2.16/28 vrf vlag4

	ip -6 route del 2001:db8:1::/64 vrf vlag4
	ip -4 route del 192.0.2.0/28 vrf vlag4

	vlan_destroy lag4 200
	vlan_destroy lag4 100

	ip link set dev $h3 down
	ip link set dev $h2 down
	simple_if_fini lag4
	ip link set dev $h3 nomaster
	ip link set dev $h2 nomaster
	team_destroy lag4
}

router_create()
{
	team_create lag2 lacp
	ip link set dev lag2 addrgenmode none
	ip link set dev lag2 address $(mac_get $swp1)
	ip link set dev $swp1 master lag2
	ip link set dev $swp4 master lag2

	vlan_create lag2 100
	vlan_create lag2 200

	ip link add name br1 type bridge vlan_filtering 0
	ip link set dev br1 address $(mac_get lag2.100)
	ip link set dev lag2.100 master br1

	ip link add name br2 type bridge vlan_filtering 0
	ip link set dev br2 address $(mac_get lag2.200)
	ip link set dev lag2.200 master br2

	ip link set dev $swp1 up
	ip link set dev $swp4 up
	ip link set dev br1 up
	ip link set dev br2 up

	__addr_add_del br1 add 192.0.2.2/28 2001:db8:1::2/64
	__addr_add_del br2 add 192.0.2.18/28 2001:db8:3::2/64

	team_create lag3 lacp
	ip link set dev lag3 addrgenmode none
	ip link set dev lag3 address $(mac_get $swp2)
	ip link set dev $swp2 master lag3
	ip link set dev $swp3 master lag3
	ip link set dev $swp2 up
	ip link set dev $swp3 up

	vlan_create lag3 100
	vlan_create lag3 200

	__addr_add_del lag3.100 add 192.0.2.129/28 2001:db8:2::1/64
	__addr_add_del lag3.200 add 192.0.2.145/28 2001:db8:4::1/64
}

router_destroy()
{
	__addr_add_del lag3.200 del 192.0.2.145/28 2001:db8:4::1/64
	__addr_add_del lag3.100 del 192.0.2.129/28 2001:db8:2::1/64

	vlan_destroy lag3 200
	vlan_destroy lag3 100

	ip link set dev $swp3 down
	ip link set dev $swp2 down
	ip link set dev $swp3 nomaster
	ip link set dev $swp2 nomaster
	team_destroy lag3

	__addr_add_del br2 del 192.0.2.18/28 2001:db8:3::2/64
	__addr_add_del br1 del 192.0.2.2/28 2001:db8:1::2/64

	ip link set dev br2 down
	ip link set dev br1 down
	ip link set dev $swp4 down
	ip link set dev $swp1 down

	ip link set dev lag2.200 nomaster
	ip link del dev br2

	ip link set dev lag2.100 nomaster
	ip link del dev br1

	vlan_destroy lag2 200
	vlan_destroy lag2 100

	ip link set dev $swp4 nomaster
	ip link set dev $swp1 nomaster
	team_destroy lag2
}

config_remaster_lag2()
{
	log_info "Remaster bridge slaves"

	ip link set dev lag2.200 nomaster
	ip link set dev lag2.100 nomaster
	sleep 2
	ip link set dev lag2.100 master br1
	ip link set dev lag2.200 master br2
}

config_deslave()
{
	local netdev=$1; shift

	log_info "Deslave $netdev"
	ip link set dev $netdev down
	ip link set dev $netdev nomaster
	ip link set dev $netdev up
}

config_deslave_swp1()
{
	config_deslave $swp1
}

config_deslave_swp2()
{
	config_deslave $swp2
}

config_deslave_swp3()
{
	config_deslave $swp3
}

config_deslave_swp4()
{
	config_deslave $swp4
}

config_enslave()
{
	local netdev=$1; shift
	local master=$1; shift

	log_info "Enslave $netdev to $master"
	ip link set dev $netdev down
	ip link set dev $netdev master $master
	ip link set dev $netdev up
}

config_enslave_swp1()
{
	config_enslave $swp1 lag2
}

config_enslave_swp2()
{
	config_enslave $swp2 lag3
}

config_enslave_swp3()
{
	config_enslave $swp3 lag3
}

config_enslave_swp4()
{
	config_enslave $swp4 lag2
}

config_wait()
{
	setup_wait_dev lag2
	setup_wait_dev lag3
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	swp3=${NETIFS[p5]}
	h3=${NETIFS[p6]}

	h4=${NETIFS[p7]}
	swp4=${NETIFS[p8]}

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
	ping_test lag1.100 192.0.2.130 ": via 100"
	ping_test lag1.200 192.0.2.146 ": via 200"
}

ping_ipv6()
{
	ping6_test lag1.100 2001:db8:2::2 ": via 100"
	ping6_test lag1.200 2001:db8:4::2 ": via 200"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
