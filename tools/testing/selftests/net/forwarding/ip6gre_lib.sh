# SPDX-License-Identifier: GPL-2.0
#!/bin/bash

# Handles creation and destruction of IP-in-IP or GRE tunnels over the given
# topology. Supports both flat and hierarchical models.
#
# Flat Model:
# Overlay and underlay share the same VRF.
# SW1 uses default VRF so tunnel has no bound dev.
# SW2 uses non-default VRF tunnel has a bound dev.
# +--------------------------------+
# | H1                             |
# |                     $h1 +      |
# |        198.51.100.1/24  |      |
# |        2001:db8:1::1/64 |      |
# +-------------------------|------+
#                           |
# +-------------------------|-------------------+
# | SW1                     |                   |
# |                    $ol1 +                   |
# |        198.51.100.2/24                      |
# |        2001:db8:1::2/64                     |
# |                                             |
# |      + g1a (ip6gre)                         |
# |        loc=2001:db8:3::1                    |
# |        rem=2001:db8:3::2 --.                |
# |        tos=inherit         |                |
# |                            .                |
# |      .---------------------                 |
# |      |                                      |
# |      v                                      |
# |      + $ul1.111 (vlan)                      |
# |      | 2001:db8:10::1/64                    |
# |       \                                     |
# |        \____________                        |
# |                     |                       |
# | VRF default         + $ul1                  |
# +---------------------|-----------------------+
#                       |
# +---------------------|-----------------------+
# | SW2                 |                       |
# |                $ul2 +                       |
# |          ___________|                       |
# |         /                                   |
# |        /                                    |
# |       + $ul2.111 (vlan)                     |
# |       ^ 2001:db8:10::2/64                   |
# |       |                                     |
# |       |                                     |
# |       '----------------------.              |
# |       + g2a (ip6gre)         |              |
# |         loc=2001:db8:3::2    |              |
# |         rem=2001:db8:3::1  --'              |
# |         tos=inherit                         |
# |                                             |
# |                     + $ol2                  |
# |                     | 203.0.113.2/24        |
# | VRF v$ol2           | 2001:db8:2::2/64      |
# +---------------------|-----------------------+
# +---------------------|----------+
# | H2                  |          |
# |                 $h2 +          |
# |    203.0.113.1/24              |
# |    2001:db8:2::1/64            |
# +--------------------------------+
#
# Hierarchical model:
# The tunnel is bound to a device in a different VRF
#
# +--------------------------------+
# | H1                             |
# |                     $h1 +      |
# |        198.51.100.1/24  |      |
# |        2001:db8:1::1/64 |      |
# +-------------------------|------+
#                           |
# +-------------------------|-------------------+
# | SW1                     |                   |
# | +-----------------------|-----------------+ |
# | |                  $ol1 +                 | |
# | |      198.51.100.2/24                    | |
# | |      2001:db8:1::2/64                   | |
# | |                                         | |
# | |              + g1a (ip6gre)             | |
# | |                loc=2001:db8:3::1        | |
# | |                rem=2001:db8:3::2        | |
# | |                tos=inherit              | |
# | |                    ^                    | |
# | |   VRF v$ol1        |                    | |
# | +--------------------|--------------------+ |
# |                      |                      |
# | +--------------------|--------------------+ |
# | |   VRF v$ul1        |                    | |
# | |                    |                    | |
# | |                    v                    | |
# | |             dummy1 +                    | |
# | |       2001:db8:3::1/64                  | |
# | |         .-----------'                   | |
# | |         |                               | |
# | |         v                               | |
# | |         + $ul1.111 (vlan)               | |
# | |         | 2001:db8:10::1/64             | |
# | |         \                               | |
# | |          \__________                    | |
# | |                     |                   | |
# | |                     + $ul1              | |
# | +---------------------|-------------------+ |
# +-----------------------|---------------------+
#                         |
# +-----------------------|---------------------+
# | SW2                   |                     |
# | +---------------------|-------------------+ |
# | |                     + $ul2              | |
# | |                _____|                   | |
# | |               /                         | |
# | |              /                          | |
# | |              | $ul2.111 (vlan)          | |
# | |              + 2001:db8:10::2/64        | |
# | |              ^                          | |
# | |              |                          | |
# | |              '------.                   | |
# | |              dummy2 +                   | |
# | |              2001:db8:3::2/64           | |
# | |                     ^                   | |
# | |                     |                   | |
# | |                     |                   | |
# | | VRF v$ul2           |                   | |
# | +---------------------|-------------------+ |
# |                       |                     |
# | +---------------------|-------------------+ |
# | | VRF v$ol2           |                   | |
# | |                     |                   | |
# | |                     v                   | |
# | |        g2a (ip6gre) +                   | |
# | |        loc=2001:db8:3::2                | |
# | |        rem=2001:db8:3::1                | |
# | |        tos=inherit                      | |
# | |                                         | |
# | |                $ol2 +                   | |
# | |    203.0.113.2/24   |                   | |
# | |    2001:db8:2::2/64 |                   | |
# | +---------------------|-------------------+ |
# +-----------------------|---------------------+
#                         |
# +-----------------------|--------+
# | H2                    |        |
# |                   $h2 +        |
# |      203.0.113.1/24            |
# |      2001:db8:2::1/64          |
# +--------------------------------+

