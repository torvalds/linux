# SPDX-License-Identifier: GPL-2.0-or-later

CFLAGS += $(KHDR_INCLUDES)
CFLAGS += -Wall -O2 -g $(TOOLS_INCLUDES)
TEST_GEN_PROGS := openat2_test resolve_test rename_attack_test emptypath_test

LOCAL_HDRS += helpers.h

include ../../lib.mk

# gcc requires -static-libasan in order to ensure that Address Sanitizer's
# library is the first one loaded. However, clang already statically links the
# Address Sanitizer if -fsanitize is specified. Therefore, simply omit
# -static-libasan for clang builds.
SANITIZER_CFLAGS := -fsanitize=address -fsanitize=undefined
ifeq ($(LLVM),)
    SANITIZER_CFLAGS += -static-libasan
endif

# Build without the sanitizers when the toolchain cannot link them.
ifeq ($(shell echo 'int main(void) { return 0; }' | $(CC) $(CFLAGS) \
	$(SANITIZER_CFLAGS) $(LDFLAGS) -x c - -o /dev/null >/dev/null 2>&1 && \
	echo y),y)
CFLAGS += $(SANITIZER_CFLAGS)
endif
