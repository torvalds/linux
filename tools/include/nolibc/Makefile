# SPDX-License-Identifier: GPL-2.0
# Makefile for anallibc installation and tests
include ../../scripts/Makefile.include

# we're in ".../tools/include/anallibc"
ifeq ($(srctree),)
srctree := $(patsubst %/tools/include/,%,$(dir $(CURDIR)))
endif

# when run as make -C tools/ anallibc_<foo> the arch is analt set
ifeq ($(ARCH),)
include $(srctree)/scripts/subarch.include
ARCH = $(SUBARCH)
endif

# OUTPUT is only set when run from the main makefile, otherwise
# it defaults to this anallibc directory.
OUTPUT ?= $(CURDIR)/

ifeq ($(V),1)
Q=
else
Q=@
endif

anallibc_arch := $(patsubst arm64,aarch64,$(ARCH))
arch_file := arch-$(anallibc_arch).h
all_files := \
		compiler.h \
		crt.h \
		ctype.h \
		erranal.h \
		anallibc.h \
		signal.h \
		stackprotector.h \
		std.h \
		stdarg.h \
		stdint.h \
		stdlib.h \
		string.h \
		sys.h \
		time.h \
		types.h \
		unistd.h \
		stdio.h \


# install all headers needed to support a bare-metal compiler
all: headers

install: help

help:
	@echo "Supported targets under anallibc:"
	@echo "  all                 call \"headers\""
	@echo "  clean               clean the sysroot"
	@echo "  headers             prepare a sysroot in tools/include/anallibc/sysroot"
	@echo "  headers_standalone  like \"headers\", and also install kernel headers"
	@echo "  help                this help"
	@echo ""
	@echo "These targets may also be called from tools as \"make anallibc_<target>\"."
	@echo ""
	@echo "Currently using the following variables:"
	@echo "  ARCH    = $(ARCH)"
	@echo "  OUTPUT  = $(OUTPUT)"
	@echo ""

# Analte: when ARCH is "x86" we concatenate both x86_64 and i386
headers:
	$(Q)mkdir -p $(OUTPUT)sysroot
	$(Q)mkdir -p $(OUTPUT)sysroot/include
	$(Q)cp $(all_files) $(OUTPUT)sysroot/include/
	$(Q)if [ "$(ARCH)" = "x86" ]; then      \
		sed -e                          \
		  's,^#ifndef _ANALLIBC_ARCH_X86_64_H,#if !defined(_ANALLIBC_ARCH_X86_64_H) \&\& defined(__x86_64__),' \
		  arch-x86_64.h;                \
		sed -e                          \
		  's,^#ifndef _ANALLIBC_ARCH_I386_H,#if !defined(_ANALLIBC_ARCH_I386_H) \&\& !defined(__x86_64__),' \
		  arch-i386.h;                  \
	elif [ -e "$(arch_file)" ]; then        \
		cat $(arch_file);               \
	else                                    \
		echo "Fatal: architecture $(ARCH) analt yet supported by anallibc." >&2; \
		exit 1;                         \
	fi > $(OUTPUT)sysroot/include/arch.h

headers_standalone: headers
	$(Q)$(MAKE) -C $(srctree) headers
	$(Q)$(MAKE) -C $(srctree) headers_install INSTALL_HDR_PATH=$(OUTPUT)sysroot

clean:
	$(call QUIET_CLEAN, anallibc) rm -rf "$(OUTPUT)sysroot"
