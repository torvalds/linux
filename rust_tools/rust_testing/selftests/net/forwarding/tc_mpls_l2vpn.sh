#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +-----------------------+
# | H1 (v$h1)             |
# | 192.0.2.1/24          |
# | 2001:db8::1/124       |
# |                 + $h1 |
# +-----------------|-----+
#                   |
#                   | (Plain Ethernet traffic)
#                   |
# +-----------------|-----------------------------------------+
# | LER1            + $edge1                                  |
# |                     -ingress:                             |
# |                       -encapsulate Ethernet into MPLS     |
# |                       -add outer Ethernet header          |
# |                       -redirect to $mpls1 (egress)        |
# |                                                           |
# |                 + $mpls1                                  |
# |                 |   -ingress:                             |
# |                 |     -remove outer Ethernet header       |
# |                 |     -remove MPLS header                 |
# |                 |     -redirect to $edge1 (egress)        |
# +-----------------|-----------------------------------------+
#                   |
#                   | (Ethernet over MPLS traffic)
#                   |
# +-----------------|-----------------------------------------+
# | LER2            + $mpls2                                  |
# |                     -ingress:                             |
# |                       -remove outer Ethernet header       |
# |                       -remove MPLS header                 |
# |                       -redirect to $edge2 (egress)        |
# |                                                           |
# |                 + $edge2                                  |
# |                 |   -ingress:                             |
# |                 |     -encapsulate Ethernet into MPLS     |
# |                 |     -add outer Ethernet header          |
# |                 |     -redirect to $mpls2 (egress)        |
# +-----------------|-----------------------------------------|
#                   |
#                   | (Plain Ethernet traffic)
#                   |
# +-----------------|-----+
# | H2 (v$h2)       |     |
# |                 + $h2 |
# | 192.0.2.2/24          |
# | 2001:db8::2/124       |
# +-----------------------+
#
# LER1 and LER2 logically represent two different routers. However, no VRF is
# created for them, as they don't do any IP routing.

ALL_TESTS="mpls_forward_eth"
NUM_NETIFS=6
source lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/24 2001:db8::1/124
}

h1_destroy()
{
	simple_if_fini $h1 192.0.2.1/24 2001:db8::1/124
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/24 2001:db8::2/124
}

h2_destroy()
{
	simple_if_fini $h2 192.0.2.2/24 2001:db8::2/124
}

ler1_create()
{
	tc qdisc add dev $edge1 ingress
	tc filter add dev $edge1 ingress                            \
	   matchall                                                 \
	   action mpls mac_push label 102                           \
	   action vlan push_eth dst_mac $mpls2mac src_mac $mpls1mac \
	   action mirred egress redirect dev $mpls1
	ip link set dev $edge1 up

	tc qdisc add dev $mpls1 ingress
	tc filter add dev $mpls1 ingress            \
	   protocol mpls_uc                         \
	   flower mpls_label 101                    \
	   action vlan pop_eth                      \
	   action mpls pop protocol teb             \
	   action mirred egress redirect dev $edge1
	ip link set dev $mpls1 up
}

ler1_destroy()
{
	ip link set dev $mpls1 down
	tc qdisc del dev $mpls1 ingress

	ip link set dev $edge1 down
	tc qdisc del dev $edge1 ingress
}

ler2_create()
{
	tc qdisc add dev $edge2 ingress
	tc filter add dev $edge2 ingress                            \
	   matchall                                                 \
	   action mpls mac_push label 101                           \
	   action vlan push_eth dst_mac $mpls1mac src_mac $mpls2mac \
	   action mirred egress redirect dev $mpls2
	ip link set dev $edge2 up

	tc qdisc add dev $mpls2 ingress
	tc filter add dev $mpls2 ingress            \
	   protocol mpls_uc                         \
	   flower mpls_label 102                    \
	   action vlan pop_eth                      \
	   action mpls pop protocol teb             \
	   action mirred egress redirect dev $edge2
	ip link set dev $mpls2 up
}

ler2_destroy()
{
	ip link set dev $mpls2 down
	tc qdisc del dev $mpls2 ingress

	ip link set dev $edge2 down
	tc qdisc del dev $edge2 ingress
}

mpls_forward_eth()
{
	ping_test $h1 192.0.2.2
	ping6_test $h1 2001:db8::2
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	edge1=${NETIFS[p2]}

	mpls1=${NETIFS[p3]}
	mpls2=${NETIFS[p4]}

	edge2=${NETIFS[p5]}
	h2=${NETIFS[p6]}

	mpls1mac=$(mac_get $mpls1)
	mpls2mac=$(mac_get $mpls2)

	vrf_prepare

	h1_create
	h2_create
	ler1_create
	ler2_create
}

cleanup()
{
	pre_cleanup

	ler2_destroy
	ler1_destroy
	h2_destroy
	h1_destroy

	vrf_cleanup
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

tc_offload_check
if [[ $? -ne 0 ]]; then
	log_info "Could not test offloaded functionality"
else
	tcflags="skip_sw"
	tests_run
fi

exit $EXIT_STATUS
