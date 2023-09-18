# SPDX-License-Identifier: GPL-2.0

TEST_GEN_PROGS := btitest nobtitest

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

$(OUTPUT)/%-bti.o: %.c
	$(BTI_CC_COMMAND)

$(OUTPUT)/%-bti.o: %.S
	$(BTI_CC_COMMAND)

$(OUTPUT)/%-nobti.o: %.c
	$(NOBTI_CC_COMMAND)

$(OUTPUT)/%-nobti.o: %.S
	$(NOBTI_CC_COMMAND)

BTI_OBJS =                                      \
	$(OUTPUT)/test-bti.o                    \
	$(OUTPUT)/signal-bti.o                  \
	$(OUTPUT)/start-bti.o                   \
	$(OUTPUT)/syscall-bti.o                 \
	$(OUTPUT)/system-bti.o                  \
	$(OUTPUT)/teststubs-bti.o               \
	$(OUTPUT)/trampoline-bti.o
$(OUTPUT)/btitest: $(BTI_OBJS)
	$(CC) $(CFLAGS_BTI) $(CFLAGS_COMMON) -nostdlib -static -o $@ $^

NOBTI_OBJS =                                    \
	$(OUTPUT)/test-nobti.o                  \
	$(OUTPUT)/signal-nobti.o                \
	$(OUTPUT)/start-nobti.o                 \
	$(OUTPUT)/syscall-nobti.o               \
	$(OUTPUT)/system-nobti.o                \
	$(OUTPUT)/teststubs-nobti.o             \
	$(OUTPUT)/trampoline-nobti.o
$(OUTPUT)/nobtitest: $(NOBTI_OBJS)
	$(CC) $(CFLAGS_BTI) $(CFLAGS_COMMON) -nostdlib -static -o $@ $^

# Including KSFT lib.mk here will also mangle the TEST_GEN_PROGS list
# to account for any OUTPUT target-dirs optionally provided by
# the toplevel makefile
include ../../lib.mk
