#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test for "tc action mirred egress mirror" when the underlay route points at a
# bridge device without vlan filtering (802.1d).
#
# This test uses standard topology for testing mirror-to-gretap. See
# mirror_gre_topo_lib.sh for more details. The full topology is as follows:
#
#  +---------------------+                             +---------------------+
#  | H1                  |                             |                  H2 |
#  |     + $h1           |                             |           $h2 +     |
#  |     | 192.0.2.1/28  |                             |  192.0.2.2/28 |     |
#  +-----|---------------+                             +---------------|-----+
#        |                                                             |
#  +-----|-------------------------------------------------------------|-----+
#  | SW  o---> mirror                                                  |     |
#  | +---|-------------------------------------------------------------|---+ |
#  | |   + $swp1            + br1 (802.1q bridge)                $swp2 +   | |
#  | +---------------------------------------------------------------------+ |
#  |                                                                         |
#  | +---------------------------------------------------------------------+ |
#  | |                      + br2 (802.1d bridge)                          | |
#  | |                        192.0.2.129/28                               | |
#  | |   + $swp3              2001:db8:2::1/64                             | |
#  | +---|-----------------------------------------------------------------+ |
#  |     |                                          ^                    ^   |
#  |     |                     + gt6 (ip6gretap)    | + gt4 (gretap)     |   |
#  |     |                     : loc=2001:db8:2::1  | : loc=192.0.2.129  |   |
#  |     |                     : rem=2001:db8:2::2 -+ : rem=192.0.2.130 -+   |
#  |     |                     : ttl=100              : ttl=100              |
#  |     |                     : tos=inherit          : tos=inherit          |
#  +-----|---------------------:----------------------:----------------------+
#        |                     :                      :
#  +-----|---------------------:----------------------:----------------------+
#  | H3  + $h3                 + h3-gt6(ip6gretap)    + h3-gt4 (gretap)      |
#  |       192.0.2.130/28        loc=2001:db8:2::2      loc=192.0.2.130      |
#  |       2001:db8:2::2/64      rem=2001:db8:2::1      rem=192.0.2.129      |
#  |                             ttl=100                ttl=100              |
#  |                             tos=inherit            tos=inherit          |
#  +-------------------------------------------------------------------------+

ALL_TESTS="
	test_gretap
	test_ip6gretap
"

NUM_NETIFS=6
source lib.sh
source mirror_lib.sh
source mirror_gre_lib.sh
source mirror_gre_topo_lib.sh

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	swp3=${NETIFS[p5]}
	h3=${NETIFS[p6]}

	vrf_prepare
	mirror_gre_topo_create

	ip link add name br2 address $(mac_get $swp3) \
		type bridge vlan_filtering 0
	ip link set dev br2 up

	ip link set dev $swp3 master br2
	ip route add 192.0.2.130/32 dev br2
	ip -6 route add 2001:db8:2::2/128 dev br2

	ip address add dev br2 192.0.2.129/28
	ip address add dev br2 2001:db8:2::1/64

	ip address add dev $h3 192.0.2.130/28
	ip address add dev $h3 2001:db8:2::2/64
}

cleanup()
{
	pre_cleanup

	ip address del dev $h3 2001:db8:2::2/64
	ip address del dev $h3 192.0.2.130/28
	ip link del dev br2

	mirror_gre_topo_destroy
	vrf_cleanup
}

test_gretap()
{
	ip neigh replace 192.0.2.130 lladdr $(mac_get $h3) \
		 nud permanent dev br2
	full_test_span_gre_dir gt4 ingress 8 0 "mirror to gretap"
	full_test_span_gre_dir gt4 egress 0 8 "mirror to gretap"
}

test_ip6gretap()
{
	ip neigh replace 2001:db8:2::2 lladdr $(mac_get $h3) \
		nud permanent dev br2
	full_test_span_gre_dir gt6 ingress 8 0 "mirror to ip6gretap"
	full_test_span_gre_dir gt6 egress 0 8 "mirror to ip6gretap"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
