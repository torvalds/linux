# SPDX-License-Identifier: GPL-2.0
CFLAGS += $(KHDR_INCLUDES)

TEST_INCLUDES := $(wildcard lib/py/*.py) \
		 $(wildcard lib/sh/*.sh) \
		 ../../net/lib.sh \

TEST_GEN_FILES := \
	napi_id_helper \
	so_txtime \
# end of TEST_GEN_FILES

TEST_PROGS := \
	gro.py \
	hds.py \
	macsec.py \
	napi_id.py \
	napi_threaded.py \
	netpoll_basic.py \
	ping.py \
	psp.py \
	queues.py \
	ring_reconfig.py \
	shaper.py \
	so_txtime.py \
	stats.py \
	xdp.py \
# end of TEST_PROGS

# YNL files, must be before "include ..lib.mk"
YNL_GEN_FILES := psp_responder
TEST_GEN_FILES += $(YNL_GEN_FILES)

include ../../lib.mk

# YNL build
YNL_GENS := psp

include ../../net/ynl.mk
