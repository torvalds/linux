# SPDX-License-Identifier: GPL-2.0

# When ARCH not overridden for crosscompiling, lookup machine
ARCH ?= $(shell uname -m 2>/dev/null || echo not)

ifneq (,$(filter $(ARCH),aarch64 arm64))
ARM64_SUBTARGETS ?= tags signal pauth fp
else
ARM64_SUBTARGETS :=
endif

CFLAGS := -Wall -O2 -g

# A proper top_srcdir is needed by KSFT(lib.mk)
top_srcdir = $(realpath ../../../../)

# Additional include paths needed by kselftest.h and local headers
CFLAGS += -I$(top_srcdir)/tools/testing/selftests/

# Guessing where the Kernel headers could have been installed
# depending on ENV config
ifeq ($(KBUILD_OUTPUT),)
khdr_dir = $(top_srcdir)/usr/include
else
# the KSFT preferred location when KBUILD_OUTPUT is set
khdr_dir = $(KBUILD_OUTPUT)/kselftest/usr/include
endif

CFLAGS += -I$(khdr_dir)

export CFLAGS
export top_srcdir

all:
	@for DIR in $(ARM64_SUBTARGETS); do				\
		BUILD_TARGET=$(OUTPUT)/$$DIR;			\
		mkdir -p $$BUILD_TARGET;			\
		make OUTPUT=$$BUILD_TARGET -C $$DIR $@;		\
	done

install: all
	@for DIR in $(ARM64_SUBTARGETS); do				\
		BUILD_TARGET=$(OUTPUT)/$$DIR;			\
		make OUTPUT=$$BUILD_TARGET -C $$DIR $@;		\
	done

run_tests: all
	@for DIR in $(ARM64_SUBTARGETS); do				\
		BUILD_TARGET=$(OUTPUT)/$$DIR;			\
		make OUTPUT=$$BUILD_TARGET -C $$DIR $@;		\
	done

# Avoid any output on non arm64 on emit_tests
emit_tests: all
	@for DIR in $(ARM64_SUBTARGETS); do				\
		BUILD_TARGET=$(OUTPUT)/$$DIR;			\
		make OUTPUT=$$BUILD_TARGET -C $$DIR $@;		\
	done

clean:
	@for DIR in $(ARM64_SUBTARGETS); do				\
		BUILD_TARGET=$(OUTPUT)/$$DIR;			\
		make OUTPUT=$$BUILD_TARGET -C $$DIR $@;		\
	done

.PHONY: all clean install run_tests emit_tests
