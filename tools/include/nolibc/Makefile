# SPDX-License-Identifier: GPL-2.0
# Makefile for nolibc installation and tests
include ../../scripts/Makefile.include

# we're in ".../tools/include/nolibc"
ifeq ($(srctree),)
srctree := $(patsubst %/tools/include/,%,$(dir $(CURDIR)))
endif

nolibc_arch := $(patsubst arm64,aarch64,$(ARCH))
arch_file := arch-$(nolibc_arch).h
all_files := ctype.h errno.h nolibc.h signal.h std.h stdio.h stdlib.h string.h \
             sys.h time.h types.h unistd.h

# install all headers needed to support a bare-metal compiler
all:

# Note: when ARCH is "x86" we concatenate both x86_64 and i386
headers:
	$(Q)mkdir -p $(OUTPUT)sysroot
	$(Q)mkdir -p $(OUTPUT)sysroot/include
	$(Q)cp $(all_files) $(OUTPUT)sysroot/include/
	$(Q)if [ "$(ARCH)" = "x86" ]; then      \
		sed -e                          \
		  's,^#ifndef _NOLIBC_ARCH_X86_64_H,#if !defined(_NOLIBC_ARCH_X86_64_H) \&\& defined(__x86_64__),' \
		  arch-x86_64.h;                \
		sed -e                          \
		  's,^#ifndef _NOLIBC_ARCH_I386_H,#if !defined(_NOLIBC_ARCH_I386_H) \&\& !defined(__x86_64__),' \
		  arch-i386.h;                  \
	elif [ -e "$(arch_file)" ]; then        \
		cat $(arch_file);               \
	else                                    \
		echo "Fatal: architecture $(ARCH) not yet supported by nolibc." >&2; \
		exit 1;                         \
	fi > $(OUTPUT)sysroot/include/arch.h

headers_standalone: headers
	$(Q)$(MAKE) -C $(srctree) headers
	$(Q)$(MAKE) -C $(srctree) headers_install INSTALL_HDR_PATH=$(OUTPUT)/sysroot

clean:
	$(call QUIET_CLEAN, nolibc) rm -rf "$(OUTPUT)sysroot"
