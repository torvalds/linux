# SPDX-License-Identifier: GPL-2.0+ OR MIT

TEST_GEN_FILES = iou-zcrx

TEST_PROGS = \
	csum.py \
	devlink_port_split.py \
	devmem.py \
	ethtool.sh \
	ethtool_extended_state.sh \
	ethtool_mm.sh \
	ethtool_rmon.sh \
	hw_stats_l3.sh \
	hw_stats_l3_gre.sh \
	iou-zcrx.py \
	irq.py \
	loopback.sh \
	nic_timestamp.py \
	pp_alloc_fail.py \
	rss_api.py \
	rss_ctx.py \
	rss_flow_label.py \
	rss_input_xfrm.py \
	tso.py \
	xsk_reconfig.py \
	#

TEST_FILES := \
	ethtool_lib.sh \
	#

TEST_INCLUDES := \
	$(wildcard lib/py/*.py ../lib/py/*.py) \
	../../../net/lib.sh \
	../../../net/forwarding/ipip_lib.sh \
	../../../net/forwarding/lib.sh \
	../../../net/forwarding/tc_common.sh \
	#

# YNL files, must be before "include ..lib.mk"
YNL_GEN_FILES := ncdevmem
TEST_GEN_FILES += $(YNL_GEN_FILES)
TEST_GEN_FILES += $(patsubst %.c,%.o,$(wildcard *.bpf.c))

include ../../../lib.mk

# YNL build
YNL_GENS := \
	ethtool \
	netdev \
# end of YNL_GENS

include ../../../net/ynl.mk

include ../../../net/bpf.mk

$(OUTPUT)/iou-zcrx: LDLIBS += -luring
