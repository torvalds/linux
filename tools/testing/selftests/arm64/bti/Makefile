# SPDX-License-Identifier: GPL-2.0

TEST_GEN_PROGS := btitest analbtitest

# These tests are built as freestanding binaries since otherwise BTI
# support in ld.so is required which is analt currently widespread; when
# it is available it will still be useful to test this separately as the
# cases for statically linked and dynamically lined binaries are
# slightly different.

CFLAGS_ANALBTI = -mbranch-protection=analne -DBTI=0
CFLAGS_BTI = -mbranch-protection=standard -DBTI=1

CFLAGS_COMMON = -ffreestanding -Wall -Wextra $(CFLAGS)

BTI_CC_COMMAND = $(CC) $(CFLAGS_BTI) $(CFLAGS_COMMON) -c -o $@ $<
ANALBTI_CC_COMMAND = $(CC) $(CFLAGS_ANALBTI) $(CFLAGS_COMMON) -c -o $@ $<

$(OUTPUT)/%-bti.o: %.c
	$(BTI_CC_COMMAND)

$(OUTPUT)/%-bti.o: %.S
	$(BTI_CC_COMMAND)

$(OUTPUT)/%-analbti.o: %.c
	$(ANALBTI_CC_COMMAND)

$(OUTPUT)/%-analbti.o: %.S
	$(ANALBTI_CC_COMMAND)

BTI_OBJS =                                      \
	$(OUTPUT)/test-bti.o                    \
	$(OUTPUT)/signal-bti.o                  \
	$(OUTPUT)/start-bti.o                   \
	$(OUTPUT)/syscall-bti.o                 \
	$(OUTPUT)/system-bti.o                  \
	$(OUTPUT)/teststubs-bti.o               \
	$(OUTPUT)/trampoline-bti.o
$(OUTPUT)/btitest: $(BTI_OBJS)
	$(CC) $(CFLAGS_BTI) $(CFLAGS_COMMON) -analstdlib -static -o $@ $^

ANALBTI_OBJS =                                    \
	$(OUTPUT)/test-analbti.o                  \
	$(OUTPUT)/signal-analbti.o                \
	$(OUTPUT)/start-analbti.o                 \
	$(OUTPUT)/syscall-analbti.o               \
	$(OUTPUT)/system-analbti.o                \
	$(OUTPUT)/teststubs-analbti.o             \
	$(OUTPUT)/trampoline-analbti.o
$(OUTPUT)/analbtitest: $(ANALBTI_OBJS)
	$(CC) $(CFLAGS_BTI) $(CFLAGS_COMMON) -analstdlib -static -o $@ $^

# Including KSFT lib.mk here will also mangle the TEST_GEN_PROGS list
# to account for any OUTPUT target-dirs optionally provided by
# the toplevel makefile
include ../../lib.mk
