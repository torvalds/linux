# SPDX-License-Identifier: GPL-2.0-or-later

CFLAGS += -Wall -O2 -g $(KHDR_INCLUDES) $(TOOLS_INCLUDES)
LDLIBS += -lcap

TEST_GEN_PROGS := mount-notify_test mount-notify_test_ns

include ../../lib.mk

$(OUTPUT)/mount-notify_test: ../utils.c
$(OUTPUT)/mount-notify_test_ns: ../utils.c
