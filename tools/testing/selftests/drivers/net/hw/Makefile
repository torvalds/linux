# SPDX-License-Identifier: GPL-2.0+ OR MIT

# Check if io_uring supports zero-copy receive
HAS_IOURING_ZCRX := $(shell \
	echo -e '#include <liburing.h>\n' \
	     'void *func = (void *)io_uring_register_ifq;\n' \
	     'int main() {return 0;}' | \
	$(CC) -luring -x c - -o /dev/null 2>&1 && echo y)

ifeq ($(HAS_IOURING_ZCRX),y)
COND_GEN_FILES += iou-zcrx
else
$(warning excluding iouring tests, liburing not installed or too old)
endif

TEST_GEN_FILES := \
	$(COND_GEN_FILES) \
# end of TEST_GEN_FILES

TEST_PROGS = \
	csum.py \
	devlink_port_split.py \
	devlink_rate_tc_bw.py \
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
	rss_drv.py \
	rss_flow_label.py \
	rss_input_xfrm.py \
	toeplitz.py \
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
YNL_GEN_FILES := \
	ncdevmem \
	toeplitz \
# end of YNL_GEN_FILES
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

ifeq ($(HAS_IOURING_ZCRX),y)
$(OUTPUT)/iou-zcrx: LDLIBS += -luring
endif
