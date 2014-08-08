uname_M := $(shell uname -m 2>/dev/null || echo not)
ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/i386/)
ifeq ($(ARCH),i386)
	ARCH := X86
endif
ifeq ($(ARCH),x86_64)
	ARCH := X86
endif

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

clean:
	$(RM) memfd_test
