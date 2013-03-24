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

# Treat warnings as errors unless directed not to
ifneq ($(WERROR),0)
	CFLAGS_WERROR := -Werror
endif

ifeq ("$(origin DEBUG)", "command line")
	PERF_DEBUG = $(DEBUG)
endif
ifndef PERF_DEBUG
	CFLAGS_OPTIMIZE = -O6
endif

ifdef PARSER_DEBUG
	PARSER_DEBUG_BISON  := -t
	PARSER_DEBUG_FLEX   := -d
	PARSER_DEBUG_CFLAGS := -DPARSER_DEBUG
endif

CFLAGS = -fno-omit-frame-pointer -ggdb3 -funwind-tables -Wall -Wextra -std=gnu99 $(CFLAGS_WERROR) $(CFLAGS_OPTIMIZE) $(EXTRA_WARNINGS) $(EXTRA_CFLAGS) $(PARSER_DEBUG_CFLAGS)
EXTLIBS = -lpthread -lrt -lelf -lm
ALL_CFLAGS = $(CFLAGS) -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
ALL_LDFLAGS = $(LDFLAGS)

ifeq ($(call try-cc,$(SOURCE_HELLO),$(CFLAGS) -Werror -fstack-protector-all,-fstack-protector-all),y)
	CFLAGS := $(CFLAGS) -fstack-protector-all
endif

ifeq ($(call try-cc,$(SOURCE_HELLO),$(CFLAGS) -Werror -Wstack-protector,-Wstack-protector),y)
	CFLAGS := $(CFLAGS) -Wstack-protector
endif

ifeq ($(call try-cc,$(SOURCE_HELLO),$(CFLAGS) -Werror -Wvolatile-register-var,-Wvolatile-register-var),y)
	CFLAGS := $(CFLAGS) -Wvolatile-register-var
endif

ifndef PERF_DEBUG
	ifeq ($(call try-cc,$(SOURCE_HELLO),$(CFLAGS) -D_FORTIFY_SOURCE=2,-D_FORTIFY_SOURCE=2),y)
		CFLAGS := $(CFLAGS) -D_FORTIFY_SOURCE=2
	endif
endif

BASIC_CFLAGS += \
	-Iutil/include \
	-Iarch/$(ARCH)/include \
	$(if $(objtree),-I$(objtree)/arch/$(ARCH)/include/generated/uapi) \
	-I$(srctree)/arch/$(ARCH)/include/uapi \
	-I$(srctree)/arch/$(ARCH)/include \
	$(if $(objtree),-I$(objtree)/include/generated/uapi) \
	-I$(srctree)/include/uapi \
	-I$(srctree)/include \
	-I$(OUTPUT)util \
	-Iutil \
	-I. \
	-I$(TRACE_EVENT_DIR) \
	-I../lib/ \
	-D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE

BASIC_LDFLAGS =

ifeq ($(call try-cc,$(SOURCE_BIONIC),$(CFLAGS),bionic),y)
	BIONIC := 1
	EXTLIBS := $(filter-out -lrt,$(EXTLIBS))
	EXTLIBS := $(filter-out -lpthread,$(EXTLIBS))
	BASIC_CFLAGS += -I.
endif
