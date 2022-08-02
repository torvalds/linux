# This mimics the top-level Makefile. We do it explicitly here so that this
# Makefile can operate with or without the kbuild infrastructure.
ifneq ($(LLVM),)
ifneq ($(filter %/,$(LLVM)),)
LLVM_PREFIX := $(LLVM)
else ifneq ($(filter -%,$(LLVM)),)
LLVM_SUFFIX := $(LLVM)
endif

CLANG_TARGET_FLAGS_arm          := arm-linux-gnueabi
CLANG_TARGET_FLAGS_arm64        := aarch64-linux-gnu
CLANG_TARGET_FLAGS_hexagon      := hexagon-linux-musl
CLANG_TARGET_FLAGS_m68k         := m68k-linux-gnu
CLANG_TARGET_FLAGS_mips         := mipsel-linux-gnu
CLANG_TARGET_FLAGS_powerpc      := powerpc64le-linux-gnu
CLANG_TARGET_FLAGS_riscv        := riscv64-linux-gnu
CLANG_TARGET_FLAGS_s390         := s390x-linux-gnu
CLANG_TARGET_FLAGS_x86          := x86_64-linux-gnu
CLANG_TARGET_FLAGS              := $(CLANG_TARGET_FLAGS_$(ARCH))

ifeq ($(CROSS_COMPILE),)
ifeq ($(CLANG_TARGET_FLAGS),)
$(error Specify CROSS_COMPILE or add '--target=' option to lib.mk
else
CLANG_FLAGS     += --target=$(CLANG_TARGET_FLAGS)
endif # CLANG_TARGET_FLAGS
else
CLANG_FLAGS     += --target=$(notdir $(CROSS_COMPILE:%-=%))
endif # CROSS_COMPILE

CC := $(LLVM_PREFIX)clang$(LLVM_SUFFIX) $(CLANG_FLAGS) -fintegrated-as
else
CC := $(CROSS_COMPILE)gcc
endif # LLVM

ifeq (0,$(MAKELEVEL))
    ifeq ($(OUTPUT),)
	OUTPUT := $(shell pwd)
	DEFAULT_INSTALL_HDR_PATH := 1
    endif
endif
selfdir = $(realpath $(dir $(filter %/lib.mk,$(MAKEFILE_LIST))))

# The following are built by lib.mk common compile rules.
# TEST_CUSTOM_PROGS should be used by tests that require
# custom build rule and prevent common build rule use.
# TEST_PROGS are for test shell scripts.
# TEST_CUSTOM_PROGS and TEST_PROGS will be run by common run_tests
# and install targets. Common clean doesn't touch them.
TEST_GEN_PROGS := $(patsubst %,$(OUTPUT)/%,$(TEST_GEN_PROGS))
TEST_GEN_PROGS_EXTENDED := $(patsubst %,$(OUTPUT)/%,$(TEST_GEN_PROGS_EXTENDED))
TEST_GEN_FILES := $(patsubst %,$(OUTPUT)/%,$(TEST_GEN_FILES))

ifdef KSFT_KHDR_INSTALL
top_srcdir ?= ../../../..
include $(top_srcdir)/scripts/subarch.include
ARCH		?= $(SUBARCH)

# set default goal to all, so make without a target runs all, even when
# all isn't the first target in the file.
.DEFAULT_GOAL := all

# Invoke headers install with --no-builtin-rules to avoid circular
# dependency in "make kselftest" case. In this case, second level
# make inherits builtin-rules which will use the rule generate
# Makefile.o and runs into
# "Circular Makefile.o <- prepare dependency dropped."
# and headers_install fails and test compile fails.
# O= KBUILD_OUTPUT cases don't run into this error, since main Makefile
# invokes them as sub-makes and --no-builtin-rules is not necessary,
# but doesn't cause any failures. Keep it simple and use the same
# flags in both cases.
# Note that the support to install headers from lib.mk is necessary
# when test Makefile is run directly with "make -C".
# When local build is done, headers are installed in the default
# INSTALL_HDR_PATH usr/include.
.PHONY: khdr
.NOTPARALLEL:
khdr:
ifndef KSFT_KHDR_INSTALL_DONE
ifeq (1,$(DEFAULT_INSTALL_HDR_PATH))
	$(MAKE) --no-builtin-rules ARCH=$(ARCH) -C $(top_srcdir) headers_install
else
	$(MAKE) --no-builtin-rules INSTALL_HDR_PATH=$$OUTPUT/usr \
		ARCH=$(ARCH) -C $(top_srcdir) headers_install
endif
endif

all: khdr $(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED) $(TEST_GEN_FILES)
else
all: $(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED) $(TEST_GEN_FILES)
endif

define RUN_TESTS
	BASE_DIR="$(selfdir)";			\
	. $(selfdir)/kselftest/runner.sh;	\
	if [ "X$(summary)" != "X" ]; then       \
		per_test_logging=1;		\
	fi;                                     \
	run_many $(1)
endef

run_tests: all
ifdef building_out_of_srctree
	@if [ "X$(TEST_PROGS)$(TEST_PROGS_EXTENDED)$(TEST_FILES)" != "X" ]; then \
		rsync -aq $(TEST_PROGS) $(TEST_PROGS_EXTENDED) $(TEST_FILES) $(OUTPUT); \
	fi
	@if [ "X$(TEST_PROGS)" != "X" ]; then \
		$(call RUN_TESTS, $(TEST_GEN_PROGS) $(TEST_CUSTOM_PROGS) \
				  $(addprefix $(OUTPUT)/,$(TEST_PROGS))) ; \
	else \
		$(call RUN_TESTS, $(TEST_GEN_PROGS) $(TEST_CUSTOM_PROGS)); \
	fi
else
	@$(call RUN_TESTS, $(TEST_GEN_PROGS) $(TEST_CUSTOM_PROGS) $(TEST_PROGS))
endif

define INSTALL_SINGLE_RULE
	$(if $(INSTALL_LIST),@mkdir -p $(INSTALL_PATH))
	$(if $(INSTALL_LIST),rsync -a $(INSTALL_LIST) $(INSTALL_PATH)/)
endef

define INSTALL_RULE
	$(eval INSTALL_LIST = $(TEST_PROGS)) $(INSTALL_SINGLE_RULE)
	$(eval INSTALL_LIST = $(TEST_PROGS_EXTENDED)) $(INSTALL_SINGLE_RULE)
	$(eval INSTALL_LIST = $(TEST_FILES)) $(INSTALL_SINGLE_RULE)
	$(eval INSTALL_LIST = $(TEST_GEN_PROGS)) $(INSTALL_SINGLE_RULE)
	$(eval INSTALL_LIST = $(TEST_CUSTOM_PROGS)) $(INSTALL_SINGLE_RULE)
	$(eval INSTALL_LIST = $(TEST_GEN_PROGS_EXTENDED)) $(INSTALL_SINGLE_RULE)
	$(eval INSTALL_LIST = $(TEST_GEN_FILES)) $(INSTALL_SINGLE_RULE)
	$(eval INSTALL_LIST = $(wildcard config settings)) $(INSTALL_SINGLE_RULE)
endef

install: all
ifdef INSTALL_PATH
	$(INSTALL_RULE)
else
	$(error Error: set INSTALL_PATH to use install)
endif

emit_tests:
	for TEST in $(TEST_GEN_PROGS) $(TEST_CUSTOM_PROGS) $(TEST_PROGS); do \
		BASENAME_TEST=`basename $$TEST`;	\
		echo "$(COLLECTION):$$BASENAME_TEST";	\
	done

# define if isn't already. It is undefined in make O= case.
ifeq ($(RM),)
RM := rm -f
endif

define CLEAN
	$(RM) -r $(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED) $(TEST_GEN_FILES) $(EXTRA_CLEAN)
endef

clean:
	$(CLEAN)

# When make O= with kselftest target from main level
# the following aren't defined.
#
ifdef building_out_of_srctree
LINK.c = $(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(TARGET_ARCH)
COMPILE.S = $(CC) $(ASFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c
LINK.S = $(CC) $(ASFLAGS) $(CPPFLAGS) $(LDFLAGS) $(TARGET_ARCH)
endif

# Selftest makefiles can override those targets by setting
# OVERRIDE_TARGETS = 1.
ifeq ($(OVERRIDE_TARGETS),)
LOCAL_HDRS += $(selfdir)/kselftest_harness.h $(selfdir)/kselftest.h
$(OUTPUT)/%:%.c $(LOCAL_HDRS)
	$(LINK.c) $(filter-out $(LOCAL_HDRS),$^) $(LDLIBS) -o $@

$(OUTPUT)/%.o:%.S
	$(COMPILE.S) $^ -o $@

$(OUTPUT)/%:%.S
	$(LINK.S) $^ $(LDLIBS) -o $@
endif

.PHONY: run_tests all clean install emit_tests
