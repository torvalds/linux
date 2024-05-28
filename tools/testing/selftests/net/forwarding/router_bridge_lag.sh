#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +----------------------------+                   +--------------------------+
# | H1 (vrf)                   |                   |                 H2 (vrf) |
# |                            |                   |                          |
# |        + LAG1 (team)       |                   |     + LAG4 (team)        |
# |        | 192.0.2.1/28      |                   |     | 192.0.2.130/28     |
# |        | 2001:db8:1::1/64  |                   |     | 2001:db8:2::2/64   |
# |      __^___                |                   |   __^_____               |
# |     /      \               |                   |  /        \              |
# |    + $h1    + $h4          |                   | + $h2      + $h3         |
# |    |        |              |                   | |          |             |
# +----|--------|--------------+                   +-|----------|-------------+
#      |        |                                    |          |
# +----|--------|------------------------------------|----------|-------------+
# | SW |        |                                    |          |             |
# |    + $swp1  + $swp4                              + $swp2    + $swp3       |
# |     \__ ___/                                      \__ _____/              |
# |        v                                             v                    |
# | +------|-------------------------------+             |                    |
# | |      + LAG2       BR1 (802.1q)       |             + LAG3 (team)        |
# | |        (team)       192.0.2.2/28     |               192.0.2.129/28     |
# | |                     2001:db8:1::2/64 |               2001:db8:2::1/64   |
# | |                                      |                                  |
# | +--------------------------------------+                                  |
# +---------------------------------------------------------------------------+

: ${ALL_TESTS:="
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

	$(: move LAG3 to a bridge and then out )
	config_remaster_lag3
	config_wait
	ping_ipv4
	ping_ipv6
    "}
NUM_NETIFS=8
: ${lib_dir:=.}
source $lib_dir/lib.sh
$EXTRA_SOURCE

h1_create()
{
	team_create lag1 lacp
	ip link set dev lag1 address $(mac_get $h1)
	ip link set dev $h1 master lag1
	ip link set dev $h4 master lag1
	simple_if_init lag1 192.0.2.1/28 2001:db8:1::1/64
	ip link set dev $h1 up
	ip link set dev $h4 up
	ip -4 route add 192.0.2.128/28 vrf vlag1 nexthop via 192.0.2.2
	ip -6 route add 2001:db8:2::/64 vrf vlag1 nexthop via 2001:db8:1::2
}

h1_destroy()
{
	ip -6 route del 2001:db8:2::/64 vrf vlag1
	ip -4 route del 192.0.2.128/28 vrf vlag1
	ip link set dev $h4 down
	ip link set dev $h1 down
	simple_if_fini lag1 192.0.2.1/28 2001:db8:1::1/64
	ip link set dev $h4 nomaster
	ip link set dev $h1 nomaster
	team_destroy lag1
}

h2_create()
{
	team_create lag4 lacp
	ip link set dev lag4 address $(mac_get $h2)
	ip link set dev $h2 master lag4
	ip link set dev $h3 master lag4
	simple_if_init lag4 192.0.2.130/28 2001:db8:2::2/64
	ip link set dev $h2 up
	ip link set dev $h3 up
	ip -4 route add 192.0.2.0/28 vrf vlag4 nexthop via 192.0.2.129
	ip -6 route add 2001:db8:1::/64 vrf vlag4 nexthop via 2001:db8:2::1
}

h2_destroy()
{
	ip -6 route del 2001:db8:1::/64 vrf vlag4
	ip -4 route del 192.0.2.0/28 vrf vlag4
	ip link set dev $h3 down
	ip link set dev $h2 down
	simple_if_fini lag4 192.0.2.130/28 2001:db8:2::2/64
	ip link set dev $h3 nomaster
	ip link set dev $h2 nomaster
	team_destroy lag4
}

router_create()
{
	team_create lag2 lacp
	ip link set dev lag2 address $(mac_get $swp1)
	ip link set dev $swp1 master lag2
	ip link set dev $swp4 master lag2

	ip link add name br1 address $(mac_get lag2) \
		type bridge vlan_filtering 1
	ip link set dev lag2 master br1

	ip link set dev $swp1 up
	ip link set dev $swp4 up
	ip link set dev br1 up

	__addr_add_del br1 add 192.0.2.2/28 2001:db8:1::2/64

	team_create lag3 lacp
	ip link set dev lag3 address $(mac_get $swp2)
	ip link set dev $swp2 master lag3
	ip link set dev $swp3 master lag3
	ip link set dev $swp2 up
	ip link set dev $swp3 up
	__addr_add_del lag3 add 192.0.2.129/28 2001:db8:2::1/64
}

router_destroy()
{
	__addr_add_del lag3 del 192.0.2.129/28 2001:db8:2::1/64
	ip link set dev $swp3 down
	ip link set dev $swp2 down
	ip link set dev $swp3 nomaster
	ip link set dev $swp2 nomaster
	team_destroy lag3

	__addr_add_del br1 del 192.0.2.2/28 2001:db8:1::2/64

	ip link set dev $swp4 down
	ip link set dev $swp1 down
	ip link set dev br1 down

	ip link set dev lag2 nomaster
	ip link del dev br1

	ip link set dev $swp4 nomaster
	ip link set dev $swp1 nomaster
	team_destroy lag2
}

config_remaster_lag2()
{
	log_info "Remaster bridge slave"

	ip link set dev lag2 nomaster
	sleep 2
	ip link set dev lag2 master br1
}

config_remaster_lag3()
{
	log_info "Move lag3 to the bridge, then out again"

	ip link set dev lag3 master br1
	sleep 2
	ip link set dev lag3 nomaster
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
	ping_test lag1 192.0.2.130
}

ping_ipv6()
{
	ping6_test lag1 2001:db8:2::2
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
