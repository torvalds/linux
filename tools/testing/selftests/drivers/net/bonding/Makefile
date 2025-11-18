# SPDX-License-Identifier: GPL-2.0
# Makefile for net selftests

TEST_PROGS := \
	bond-arp-interval-causes-panic.sh \
	bond-break-lacpdu-tx.sh \
	bond-eth-type-change.sh \
	bond-lladdr-target.sh \
	bond_ipsec_offload.sh \
	bond_lacp_prio.sh \
	bond_macvlan_ipvlan.sh \
	bond_options.sh \
	bond_passive_lacp.sh \
	dev_addr_lists.sh \
	mode-1-recovery-updelay.sh \
	mode-2-recovery-updelay.sh \
	netcons_over_bonding.sh \
# end of TEST_PROGS

TEST_FILES := \
	bond_topo_2d1c.sh \
	bond_topo_3d1c.sh \
	lag_lib.sh \
# end of TEST_FILES

TEST_INCLUDES := \
	../../../net/lib.sh \
	../lib/sh/lib_netcons.sh \
	../../../net/forwarding/lib.sh \
# end of TEST_INCLUDES

include ../../../lib.mk
