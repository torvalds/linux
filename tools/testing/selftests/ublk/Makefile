# SPDX-License-Identifier: GPL-2.0

CFLAGS += -O3 -Wl,-no-as-needed -Wall -I $(top_srcdir)
LDLIBS += -lpthread -lm -luring

TEST_PROGS := test_generic_01.sh

TEST_PROGS += test_null_01.sh
TEST_PROGS += test_null_02.sh
TEST_PROGS += test_loop_01.sh
TEST_PROGS += test_loop_02.sh
TEST_PROGS += test_loop_03.sh
TEST_PROGS += test_loop_04.sh
TEST_PROGS += test_stripe_01.sh
TEST_PROGS += test_stripe_02.sh

TEST_PROGS += test_stress_01.sh
TEST_PROGS += test_stress_02.sh

TEST_GEN_PROGS_EXTENDED = kublk

include ../lib.mk

$(TEST_GEN_PROGS_EXTENDED): kublk.c null.c file_backed.c common.c stripe.c

check:
	shellcheck -x -f gcc *.sh
