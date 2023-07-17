#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

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
#   | +---------------------------------------------------------------------+ |
#   | | OL                      + gt6 (ip6gretap)      + gt4 (gretap)       | |
#   | |                         : loc=2001:db8:2::1    : loc=192.0.2.129    | |
#   | |                         : rem=2001:db8:2::2    : rem=192.0.2.130    | |
#   | |                         : ttl=100              : ttl=100            | |
#   | |                         : tos=inherit          : tos=inherit        | |
#   | +-------------------------:--|-------------------:--|-----------------+ |
#   |                           :  |                   :  |                   |
#   | +-------------------------:--|-------------------:--|-----------------+ |
#   | | UL                      :  |,---------------------'                 | |
#   | |   + $swp3               :  ||                  :                    | |
#   | |   | 192.0.2.129/28      :  vv                  :                    | |
#   | |   | 2001:db8:2::1/64    :  + ul (dummy)        :                    | |
#   | +---|---------------------:----------------------:--------------------+ |
#   +-----|---------------------:----------------------:----------------------+
#         |                     :                      :
#   +-----|---------------------:----------------------:----------------------+
#   | H3  + $h3                 + h3-gt6 (ip6gretap)   + h3-gt4 (gretap)      |
#   |       192.0.2.130/28        loc=2001:db8:2::2      loc=192.0.2.130      |
#   |       2001:db8:2::2/64      rem=2001:db8:2::1      rem=192.0.2.129      |
#   |                             ttl=100                ttl=100              |
#   |                             tos=inherit            tos=inherit          |
#   |                                                                         |
#   +-------------------------------------------------------------------------+
#
# This tests mirroring to gretap and ip6gretap configured in an overlay /
# underlay manner, i.e. with a bound dummy device that marks underlay VRF where
# the encapsulated packed should be routed.

ALL_TESTS="
	test_gretap
	test_ip6gretap
"

NUM_NETIFS=6
source lib.sh
source mirror_lib.sh
source mirror_gre_lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/28
}

h1_destroy()
{
	simple_if_fini $h1 192.0.2.1/28
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/28
}

h2_destroy()
{
	simple_if_fini $h2 192.0.2.2/28
}

h3_create()
{
	simple_if_init $h3 192.0.2.130/28 2001:db8:2::2/64

	tunnel_create h3-gt4 gretap 192.0.2.130 192.0.2.129
	ip link set h3-gt4 vrf v$h3
	matchall_sink_create h3-gt4

	tunnel_create h3-gt6 ip6gretap 2001:db8:2::2 2001:db8:2::1
	ip link set h3-gt6 vrf v$h3
	matchall_sink_create h3-gt6
}

h3_destroy()
{
	tunnel_destroy h3-gt6
	tunnel_destroy h3-gt4

	simple_if_fini $h3 192.0.2.130/28 2001:db8:2::2/64
}

switch_create()
{
	# Bridge between H1 and H2.

	ip link add name br1 type bridge vlan_filtering 1
	ip link set dev br1 addrgenmode none
	ip link set dev br1 up

	ip link set dev $swp1 master br1
	ip link set dev $swp1 up

	ip link set dev $swp2 master br1
	ip link set dev $swp2 up

	tc qdisc add dev $swp1 clsact

	# Underlay.

	simple_if_init $swp3 192.0.2.129/28 2001:db8:2::1/64

	ip link add name ul type dummy
	ip link set dev ul master v$swp3
	ip link set dev ul up

	# Overlay.

	vrf_create vrf-ol
	ip link set dev vrf-ol up

	tunnel_create gt4 gretap 192.0.2.129 192.0.2.130 \
		      ttl 100 tos inherit dev ul
	ip link set dev gt4 master vrf-ol
	ip link set dev gt4 up

	tunnel_create gt6 ip6gretap 2001:db8:2::1 2001:db8:2::2 \
		      ttl 100 tos inherit dev ul allow-localremote
	ip link set dev gt6 master vrf-ol
	ip link set dev gt6 up
}

switch_destroy()
{
	vrf_destroy vrf-ol

	tunnel_destroy gt6
	tunnel_destroy gt4

	simple_if_fini $swp3 192.0.2.129/28 2001:db8:2::1/64

	ip link del dev ul

	tc qdisc del dev $swp1 clsact

	ip link set dev $swp1 down
	ip link set dev $swp2 down
	ip link del dev br1
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	swp3=${NETIFS[p5]}
	h3=${NETIFS[p6]}

	vrf_prepare

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

	vrf_cleanup
}

test_gretap()
{
	full_test_span_gre_dir gt4 ingress 8 0 "mirror to gretap w/ UL"
	full_test_span_gre_dir gt4 egress  0 8 "mirror to gretap w/ UL"
}

test_ip6gretap()
{
	full_test_span_gre_dir gt6 ingress 8 0 "mirror to ip6gretap w/ UL"
	full_test_span_gre_dir gt6 egress  0 8 "mirror to ip6gretap w/ UL"
}

test_all()
{
	RET=0

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
