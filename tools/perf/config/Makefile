uname_M := $(shell uname -m 2>/dev/null || echo not)

ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/i386/ -e s/sun4u/sparc64/ \
				  -e s/arm.*/arm/ -e s/sa110/arm/ \
				  -e s/s390x/s390/ -e s/parisc64/parisc/ \
				  -e s/ppc.*/powerpc/ -e s/mips.*/mips/ \
				  -e s/sh[234].*/sh/ -e s/aarch64.*/arm64/ )
NO_PERF_REGS := 1

# Additional ARCH settings for x86
ifeq ($(ARCH),i386)
	override ARCH := x86
	NO_PERF_REGS := 0
	LIBUNWIND_LIBS = -lunwind -lunwind-x86
endif

ifeq ($(ARCH),x86_64)
	override ARCH := x86
	IS_X86_64 := 0
	ifeq (, $(findstring m32,$(EXTRA_CFLAGS)))
		IS_X86_64 := $(shell echo __x86_64__ | ${CC} -E -x c - | tail -n 1)
	endif
	ifeq (${IS_X86_64}, 1)
		RAW_ARCH := x86_64
		ARCH_CFLAGS := -DARCH_X86_64
		ARCH_INCLUDE = ../../arch/x86/lib/memcpy_64.S ../../arch/x86/lib/memset_64.S
	endif
	NO_PERF_REGS := 0
	LIBUNWIND_LIBS = -lunwind -lunwind-x86_64
endif

ifeq ($(NO_PERF_REGS),0)
        BASIC_CFLAGS += -DHAVE_PERF_REGS
endif

-include config/feature-tests.mak

ifeq ($(call get-executable,$(FLEX)),)
	dummy := $(error Error: $(FLEX) is missing on this system, please install it)
endif

ifeq ($(call get-executable,$(BISON)),)
	dummy := $(error Error: $(BISON) is missing on this system, please install it)
endif
