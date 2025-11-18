# SPDX-License-Identifier: GPL-2.0

CFLAGS += -O3 -Wl,-no-as-needed -Wall -I $(top_srcdir)/usr/include
ifneq ($(WERROR),0)
	CFLAGS += -Werror
endif

LDLIBS += -lpthread -lm -luring

TEST_PROGS := test_generic_01.sh
TEST_PROGS += test_generic_02.sh
TEST_PROGS += test_generic_03.sh
TEST_PROGS += test_generic_04.sh
TEST_PROGS += test_generic_05.sh
TEST_PROGS += test_generic_06.sh
TEST_PROGS += test_generic_07.sh

TEST_PROGS += test_generic_08.sh
TEST_PROGS += test_generic_09.sh
TEST_PROGS += test_generic_10.sh
TEST_PROGS += test_generic_11.sh
TEST_PROGS += test_generic_12.sh
TEST_PROGS += test_generic_13.sh

TEST_PROGS += test_null_01.sh
TEST_PROGS += test_null_02.sh
TEST_PROGS += test_loop_01.sh
TEST_PROGS += test_loop_02.sh
TEST_PROGS += test_loop_03.sh
TEST_PROGS += test_loop_04.sh
TEST_PROGS += test_loop_05.sh
TEST_PROGS += test_stripe_01.sh
TEST_PROGS += test_stripe_02.sh
TEST_PROGS += test_stripe_03.sh
TEST_PROGS += test_stripe_04.sh

TEST_PROGS += test_stress_01.sh
TEST_PROGS += test_stress_02.sh
TEST_PROGS += test_stress_03.sh
TEST_PROGS += test_stress_04.sh
TEST_PROGS += test_stress_05.sh

TEST_GEN_PROGS_EXTENDED = kublk

include ../lib.mk

$(TEST_GEN_PROGS_EXTENDED): kublk.c null.c file_backed.c common.c stripe.c \
	fault_inject.c

check:
	shellcheck -x -f gcc *.sh
