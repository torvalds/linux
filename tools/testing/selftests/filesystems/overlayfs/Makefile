# SPDX-License-Identifier: GPL-2.0

CFLAGS += -Wall
CFLAGS += $(KHDR_INCLUDES)
LDLIBS += -lcap

LOCAL_HDRS += wrappers.h log.h

TEST_GEN_PROGS := dev_in_maps
TEST_GEN_PROGS += set_layers_via_fds

include ../../lib.mk

$(OUTPUT)/set_layers_via_fds: ../utils.c
