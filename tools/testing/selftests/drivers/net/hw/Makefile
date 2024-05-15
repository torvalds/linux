# SPDX-License-Identifier: GPL-2.0+ OR MIT

TEST_PROGS = \
	csum.py \
	devlink_port_split.py \
	ethtool.sh \
	ethtool_extended_state.sh \
	ethtool_mm.sh \
	ethtool_rmon.sh \
	hw_stats_l3.sh \
	hw_stats_l3_gre.sh \
	loopback.sh \
	pp_alloc_fail.py \
	#

TEST_FILES := \
	ethtool_lib.sh \
	#

TEST_INCLUDES := \
	$(wildcard lib/py/*.py ../lib/py/*.py) \
	../../../net/lib.sh \
	../../../net/forwarding/lib.sh \
	../../../net/forwarding/ipip_lib.sh \
	../../../net/forwarding/tc_common.sh \
	#

include ../../../lib.mk
