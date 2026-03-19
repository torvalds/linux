# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2020-2025 OpenVPN, Inc.
#
CFLAGS = -pedantic -Wextra -Wall -Wl,--no-as-needed -g -O0 -ggdb $(KHDR_INCLUDES)
CFLAGS += $(shell pkg-config --cflags mbedcrypto-3 mbedtls-3 2>/dev/null)

VAR_CFLAGS = $(shell pkg-config --cflags libnl-3.0 libnl-genl-3.0 2>/dev/null)
ifeq ($(VAR_CFLAGS),)
VAR_CFLAGS = -I/usr/include/libnl3
endif
CFLAGS += $(VAR_CFLAGS)

MTLS_LDLIBS= $(shell pkg-config --libs mbedcrypto-3 mbedtls-3 2>/dev/null)
ifeq ($(MTLS_LDLIBS),)
MTLS_LDLIBS = -lmbedtls -lmbedcrypto
endif
LDLIBS += $(MTLS_LDLIBS)

NL_LDLIBS = $(shell pkg-config --libs libnl-3.0 libnl-genl-3.0 2>/dev/null)
ifeq ($(NL_LDLIBS),)
NL_LDLIBS = -lnl-genl-3 -lnl-3
endif
LDLIBS += $(NL_LDLIBS)


TEST_FILES = \
	common.sh \
	data64.key \
	json \
	tcp_peers.txt \
	udp_peers.txt \
	../../../../net/ynl/pyynl/cli.py \
# end of TEST_FILES

TEST_PROGS := \
	test-chachapoly.sh \
	test-close-socket-tcp.sh \
	test-close-socket.sh \
	test-float.sh \
	test-large-mtu.sh \
	test-mark.sh \
	test-symmetric-id-float.sh \
	test-symmetric-id-tcp.sh \
	test-symmetric-id.sh \
	test-tcp.sh \
	test.sh \
# end of TEST_PROGS

TEST_GEN_FILES := ovpn-cli

include ../../lib.mk
