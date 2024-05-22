#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Handles creation and destruction of IP-in-IP or GRE tunnels over the given
# topology. Supports both flat and hierarchical models.
#
# Flat Model:
# Overlay and underlay share the same VRF.
# SW1 uses default VRF so tunnel has no bound dev.
# SW2 uses non-default VRF tunnel has a bound dev.
# +-------------------------+
# | H1                      |
# |               $h1 +     |
# |      192.0.2.1/28 |     |
# +-------------------|-----+
#                     |
# +-------------------|-----+
# | SW1               |     |
# |              $ol1 +     |
# |      192.0.2.2/28       |
# |                         |
# |  + g1a (gre)            |
# |    loc=192.0.2.65       |
# |    rem=192.0.2.66 --.   |
# |    tos=inherit      |   |
# |  .------------------'   |
# |  |                      |
# |  v                      |
# |  + $ul1.111 (vlan)      |
# |  | 192.0.2.129/28       |
# |   \                     |
# |    \_______             |
# |            |            |
# |VRF default + $ul1       |
# +------------|------------+
#              |
# +------------|------------+
# | SW2        + $ul2       |
# |     _______|            |
# |    /                    |
# |   /                     |
# |  + $ul2.111 (vlan)      |
# |  ^ 192.0.2.130/28       |
# |  |                      |
# |  |                      |
# |  '------------------.   |
# |  + g2a (gre)        |   |
# |    loc=192.0.2.66   |   |
# |    rem=192.0.2.65 --'   |
# |    tos=inherit          |
# |                         |
# |              $ol2 +     |
# |     192.0.2.17/28 |     |
# | VRF v$ol2         |     |
# +-------------------|-----+
#                     |
# +-------------------|-----+
# | H2                |     |
# |               $h2 +     |
# |     192.0.2.18/28       |
# +-------------------------+
#
# Hierarchical model:
# The tunnel is bound to a device in a different VRF
#
# +---------------------------+
# | H1                        |
# |               $h1 +       |
# |      192.0.2.1/28 |       |
# +-------------------|-------+
#                     |
# +-------------------|-------+
# | SW1               |       |
# | +-----------------|-----+ |
# | |            $ol1 +     | |
# | |     192.0.2.2/28      | |
# | |                       | |
# | |    + g1a (gre)        | |
# | |    rem=192.0.2.66     | |
# | |    tos=inherit        | |
# | |    loc=192.0.2.65     | |
# | |           ^           | |
# | | VRF v$ol1 |           | |
# | +-----------|-----------+ |
# |             |             |
# | +-----------|-----------+ |
# | | VRF v$ul1 |           | |
# | |           |           | |
# | |           |           | |
# | |           v           | |
# | |    dummy1 +           | |
# | |   192.0.2.65          | |
# | |   .-------'           | |
# | |   |                   | |
# | |   v                   | |
# | |   + $ul1.111 (vlan)   | |
# | |   | 192.0.2.129/28    | |
# | |   \                   | |
# | |    \_____             | |
# | |          |            | |
# | |          + $ul1       | |
# | +----------|------------+ |
# +------------|--------------+
#              |
# +------------|--------------+
# | SW2        |              |
# | +----------|------------+ |
# | |          + $ul2       | |
# | |     _____|            | |
# | |    /                  | |
# | |   /                   | |
# | |   | $ul2.111 (vlan)   | |
# | |   + 192.0.2.130/28    | |
# | |   ^                   | |
# | |   |                   | |
# | |   '-------.           | |
# | |    dummy2 +           | |
# | |    192.0.2.66         | |
# | |           ^           | |
# | |           |           | |
# | |           |           | |
# | | VRF v$ul2 |           | |
# | +-----------|-----------+ |
# |             |             |
# | +-----------|-----------+ |
# | | VRF v$ol2 |           | |
# | |           |           | |
# | |           v           | |
# | |  g2a (gre)+           | |
# | |  loc=192.0.2.66       | |
# | |  rem=192.0.2.65       | |
# | |  tos=inherit          | |
# | |                       | |
# | |            $ol2 +     | |
# | |   192.0.2.17/28 |     | |
# | +-----------------|-----+ |
# +-------------------|-------+
#                     |
# +-------------------|-------+
# | H2                |       |
# |               $h2 +       |
# |     192.0.2.18/28         |
# +---------------------------+

h1_create()
{
	simple_if_init $h1 192.0.2.1/28 2001:db8:1::1/64
	ip route add vrf v$h1 192.0.2.16/28 via 192.0.2.2
}

h1_destroy()
{
	ip route del vrf v$h1 192.0.2.16/28 via 192.0.2.2
	simple_if_fini $h1 192.0.2.1/28
}

h2_create()
{
	simple_if_init $h2 192.0.2.18/28
	ip route add vrf v$h2 192.0.2.0/28 via 192.0.2.17
}

h2_destroy()
{
	ip route del vrf v$h2 192.0.2.0/28 via 192.0.2.17
	simple_if_fini $h2 192.0.2.18/28
}

