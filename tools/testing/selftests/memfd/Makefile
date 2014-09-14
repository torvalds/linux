uname_M := $(shell uname -m 2>/dev/null || echo not)
ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/i386/)
ifeq ($(ARCH),i386)
	ARCH := X86
endif
ifeq ($(ARCH),x86_64)
	ARCH := X86
endif

CFLAGS += -D_FILE_OFFSET_BITS=64
CFLAGS += -I../../../../arch/x86/include/generated/uapi/
CFLAGS += -I../../../../arch/x86/include/uapi/
CFLAGS += -I../../../../include/uapi/
CFLAGS += -I../../../../include/

all:
ifeq ($(ARCH),X86)
	gcc $(CFLAGS) memfd_test.c -o memfd_test
else
	echo "Not an x86 target, can't build memfd selftest"
endif

run_tests: all
ifeq ($(ARCH),X86)
	gcc $(CFLAGS) memfd_test.c -o memfd_test
endif
	@./memfd_test || echo "memfd_test: [FAIL]"

build_fuse:
ifeq ($(ARCH),X86)
	gcc $(CFLAGS) fuse_mnt.c `pkg-config fuse --cflags --libs` -o fuse_mnt
	gcc $(CFLAGS) fuse_test.c -o fuse_test
else
	echo "Not an x86 target, can't build memfd selftest"
endif

run_fuse: build_fuse
	@./run_fuse_test.sh || echo "fuse_test: [FAIL]"

clean:
	$(RM) memfd_test fuse_test
