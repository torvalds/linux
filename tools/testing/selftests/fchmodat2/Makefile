# SPDX-License-Identifier: GPL-2.0-or-later

CFLAGS += -Wall -O2 -g -fsanitize=address -fsanitize=undefined $(KHDR_INCLUDES)

# gcc requires -static-libasan in order to ensure that Address Sanitizer's
# library is the first one loaded. However, clang already statically links the
# Address Sanitizer if -fsanitize is specified. Therefore, simply omit
# -static-libasan for clang builds.
ifeq ($(LLVM),)
    CFLAGS += -static-libasan
endif

TEST_GEN_PROGS := fchmodat2_test

include ../lib.mk
