# SPDX-License-Identifier: GPL-2.0
# Makefile for net selftests

TEST_PROGS := \
	decoupled_enablement.sh \
	dev_addr_lists.sh \
	non_ether_header_ops.sh \
	options.sh \
	propagation.sh \
	refleak.sh \
	teamd_activebackup.sh \
	transmit_failover.sh \
# end of TEST_PROGS

TEST_INCLUDES := \
	team_lib.sh \
	../bonding/lag_lib.sh \
	../../../net/forwarding/lib.sh \
	../../../net/in_netns.sh \
	../../../net/lib.sh \
	../../../net/lib/sh/defer.sh \
# end of TEST_INCLUDES

include ../../../lib.mk