source lib.sh
source tc_common.sh

h1_create()
{
	simple_if_init $h1 198.51.100.1/24 2001:db8:1::1/64
	ip route add vrf v$h1 203.0.113.0/24 via 198.51.100.2
	ip -6 route add vrf v$h1 2001:db8:2::/64 via 2001:db8:1::2
}

h1_destroy()
{
	ip -6 route del vrf v$h1 2001:db8:2::/64 via 2001:db8:1::2
	ip route del vrf v$h1 203.0.113.0/24 via 198.51.100.2
	simple_if_fini $h1 198.51.100.1/24 2001:db8:1::1/64
}

h2_create()
{
	simple_if_init $h2 203.0.113.1/24 2001:db8:2::1/64
	ip route add vrf v$h2 198.51.100.0/24 via 203.0.113.2
	ip -6 route add vrf v$h2 2001:db8:1::/64 via 2001:db8:2::2
}

h2_destroy()
{
	ip -6 route del vrf v$h2 2001:db8:1::/64 via 2001:db8:2::2
	ip route del vrf v$h2 198.51.100.0/24 via 203.0.113.2
	simple_if_fini $h2 203.0.113.1/24 2001:db8:2::1/64
}

sw1_flat_create()
{
	local ol1=$1; shift
	local ul1=$1; shift

	ip link set dev $ol1 up
        __addr_add_del $ol1 add 198.51.100.2/24 2001:db8:1::2/64

	ip link set dev $ul1 up
	vlan_create $ul1 111 "" 2001:db8:10::1/64

	tunnel_create g1a ip6gre 2001:db8:3::1 2001:db8:3::2 tos inherit \
		ttl inherit "$@"
	ip link set dev g1a up
        __addr_add_del g1a add "2001:db8:3::1/128"

	ip -6 route add 2001:db8:3::2/128 via 2001:db8:10::2
	ip route add 203.0.113.0/24 dev g1a
	ip -6 route add 2001:db8:2::/64 dev g1a
}

sw1_flat_destroy()
{
	local ol1=$1; shift
	local ul1=$1; shift

	ip -6 route del 2001:db8:2::/64
	ip route del 203.0.113.0/24
	ip -6 route del 2001:db8:3::2/128 via 2001:db8:10::2

	__simple_if_fini g1a 2001:db8:3::1/128
	tunnel_destroy g1a

	vlan_destroy $ul1 111
	__simple_if_fini $ul1
	__simple_if_fini $ol1 198.51.100.2/24 2001:db8:1::2/64
}

