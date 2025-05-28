# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2020-2025 OpenVPN, Inc.
#
CFLAGS = -pedantic -Wextra -Wall -Wl,--no-as-needed -g -O0 -ggdb $(KHDR_INCLUDES)
VAR_CFLAGS = $(shell pkg-config --cflags libnl-3.0 libnl-genl-3.0 2>/dev/null)
ifeq ($(VAR_CFLAGS),)
VAR_CFLAGS = -I/usr/include/libnl3
endif
CFLAGS += $(VAR_CFLAGS)


LDLIBS = -lmbedtls -lmbedcrypto
VAR_LDLIBS = $(shell pkg-config --libs libnl-3.0 libnl-genl-3.0 2>/dev/null)
ifeq ($(VAR_LDLIBS),)
VAR_LDLIBS = -lnl-genl-3 -lnl-3
endif
LDLIBS += $(VAR_LDLIBS)


TEST_FILES = common.sh

TEST_PROGS = test.sh \
	test-large-mtu.sh \
	test-chachapoly.sh \
	test-tcp.sh \
	test-float.sh \
	test-close-socket.sh \
	test-close-socket-tcp.sh

TEST_GEN_FILES := ovpn-cli

include ../../lib.mk
