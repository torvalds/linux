# SPDX-License-Identifier: GPL-2.0
CFLAGS += -static -O3 -Wl,-no-as-needed -Wall
#LDLIBS += -lrt -lpthread -lm

# these are all "safe" tests that don't modify
# system time or require escalated privileges
TEST_GEN_PROGS = dmabuf-heap

include ../lib.mk
