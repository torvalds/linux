uname_M := $(shell uname -m 2>/dev/null || echo not)
ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/i386/)
ifeq ($(ARCH),i386)
        ARCH := x86
	CFLAGS := -DCONFIG_X86_32 -D__i386__
endif
ifeq ($(ARCH),x86_64)
	ARCH := x86
	CFLAGS := -DCONFIG_X86_64 -D__x86_64__
endif

CFLAGS += -I../../../../usr/include/

all:
	$(CC) $(CFLAGS) msgque.c -o msgque_test

TEST_PROGS := msgque_test

include ../lib.mk

clean:
	rm -fr ./msgque_test
