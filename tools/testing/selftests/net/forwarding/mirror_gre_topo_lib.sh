# SPDX-License-Identifier: GPL-2.0

# This is the standard topology for testing mirroring to gretap and ip6gretap
# netdevices. The tests that use it tweak it in one way or another--importantly,
# $swp3 and $h3 need to have addresses set up.
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
#   |     + $swp3               + gt6 (ip6gretap)      + gt4 (gretap)         |
#   |     |                     : loc=2001:db8:2::1    : loc=192.0.2.129      |
#   |     |                     : rem=2001:db8:2::2    : rem=192.0.2.130      |
#   |     |                     : ttl=100              : ttl=100              |
#   |     |                     : tos=inherit          : tos=inherit          |
#   |     |                     :                      :                      |
#   +-----|---------------------:----------------------:----------------------+
#         |                     :                      :
#   +-----|---------------------:----------------------:----------------------+
#   | H3  + $h3                 + h3-gt6 (ip6gretap)   + h3-gt4 (gretap)      |
#   |                             loc=2001:db8:2::2      loc=192.0.2.130      |
#   |                             rem=2001:db8:2::1      rem=192.0.2.129      |
#   |                             ttl=100                ttl=100              |
#   |                             tos=inherit            tos=inherit          |
#   |                                                                         |
#   +-------------------------------------------------------------------------+

mirror_gre_topo_h1_create()
{
	simple_if_init $h1 192.0.2.1/28
}

mirror_gre_topo_h1_destroy()
{
	simple_if_fini $h1 192.0.2.1/28
}

mirror_gre_topo_h2_create()
{
	simple_if_init $h2 192.0.2.2/28
}

mirror_gre_topo_h2_destroy()
{
	simple_if_fini $h2 192.0.2.2/28
}

mirror_gre_topo_h3_create()
{
	simple_if_init $h3

	tunnel_create h3-gt4 gretap 192.0.2.130 192.0.2.129
	ip link set h3-gt4 vrf v$h3
	matchall_sink_create h3-gt4

	tunnel_create h3-gt6 ip6gretap 2001:db8:2::2 2001:db8:2::1
	ip link set h3-gt6 vrf v$h3
	matchall_sink_create h3-gt6
}

mirror_gre_topo_h3_destroy()
{
	tunnel_destroy h3-gt6
	tunnel_destroy h3-gt4

	simple_if_fini $h3
}

mirror_gre_topo_switch_create()
{
	ip link set dev $swp3 up

	ip link add name br1 type bridge vlan_filtering 1
	ip link set dev br1 up

	ip link set dev $swp1 master br1
	ip link set dev $swp1 up

	ip link set dev $swp2 master br1
	ip link set dev $swp2 up

	tunnel_create gt4 gretap 192.0.2.129 192.0.2.130 \
		      ttl 100 tos inherit

	tunnel_create gt6 ip6gretap 2001:db8:2::1 2001:db8:2::2 \
		      ttl 100 tos inherit allow-localremote

	tc qdisc add dev $swp1 clsact
}

mirror_gre_topo_switch_destroy()
{
	tc qdisc del dev $swp1 clsact

	tunnel_destroy gt6
	tunnel_destroy gt4

	ip link set dev $swp1 down
	ip link set dev $swp2 down
	ip link del dev br1

	ip link set dev $swp3 down
}

mirror_gre_topo_create()
{
	mirror_gre_topo_h1_create
	mirror_gre_topo_h2_create
	mirror_gre_topo_h3_create

	mirror_gre_topo_switch_create
}

mirror_gre_topo_destroy()
{
	mirror_gre_topo_switch_destroy

	mirror_gre_topo_h3_destroy
	mirror_gre_topo_h2_destroy
	mirror_gre_topo_h1_destroy
}
