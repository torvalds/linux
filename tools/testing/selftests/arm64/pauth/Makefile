# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2020 ARM Limited

# preserve CC value from top level Makefile
ifeq ($(CC),cc)
CC := $(CROSS_COMPILE)gcc
endif

CFLAGS += -mbranch-protection=pac-ret

# All supported LLVMs have PAC, test for GCC
ifeq ($(LLVM),1)
pauth_cc_support := 1
else
# check if the compiler supports ARMv8.3 and branch protection with PAuth
pauth_cc_support := $(shell if ($(CC) $(CFLAGS) -march=armv8.3-a -E -x c /dev/null -o /dev/null 2>&1) then echo "1"; fi)
endif

ifeq ($(pauth_cc_support),1)
TEST_GEN_PROGS := pac
TEST_GEN_FILES := pac_corruptor.o helper.o
TEST_GEN_PROGS_EXTENDED := exec_target
endif

include ../../lib.mk

ifeq ($(pauth_cc_support),1)
# pac* and aut* instructions are not available on architectures berfore
# ARMv8.3. Therefore target ARMv8.3 wherever they are used directly
$(OUTPUT)/pac_corruptor.o: pac_corruptor.S
	$(CC) -c $^ -o $@ $(CFLAGS) -march=armv8.3-a

$(OUTPUT)/helper.o: helper.c
	$(CC) -c $^ -o $@ $(CFLAGS) -march=armv8.3-a

# when -mbranch-protection is enabled and the target architecture is ARMv8.3 or
# greater, gcc emits pac* instructions which are not in HINT NOP space,
# preventing the tests from occurring at all. Compile for ARMv8.2 so tests can
# run on earlier targets and print a meaningful error messages
$(OUTPUT)/exec_target: exec_target.c $(OUTPUT)/helper.o
	$(CC) $^ -o $@ $(CFLAGS) -march=armv8.2-a

$(OUTPUT)/pac: pac.c $(OUTPUT)/pac_corruptor.o $(OUTPUT)/helper.o
	$(CC) $^ -o $@ $(CFLAGS) -march=armv8.2-a
endif
