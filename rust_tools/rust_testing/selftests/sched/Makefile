# SPDX-License-Identifier: GPL-2.0+

ifneq ($(shell $(CC) --version 2>&1 | head -n 1 | grep clang),)
CLANG_FLAGS += -no-integrated-as
endif

CFLAGS += -O2 -Wall -g -I./ $(KHDR_INCLUDES) -Wl,-rpath=./ \
	  $(CLANG_FLAGS)
LDLIBS += -lpthread

TEST_GEN_FILES := cs_prctl_test
TEST_PROGS := cs_prctl_test

include ../lib.mk
