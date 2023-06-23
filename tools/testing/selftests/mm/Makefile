# SPDX-License-Identifier: GPL-2.0
# Makefile for mm selftests

LOCAL_HDRS += $(selfdir)/mm/local_config.h $(top_srcdir)/mm/gup_test.h

include local_config.mk

ifeq ($(ARCH),)

ifeq ($(CROSS_COMPILE),)
uname_M := $(shell uname -m 2>/dev/null || echo not)
else
uname_M := $(shell echo $(CROSS_COMPILE) | grep -o '^[a-z0-9]\+')
endif
ARCH ?= $(shell echo $(uname_M) | sed -e 's/aarch64.*/arm64/' -e 's/ppc64.*/ppc64/')
endif

# Without this, failed build products remain, with up-to-date timestamps,
# thus tricking Make (and you!) into believing that All Is Well, in subsequent
# make invocations:
.DELETE_ON_ERROR:

# Avoid accidental wrong builds, due to built-in rules working just a little
# bit too well--but not quite as well as required for our situation here.
#
# In other words, "make $SOME_TEST" is supposed to fail to build at all,
# because this Makefile only supports either "make" (all), or "make /full/path".
# However,  the built-in rules, if not suppressed, will pick up CFLAGS and the
# initial LDLIBS (but not the target-specific LDLIBS, because those are only
# set for the full path target!). This causes it to get pretty far into building
# things despite using incorrect values such as an *occasionally* incomplete
# LDLIBS.
MAKEFLAGS += --no-builtin-rules

CFLAGS = -Wall -I $(top_srcdir) $(EXTRA_CFLAGS) $(KHDR_INCLUDES)
LDLIBS = -lrt -lpthread

TEST_GEN_PROGS = cow
TEST_GEN_PROGS += compaction_test
TEST_GEN_PROGS += gup_longterm
TEST_GEN_PROGS += gup_test
TEST_GEN_PROGS += hmm-tests
TEST_GEN_PROGS += hugetlb-madvise
TEST_GEN_PROGS += hugepage-mmap
TEST_GEN_PROGS += hugepage-mremap
TEST_GEN_PROGS += hugepage-shm
TEST_GEN_PROGS += hugepage-vmemmap
TEST_GEN_PROGS += khugepaged
TEST_GEN_PROGS += madv_populate
TEST_GEN_PROGS += map_fixed_noreplace
TEST_GEN_PROGS += map_hugetlb
TEST_GEN_PROGS += map_populate
TEST_GEN_PROGS += memfd_secret
TEST_GEN_PROGS += migration
TEST_GEN_PROGS += mkdirty
TEST_GEN_PROGS += mlock-random-test
TEST_GEN_PROGS += mlock2-tests
TEST_GEN_PROGS += mrelease_test
TEST_GEN_PROGS += mremap_dontunmap
TEST_GEN_PROGS += mremap_test
TEST_GEN_PROGS += on-fault-limit
TEST_GEN_PROGS += thuge-gen
TEST_GEN_PROGS += transhuge-stress
TEST_GEN_PROGS += uffd-stress
TEST_GEN_PROGS += uffd-unit-tests
TEST_GEN_PROGS += soft-dirty
TEST_GEN_PROGS += split_huge_page_test
TEST_GEN_PROGS += ksm_tests
TEST_GEN_PROGS += ksm_functional_tests
TEST_GEN_PROGS += mdwe_test

ifeq ($(ARCH),x86_64)
CAN_BUILD_I386 := $(shell ./../x86/check_cc.sh "$(CC)" ../x86/trivial_32bit_program.c -m32)
CAN_BUILD_X86_64 := $(shell ./../x86/check_cc.sh "$(CC)" ../x86/trivial_64bit_program.c)
CAN_BUILD_WITH_NOPIE := $(shell ./../x86/check_cc.sh "$(CC)" ../x86/trivial_program.c -no-pie)

VMTARGETS := protection_keys
BINARIES_32 := $(VMTARGETS:%=%_32)
BINARIES_64 := $(VMTARGETS:%=%_64)

ifeq ($(CAN_BUILD_WITH_NOPIE),1)
CFLAGS += -no-pie
endif