sw2_flat_create()
{
	local ol2=$1; shift
	local ul2=$1; shift

	simple_if_init $ol2 203.0.113.2/24 2001:db8:2::2/64
	__simple_if_init $ul2 v$ol2
	vlan_create $ul2 111 v$ol2 2001:db8:10::2/64

	tunnel_create g2a ip6gre 2001:db8:3::2 2001:db8:3::1 tos inherit \
		ttl inherit dev v$ol2 "$@"
	__simple_if_init g2a v$ol2 2001:db8:3::2/128

	# Replace neighbor to avoid 1 dropped packet due to "unresolved neigh"
	ip neigh replace dev $ol2 203.0.113.1 lladdr $(mac_get $h2)
	ip -6 neigh replace dev $ol2 2001:db8:2::1 lladdr $(mac_get $h2)

	ip -6 route add vrf v$ol2 2001:db8:3::1/128 via 2001:db8:10::1
	ip route add vrf v$ol2 198.51.100.0/24 dev g2a
	ip -6 route add vrf v$ol2 2001:db8:1::/64 dev g2a
}

sw2_flat_destroy()
{
	local ol2=$1; shift
	local ul2=$1; shift

	ip -6 route del vrf v$ol2 2001:db8:2::/64
	ip route del vrf v$ol2 198.51.100.0/24
	ip -6 route del vrf v$ol2 2001:db8:3::1/128 via 2001:db8:10::1

	__simple_if_fini g2a 2001:db8:3::2/128
	tunnel_destroy g2a

	vlan_destroy $ul2 111
	__simple_if_fini $ul2
	simple_if_fini $ol2 203.0.113.2/24 2001:db8:2::2/64
}

sw1_hierarchical_create()
{
	local ol1=$1; shift
	local ul1=$1; shift

	simple_if_init $ol1 198.51.100.2/24 2001:db8:1::2/64
	simple_if_init $ul1
	ip link add name dummy1 type dummy
	__simple_if_init dummy1 v$ul1 2001:db8:3::1/64

	vlan_create $ul1 111 v$ul1 2001:db8:10::1/64
	tunnel_create g1a ip6gre 2001:db8:3::1 2001:db8:3::2 tos inherit \
		ttl inherit dev dummy1 "$@"
	ip link set dev g1a master v$ol1

	ip -6 route add vrf v$ul1 2001:db8:3::2/128 via 2001:db8:10::2
	ip route add vrf v$ol1 203.0.113.0/24 dev g1a
	ip -6 route add vrf v$ol1 2001:db8:2::/64 dev g1a
}

sw1_hierarchical_destroy()
{
	local ol1=$1; shift
	local ul1=$1; shift

	ip -6 route del vrf v$ol1 2001:db8:2::/64
	ip route del vrf v$ol1 203.0.113.0/24
	ip -6 route del vrf v$ul1 2001:db8:3::2/128

	tunnel_destroy g1a
	vlan_destroy $ul1 111

	__simple_if_fini dummy1 2001:db8:3::1/64
	ip link del dev dummy1

	simple_if_fini $ul1
	simple_if_fini $ol1 198.51.100.2/24 2001:db8:1::2/64
}

sw2_hierarchical_create()
{
	local ol2=$1; shift
	local ul2=$1; shift

	simple_if_init $ol2 203.0.113.2/24 2001:db8:2::2/64
	simple_if_init $ul2

	ip link add name dummy2 type dummy
	__simple_if_init dummy2 v$ul2 2001:db8:3::2/64

	vlan_create $ul2 111 v$ul2 2001:db8:10::2/64
	tunnel_create g2a ip6gre 2001:db8:3::2 2001:db8:3::1 tos inherit \
		ttl inherit dev dummy2 "$@"
	ip link set dev g2a master v$ol2

	# Replace neighbor to avoid 1 dropped packet due to "unresolved neigh"
	ip neigh replace dev $ol2 203.0.113.1 lladdr $(mac_get $h2)
	ip -6 neigh replace dev $ol2 2001:db8:2::1 lladdr $(mac_get $h2)

	ip -6 route add vrf v$ul2 2001:db8:3::1/128 via 2001:db8:10::1
	ip route add vrf v$ol2 198.51.100.0/24 dev g2a
	ip -6 route add vrf v$ol2 2001:db8:1::/64 dev g2a
}

