# SPDX-License-Identifier: GPL-2.0

TEST_GEN_PROGS := btitest nobtitest

PROGS := $(patsubst %,gen/%,$(TEST_GEN_PROGS))

# These tests are built as freestanding binaries since otherwise BTI
# support in ld.so is required which is not currently widespread; when
# it is available it will still be useful to test this separately as the
# cases for statically linked and dynamically lined binaries are
# slightly different.

CFLAGS_NOBTI = -mbranch-protection=none -DBTI=0
CFLAGS_BTI = -mbranch-protection=standard -DBTI=1

CFLAGS_COMMON = -ffreestanding -Wall -Wextra $(CFLAGS)

BTI_CC_COMMAND = $(CC) $(CFLAGS_BTI) $(CFLAGS_COMMON) -c -o $@ $<
NOBTI_CC_COMMAND = $(CC) $(CFLAGS_NOBTI) $(CFLAGS_COMMON) -c -o $@ $<

%-bti.o: %.c
	$(BTI_CC_COMMAND)

%-bti.o: %.S
	$(BTI_CC_COMMAND)

%-nobti.o: %.c
	$(NOBTI_CC_COMMAND)

%-nobti.o: %.S
	$(NOBTI_CC_COMMAND)

BTI_OBJS =                                      \
	test-bti.o                           \
	signal-bti.o                            \
	start-bti.o                             \
	syscall-bti.o                           \
	system-bti.o                            \
	teststubs-bti.o                         \
	trampoline-bti.o
gen/btitest: $(BTI_OBJS)
	$(CC) $(CFLAGS_BTI) $(CFLAGS_COMMON) -nostdlib -static -o $@ $^

NOBTI_OBJS =                                    \
	test-nobti.o                         \
	signal-nobti.o                          \
	start-nobti.o                           \
	syscall-nobti.o                         \
	system-nobti.o                          \
	teststubs-nobti.o                       \
	trampoline-nobti.o
gen/nobtitest: $(NOBTI_OBJS)
	$(CC) $(CFLAGS_BTI) $(CFLAGS_COMMON) -nostdlib -static -o $@ $^

# Including KSFT lib.mk here will also mangle the TEST_GEN_PROGS list
# to account for any OUTPUT target-dirs optionally provided by
# the toplevel makefile
include ../../lib.mk

$(TEST_GEN_PROGS): $(PROGS)
	cp $(PROGS) $(OUTPUT)/