ifeq ($(CAN_BUILD_I386),1)
TEST_GEN_PROGS += $(BINARIES_32)
endif

ifeq ($(CAN_BUILD_X86_64),1)
TEST_GEN_PROGS += $(BINARIES_64)
endif
else

ifneq (,$(findstring $(ARCH),ppc64))
TEST_GEN_PROGS += protection_keys
endif

endif

ifneq (,$(filter $(ARCH),arm64 ia64 mips64 parisc64 ppc64 riscv64 s390x sparc64 x86_64))
TEST_GEN_PROGS += va_high_addr_switch
TEST_GEN_PROGS += virtual_address_range
TEST_GEN_PROGS += write_to_hugetlbfs
endif

TEST_PROGS := run_vmtests.sh

TEST_FILES := test_vmalloc.sh
TEST_FILES += test_hmm.sh
TEST_FILES += va_high_addr_switch.sh

include ../lib.mk

$(TEST_GEN_PROGS): vm_util.c

$(OUTPUT)/uffd-stress: uffd-common.c
$(OUTPUT)/uffd-unit-tests: uffd-common.c

ifeq ($(ARCH),x86_64)
BINARIES_32 := $(patsubst %,$(OUTPUT)/%,$(BINARIES_32))
BINARIES_64 := $(patsubst %,$(OUTPUT)/%,$(BINARIES_64))

define gen-target-rule-32
$(1) $(1)_32: $(OUTPUT)/$(1)_32
.PHONY: $(1) $(1)_32
endef

define gen-target-rule-64
$(1) $(1)_64: $(OUTPUT)/$(1)_64
.PHONY: $(1) $(1)_64
endef

ifeq ($(CAN_BUILD_I386),1)
$(BINARIES_32): CFLAGS += -m32 -mxsave
$(BINARIES_32): LDLIBS += -lrt -ldl -lm
$(BINARIES_32): $(OUTPUT)/%_32: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(notdir $^) $(LDLIBS) -o $@
$(foreach t,$(VMTARGETS),$(eval $(call gen-target-rule-32,$(t))))
endif

ifeq ($(CAN_BUILD_X86_64),1)
$(BINARIES_64): CFLAGS += -m64 -mxsave
$(BINARIES_64): LDLIBS += -lrt -ldl
$(BINARIES_64): $(OUTPUT)/%_64: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(notdir $^) $(LDLIBS) -o $@
$(foreach t,$(VMTARGETS),$(eval $(call gen-target-rule-64,$(t))))
endif

# x86_64 users should be encouraged to install 32-bit libraries
ifeq ($(CAN_BUILD_I386)$(CAN_BUILD_X86_64),01)
all: warn_32bit_failure

warn_32bit_failure:
	@echo "Warning: you seem to have a broken 32-bit build" 2>&1;		\
	echo  "environment. This will reduce test coverage of 64-bit" 2>&1;	\
	echo  "kernels. If you are using a Debian-like distribution," 2>&1;	\
	echo  "try:"; 2>&1;							\
	echo  "";								\
	echo  "  apt-get install gcc-multilib libc6-i386 libc6-dev-i386";	\
	echo  "";								\
	echo  "If you are using a Fedora-like distribution, try:";		\
	echo  "";								\
	echo  "  yum install glibc-devel.*i686";				\
	exit 0;
endif
endif

# IOURING_EXTRA_LIBS may get set in local_config.mk, or it may be left empty.
$(OUTPUT)/cow: LDLIBS += $(IOURING_EXTRA_LIBS)

$(OUTPUT)/gup_longterm: LDLIBS += $(IOURING_EXTRA_LIBS)

$(OUTPUT)/mlock-random-test $(OUTPUT)/memfd_secret: LDLIBS += -lcap

$(OUTPUT)/ksm_tests: LDLIBS += -lnuma

$(OUTPUT)/migration: LDLIBS += -lnuma

local_config.mk local_config.h: check_config.sh
	/bin/sh ./check_config.sh $(CC)

EXTRA_CLEAN += local_config.mk local_config.h

ifeq ($(IOURING_EXTRA_LIBS),)
all: warn_missing_liburing

warn_missing_liburing:
	@echo ; \
	echo "Warning: missing liburing support. Some tests will be skipped." ; \
	echo
endif