sw2_hierarchical_destroy()
{
	local ol2=$1; shift
	local ul2=$1; shift

	ip -6 route del vrf v$ol2 2001:db8:2::/64
	ip route del vrf v$ol2 198.51.100.0/24
	ip -6 route del vrf v$ul2 2001:db8:3::1/128

	tunnel_destroy g2a
	vlan_destroy $ul2 111

	__simple_if_fini dummy2 2001:db8:3::2/64
	ip link del dev dummy2

	simple_if_fini $ul2
	simple_if_fini $ol2 203.0.113.2/24 2001:db8:2::2/64
}

test_traffic_ip4ip6()
{
	RET=0

	h1mac=$(mac_get $h1)
	ol1mac=$(mac_get $ol1)

	tc qdisc add dev $ul1 clsact
	tc filter add dev $ul1 egress proto all pref 1 handle 101 \
		flower $TC_FLAG action pass

	tc qdisc add dev $ol2 clsact
	tc filter add dev $ol2 egress protocol ipv4 pref 1 handle 101 \
		flower $TC_FLAG dst_ip 203.0.113.1 action pass

	$MZ $h1 -c 1000 -p 64 -a $h1mac -b $ol1mac -A 198.51.100.1 \
		-B 203.0.113.1 -t ip -q -d 1msec

	# Check ports after encap and after decap.
	tc_check_at_least_x_packets "dev $ul1 egress" 101 1000
	check_err $? "Packets did not go through $ul1, tc_flag = $TC_FLAG"

	tc_check_at_least_x_packets "dev $ol2 egress" 101 1000
	check_err $? "Packets did not go through $ol2, tc_flag = $TC_FLAG"

	log_test "$@"

	tc filter del dev $ol2 egress protocol ipv4 pref 1 handle 101 flower
	tc qdisc del dev $ol2 clsact
	tc filter del dev $ul1 egress proto all pref 1 handle 101 flower
	tc qdisc del dev $ul1 clsact
}

test_traffic_ip6ip6()
{
	RET=0

	h1mac=$(mac_get $h1)
	ol1mac=$(mac_get $ol1)

	tc qdisc add dev $ul1 clsact
	tc filter add dev $ul1 egress proto all pref 1 handle 101 \
		flower $TC_FLAG action pass

	tc qdisc add dev $ol2 clsact
	tc filter add dev $ol2 egress protocol ipv6 pref 1 handle 101 \
		flower $TC_FLAG dst_ip 2001:db8:2::1 action pass

	$MZ -6 $h1 -c 1000 -p 64 -a $h1mac -b $ol1mac -A 2001:db8:1::1 \
		-B 2001:db8:2::1 -t ip -q -d 1msec

	# Check ports after encap and after decap.
	tc_check_at_least_x_packets "dev $ul1 egress" 101 1000
	check_err $? "Packets did not go through $ul1, tc_flag = $TC_FLAG"

	tc_check_at_least_x_packets "dev $ol2 egress" 101 1000
	check_err $? "Packets did not go through $ol2, tc_flag = $TC_FLAG"

	log_test "$@"

	tc filter del dev $ol2 egress protocol ipv6 pref 1 handle 101 flower
	tc qdisc del dev $ol2 clsact
	tc filter del dev $ul1 egress proto all pref 1 handle 101 flower
	tc qdisc del dev $ul1 clsact
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
	RET=0

	ping6_do $h1 2001:db8:2::1 "-s 1800 -w 3"
	check_fail $? "ping GRE IPv6 should not pass with packet size 1800"

	RET=0

	topo_mtu_change	2000
	ping6_do $h1 2001:db8:2::1 "-s 1800 -w 3"
	check_err $?
	log_test "ping GRE IPv6, packet size 1800 after MTU change"
}
