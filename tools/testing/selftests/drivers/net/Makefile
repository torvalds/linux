# SPDX-License-Identifier: GPL-2.0
CFLAGS += $(KHDR_INCLUDES)

TEST_INCLUDES := $(wildcard lib/py/*.py) \
		 $(wildcard lib/sh/*.sh) \
		 ../../net/net_helper.sh \
		 ../../net/lib.sh \

TEST_GEN_FILES := xdp_helper

TEST_PROGS := \
	netcons_basic.sh \
	netcons_fragmented_msg.sh \
	netcons_overflow.sh \
	netcons_sysdata.sh \
	ping.py \
	queues.py \
	stats.py \
	shaper.py \
	hds.py \
# end of TEST_PROGS

include ../../lib.mk
