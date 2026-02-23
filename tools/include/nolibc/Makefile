# SPDX-License-Identifier: GPL-2.0
# Makefile for nolibc installation and tests
include ../../scripts/Makefile.include

# we're in ".../tools/include/nolibc"
ifeq ($(srctree),)
srctree := $(patsubst %/tools/include/,%,$(dir $(CURDIR)))
endif

# when run as make -C tools/ nolibc_<foo> the arch is not set
ifeq ($(ARCH),)
include $(srctree)/scripts/subarch.include
ARCH = $(SUBARCH)
endif

# OUTPUT is only set when run from the main makefile, otherwise
# it defaults to this nolibc directory.
OUTPUT ?= $(CURDIR)/

ifeq ($(V),1)
Q=
else
Q=@
endif

arch_files := arch.h $(wildcard arch-*.h)
all_files := \
		compiler.h \
		crt.h \
		ctype.h \
		dirent.h \
		elf.h \
		errno.h \
		fcntl.h \
		getopt.h \
		inttypes.h \
		limits.h \
		math.h \
		nolibc.h \
		poll.h \
		sched.h \
		signal.h \
		stackprotector.h \
		std.h \
		stdarg.h \
		stdbool.h \
		stddef.h \
		stdint.h \
		stdlib.h \
		string.h \
		sys.h \
		sys/auxv.h \
		sys/ioctl.h \
		sys/mman.h \
		sys/mount.h \
		sys/prctl.h \
		sys/ptrace.h \
		sys/random.h \
		sys/reboot.h \
		sys/resource.h \
		sys/select.h \
		sys/stat.h \
		sys/syscall.h \
		sys/sysmacros.h \
		sys/time.h \
		sys/timerfd.h \
		sys/types.h \
		sys/uio.h \
		sys/utsname.h \
		sys/wait.h \
		time.h \
		types.h \
		unistd.h \
		stdio.h \


# install all headers needed to support a bare-metal compiler
all: headers

install: help

help:
	@echo "Supported targets under nolibc:"
	@echo "  all                 call \"headers\""
	@echo "  clean               clean the sysroot"
	@echo "  headers             prepare a multi-arch sysroot in \$${OUTPUT}sysroot"
	@echo "  headers_standalone  like \"headers\", and also install kernel headers"
	@echo "  help                this help"
	@echo ""
	@echo "These targets may also be called from tools as \"make nolibc_<target>\"."
	@echo ""
	@echo "Currently using the following variables:"
	@echo "  ARCH    = $(ARCH)"
	@echo "  OUTPUT  = $(OUTPUT)"
	@echo ""

# installs headers for all archs at once.
headers:
	$(Q)mkdir -p "$(OUTPUT)sysroot"
	$(Q)mkdir -p "$(OUTPUT)sysroot/include"
	$(Q)cp --parents $(arch_files) $(all_files) "$(OUTPUT)sysroot/include/"

headers_standalone: headers
	$(Q)$(MAKE) -C $(srctree) headers
	$(Q)$(MAKE) -C $(srctree) headers_install INSTALL_HDR_PATH=$(OUTPUT)sysroot

CFLAGS_s390 := -m64
CFLAGS := $(CFLAGS_$(ARCH))

headers_check: headers_standalone
	$(Q)for header in $(filter-out crt.h std.h,$(all_files)); do \
		$(CC) $(CFLAGS) $(CLANG_CROSS_FLAGS) -Wall -Werror -nostdinc -fsyntax-only -x c /dev/null \
			-I$(or $(objtree),$(srctree))/usr/include -include $$header -include $$header || exit 1; \
	done

clean:
	$(call QUIET_CLEAN, nolibc) rm -rf "$(OUTPUT)sysroot"
