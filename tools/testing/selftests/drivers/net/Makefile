# SPDX-License-Identifier: GPL-2.0
CFLAGS += $(KHDR_INCLUDES)

TEST_INCLUDES := $(wildcard lib/py/*.py) \
		 $(wildcard lib/sh/*.sh) \
		 ../../net/lib.sh \

TEST_GEN_FILES := \
	napi_id_helper \
# end of TEST_GEN_FILES

TEST_PROGS := \
	napi_id.py \
	napi_threaded.py \
	netcons_basic.sh \
	netcons_cmdline.sh \
	netcons_fragmented_msg.sh \
	netcons_overflow.sh \
	netcons_sysdata.sh \
	netpoll_basic.py \
	ping.py \
	queues.py \
	stats.py \
	shaper.py \
	hds.py \
	xdp.py \
# end of TEST_PROGS

include ../../lib.mk