sw1_flat_create()
{
	local type=$1; shift
	local ol1=$1; shift
	local ul1=$1; shift

	ip link set dev $ol1 up
        __addr_add_del $ol1 add "192.0.2.2/28"

	ip link set dev $ul1 up
	vlan_create $ul1 111 "" 192.0.2.129/28

	tunnel_create g1a $type 192.0.2.65 192.0.2.66 tos inherit "$@"
	ip link set dev g1a up
        __addr_add_del g1a add "192.0.2.65/32"

	ip route add 192.0.2.66/32 via 192.0.2.130

	ip route add 192.0.2.16/28 nexthop dev g1a
}

sw1_flat_destroy()
{
	local ol1=$1; shift
	local ul1=$1; shift

	ip route del 192.0.2.16/28

	ip route del 192.0.2.66/32 via 192.0.2.130
	__simple_if_fini g1a 192.0.2.65/32
	tunnel_destroy g1a

	vlan_destroy $ul1 111
	__simple_if_fini $ul1
	__simple_if_fini $ol1 192.0.2.2/28
}

sw2_flat_create()
{
	local type=$1; shift
	local ol2=$1; shift
	local ul2=$1; shift

	simple_if_init $ol2 192.0.2.17/28
	__simple_if_init $ul2 v$ol2
	vlan_create $ul2 111 v$ol2 192.0.2.130/28

	tunnel_create g2a $type 192.0.2.66 192.0.2.65 tos inherit dev v$ol2 \
		"$@"
	__simple_if_init g2a v$ol2 192.0.2.66/32

	ip route add vrf v$ol2 192.0.2.65/32 via 192.0.2.129
	ip route add vrf v$ol2 192.0.2.0/28 nexthop dev g2a
}

sw2_flat_destroy()
{
	local ol2=$1; shift
	local ul2=$1; shift

	ip route del vrf v$ol2 192.0.2.0/28

	ip route del vrf v$ol2 192.0.2.65/32 via 192.0.2.129
	__simple_if_fini g2a 192.0.2.66/32
	tunnel_destroy g2a

	vlan_destroy $ul2 111
	__simple_if_fini $ul2
	simple_if_fini $ol2 192.0.2.17/28
}

sw1_hierarchical_create()
{
	local type=$1; shift
	local ol1=$1; shift
	local ul1=$1; shift

	simple_if_init $ol1 192.0.2.2/28
	simple_if_init $ul1
	ip link add name dummy1 type dummy
	__simple_if_init dummy1 v$ul1 192.0.2.65/32

	vlan_create $ul1 111 v$ul1 192.0.2.129/28
	tunnel_create g1a $type 192.0.2.65 192.0.2.66 tos inherit dev dummy1 \
		"$@"
	ip link set dev g1a master v$ol1

	ip route add vrf v$ul1 192.0.2.66/32 via 192.0.2.130
	ip route add vrf v$ol1 192.0.2.16/28 nexthop dev g1a
}

sw1_hierarchical_destroy()
{
	local ol1=$1; shift
	local ul1=$1; shift

	ip route del vrf v$ol1 192.0.2.16/28
	ip route del vrf v$ul1 192.0.2.66/32

	tunnel_destroy g1a
	vlan_destroy $ul1 111

	__simple_if_fini dummy1 192.0.2.65/32
	ip link del dev dummy1

	simple_if_fini $ul1
	simple_if_fini $ol1 192.0.2.2/28
}

sw2_hierarchical_create()
{
	local type=$1; shift
	local ol2=$1; shift
	local ul2=$1; shift

	simple_if_init $ol2 192.0.2.17/28
	simple_if_init $ul2

	ip link add name dummy2 type dummy
	__simple_if_init dummy2 v$ul2 192.0.2.66/32

	vlan_create $ul2 111 v$ul2 192.0.2.130/28
	tunnel_create g2a $type 192.0.2.66 192.0.2.65 tos inherit dev dummy2 \
		"$@"
	ip link set dev g2a master v$ol2

	ip route add vrf v$ul2 192.0.2.65/32 via 192.0.2.129
	ip route add vrf v$ol2 192.0.2.0/28 nexthop dev g2a
}

sw2_hierarchical_destroy()
{
	local ol2=$1; shift
	local ul2=$1; shift

	ip route del vrf v$ol2 192.0.2.0/28
	ip route del vrf v$ul2 192.0.2.65/32

	tunnel_destroy g2a
	vlan_destroy $ul2 111

	__simple_if_fini dummy2 192.0.2.66/32
	ip link del dev dummy2

	simple_if_fini $ul2
	simple_if_fini $ol2 192.0.2.17/28
}

topo_mtu_change()
{
	local mtu=$1

	ip link set mtu $mtu dev $h1
	ip link set mtu $mtu dev $ol1
	ip link set mtu $mtu dev g1a
	ip link set mtu $mtu dev $ul1
	ip link set mtu $mtu dev $ul1.111
	ip link set mtu $mtu dev $h2
	ip link set mtu $mtu dev $ol2
	ip link set mtu $mtu dev g2a
	ip link set mtu $mtu dev $ul2
	ip link set mtu $mtu dev $ul2.111
}

test_mtu_change()
{
	local encap=$1; shift

	RET=0

	ping_do $h1 192.0.2.18 "-s 1800	-w 3"
	check_fail $? "ping $encap should not pass with size 1800"

	RET=0

	topo_mtu_change	2000
	ping_do	$h1 192.0.2.18 "-s 1800	-w 3"
	check_err $?
	log_test "ping $encap packet size 1800 after MTU change"
}
