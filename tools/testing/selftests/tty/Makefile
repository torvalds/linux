# SPDX-License-Identifier: GPL-2.0
CFLAGS = -O2 -Wall
TEST_GEN_PROGS := tty_tstamp_update tty_tiocsti_test
LDLIBS += -lcap

include ../lib.mk

# Add libcap for TIOCSTI test
$(OUTPUT)/tty_tiocsti_test: LDLIBS += -lcap
