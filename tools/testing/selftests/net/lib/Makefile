# SPDX-License-Identifier: GPL-2.0

CFLAGS =  -Wall -Wl,--no-as-needed -O2 -g
CFLAGS += -I../../../../../usr/include/ $(KHDR_INCLUDES)
# Additional include paths needed by kselftest.h
CFLAGS += -I../../

TEST_FILES := ../../../../../Documentation/netlink/specs
TEST_FILES += ../../../../net/ynl

TEST_GEN_FILES += csum

TEST_INCLUDES := $(wildcard py/*.py sh/*.sh)

include ../../lib.mk
