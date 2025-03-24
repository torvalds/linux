# SPDX-License-Identifier: GPL-2.0
# Makefile for net selftests

TEST_PROGS := \
	bond-arp-interval-causes-panic.sh \
	bond-break-lacpdu-tx.sh \
	bond-lladdr-target.sh \
	dev_addr_lists.sh \
	mode-1-recovery-updelay.sh \
	mode-2-recovery-updelay.sh \
	bond_options.sh \
	bond-eth-type-change.sh \
	bond_macvlan_ipvlan.sh

TEST_FILES := \
	lag_lib.sh \
	bond_topo_2d1c.sh \
	bond_topo_3d1c.sh

TEST_INCLUDES := \
	../../../net/forwarding/lib.sh \
	../../../net/lib.sh

include ../../../lib.mk
