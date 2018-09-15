# SPDX-License-Identifier: GPL-2.0
CFLAGS += -Wall

all:

TEST_GEN_PROGS = test_memcontrol
TEST_GEN_PROGS += test_core

include ../lib.mk

$(OUTPUT)/test_memcontrol: cgroup_util.c
$(OUTPUT)/test_core: cgroup_util.c
