# SPDX-License-Identifier: GPL-2.0

# This is the standard topology for testing mirroring. The tests that use it
# tweak it in one way or another--typically add more devices to the topology.
#
#   +---------------------+                             +---------------------+
#   | H1                  |                             |                  H2 |
#   |     + $h1           |                             |           $h2 +     |
#   |     | 192.0.2.1/28  |                             |  192.0.2.2/28 |     |
#   +-----|---------------+                             +---------------|-----+
#         |                                                             |
#   +-----|-------------------------------------------------------------|-----+
#   | SW  o--> mirror                                                   |     |
#   | +---|-------------------------------------------------------------|---+ |
#   | |   + $swp1                    BR                           $swp2 +   | |
#   | +---------------------------------------------------------------------+ |
#   |                                                                         |
#   |     + $swp3                                                             |
#   +-----|-------------------------------------------------------------------+
#         |
#   +-----|-------------------------------------------------------------------+
#   | H3  + $h3                                                               |
#   |                                                                         |
#   +-------------------------------------------------------------------------+

mirror_topo_h1_create()
{
	simple_if_init $h1 192.0.2.1/28
}

mirror_topo_h1_destroy()
{
	simple_if_fini $h1 192.0.2.1/28
}

mirror_topo_h2_create()
{
	simple_if_init $h2 192.0.2.2/28
}

mirror_topo_h2_destroy()
{
	simple_if_fini $h2 192.0.2.2/28
}

mirror_topo_h3_create()
{
	simple_if_init $h3
	tc qdisc add dev $h3 clsact
}

mirror_topo_h3_destroy()
{
	tc qdisc del dev $h3 clsact
	simple_if_fini $h3
}

mirror_topo_switch_create()
{
	ip link set dev $swp3 up

	ip link add name br1 type bridge vlan_filtering 1
	ip link set dev br1 addrgenmode none
	ip link set dev br1 up

	ip link set dev $swp1 master br1
	ip link set dev $swp1 up

	ip link set dev $swp2 master br1
	ip link set dev $swp2 up

	tc qdisc add dev $swp1 clsact
}

mirror_topo_switch_destroy()
{
	tc qdisc del dev $swp1 clsact

	ip link set dev $swp1 down
	ip link set dev $swp2 down
	ip link del dev br1

	ip link set dev $swp3 down
}

mirror_topo_create()
{
	mirror_topo_h1_create
	mirror_topo_h2_create
	mirror_topo_h3_create

	mirror_topo_switch_create
}

mirror_topo_destroy()
{
	mirror_topo_switch_destroy

	mirror_topo_h3_destroy
	mirror_topo_h2_destroy
	mirror_topo_h1_destroy
}
