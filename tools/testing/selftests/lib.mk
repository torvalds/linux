# This mimics the top-level Makefile. We do it explicitly here so that this
# Makefile can operate with or without the kbuild infrastructure.
ifneq ($(LLVM),)
ifneq ($(filter %/,$(LLVM)),)
LLVM_PREFIX := $(LLVM)
else ifneq ($(filter -%,$(LLVM)),)
LLVM_SUFFIX := $(LLVM)
endif

CLANG := $(LLVM_PREFIX)clang$(LLVM_SUFFIX)

CLANG_TARGET_FLAGS_arm          := arm-linux-gnueabi
CLANG_TARGET_FLAGS_arm64        := aarch64-linux-gnu
CLANG_TARGET_FLAGS_hexagon      := hexagon-linux-musl
CLANG_TARGET_FLAGS_i386         := i386-linux-gnu
CLANG_TARGET_FLAGS_m68k         := m68k-linux-gnu
CLANG_TARGET_FLAGS_mips         := mipsel-linux-gnu
CLANG_TARGET_FLAGS_powerpc      := powerpc64le-linux-gnu
CLANG_TARGET_FLAGS_riscv        := riscv64-linux-gnu
CLANG_TARGET_FLAGS_s390         := s390x-linux-gnu
CLANG_TARGET_FLAGS_x86          := x86_64-linux-gnu
CLANG_TARGET_FLAGS_x86_64       := x86_64-linux-gnu

# Default to host architecture if ARCH is not explicitly given.
ifeq ($(ARCH),)
CLANG_TARGET_FLAGS := $(shell $(CLANG) -print-target-triple)
else
CLANG_TARGET_FLAGS := $(CLANG_TARGET_FLAGS_$(ARCH))
endif

ifeq ($(CROSS_COMPILE),)
ifeq ($(CLANG_TARGET_FLAGS),)
$(error Specify CROSS_COMPILE or add '--target=' option to lib.mk)
else
CLANG_FLAGS     += --target=$(CLANG_TARGET_FLAGS)
endif # CLANG_TARGET_FLAGS
else
CLANG_FLAGS     += --target=$(notdir $(CROSS_COMPILE:%-=%))
endif # CROSS_COMPILE

# gcc defaults to silence (off) for the following warnings, but clang defaults
# to the opposite. The warnings are not useful for the kernel itself, which is
# why they have remained disabled in gcc for the main kernel build. And it is
# only due to including kernel data structures in the selftests, that we get the
# warnings from clang. Therefore, disable the warnings for clang builds.
CFLAGS += -Wno-address-of-packed-member
CFLAGS += -Wno-gnu-variable-sized-type-not-at-end

CC := $(CLANG) $(CLANG_FLAGS) -fintegrated-as
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
top_srcdir = $(selfdir)/../../..

# msg: emit succinct information message describing current building step
# $1 - generic step name (e.g., CC, LINK, etc);
# $2 - optional "flavor" specifier; if provided, will be emitted as [flavor];
# $3 - target (assumed to be file); only file name will be emitted;
# $4 - optional extra arg, emitted as-is, if provided.
ifeq ($(V),1)
Q =
msg =
else
Q = @
msg = @printf '  %-8s%s %s%s\n' "$(1)" "$(if $(2), [$(2)])" "$(notdir $(3))" "$(if $(4), $(4))";
MAKEFLAGS += --no-print-directory
endif

ifeq ($(KHDR_INCLUDES),)
KHDR_INCLUDES := -isystem $(top_srcdir)/usr/include
endif

# In order to use newer items that haven't yet been added to the user's system
# header files, add $(TOOLS_INCLUDES) to the compiler invocation in each
# each selftest.
# You may need to add files to that location, or to refresh an existing file. In
# order to do that, run "make headers" from $(top_srcdir), then copy the
# header file that you want from $(top_srcdir)/usr/include/... , to the matching
# subdir in $(TOOLS_INCLUDE).
TOOLS_INCLUDES := -isystem $(top_srcdir)/tools/include/uapi

# The following are built by lib.mk common compile rules.
# TEST_CUSTOM_PROGS should be used by tests that require
# custom build rule and prevent common build rule use.
# TEST_PROGS are for test shell scripts.
# TEST_CUSTOM_PROGS and TEST_PROGS will be run by common run_tests
# and install targets. Common clean doesn't touch them.
TEST_GEN_PROGS := $(patsubst %,$(OUTPUT)/%,$(TEST_GEN_PROGS))
TEST_GEN_PROGS_EXTENDED := $(patsubst %,$(OUTPUT)/%,$(TEST_GEN_PROGS_EXTENDED))
TEST_GEN_FILES := $(patsubst %,$(OUTPUT)/%,$(TEST_GEN_FILES))

all: $(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED) $(TEST_GEN_FILES) \
	$(if $(TEST_GEN_MODS_DIR),gen_mods_dir)

define RUN_TESTS
	BASE_DIR="$(selfdir)";			\
	. $(selfdir)/kselftest/runner.sh;	\
	if [ "X$(summary)" != "X" ]; then       \
		per_test_logging=1;		\
	fi;                                     \
	run_many $(1)
endef

define INSTALL_INCLUDES
	$(if $(TEST_INCLUDES), \
		relative_files=""; \
		for entry in $(TEST_INCLUDES); do \
			entry_dir=$$(readlink -e "$$(dirname "$$entry")"); \
			entry_name=$$(basename "$$entry"); \
			relative_dir=$${entry_dir#"$$SRC_PATH"/}; \
			if [ "$$relative_dir" = "$$entry_dir" ]; then \
				echo "Error: TEST_INCLUDES entry \"$$entry\" not located inside selftests directory ($$SRC_PATH)" >&2; \
				exit 1; \
			fi; \
			relative_files="$$relative_files $$relative_dir/$$entry_name"; \
		done; \
		cd $(SRC_PATH) && rsync -aR $$relative_files $(OBJ_PATH)/ \
	)
endef

run_tests: all
ifdef building_out_of_srctree
	@if [ "X$(TEST_PROGS)$(TEST_PROGS_EXTENDED)$(TEST_FILES)$(TEST_GEN_MODS_DIR)" != "X" ]; then \
		rsync -aq --copy-unsafe-links $(TEST_PROGS) $(TEST_PROGS_EXTENDED) $(TEST_FILES) $(TEST_GEN_MODS_DIR) $(OUTPUT); \
	fi
	@$(INSTALL_INCLUDES)
	@if [ "X$(TEST_PROGS)" != "X" ]; then \
		$(call RUN_TESTS, $(TEST_GEN_PROGS) $(TEST_CUSTOM_PROGS) \
				  $(addprefix $(OUTPUT)/,$(TEST_PROGS))) ; \
	else \
		$(call RUN_TESTS, $(TEST_GEN_PROGS) $(TEST_CUSTOM_PROGS)); \
	fi
else
	@$(call RUN_TESTS, $(TEST_GEN_PROGS) $(TEST_CUSTOM_PROGS) $(TEST_PROGS))
endif

gen_mods_dir:
	$(Q)$(MAKE) -C $(TEST_GEN_MODS_DIR)

clean_mods_dir:
	$(Q)$(MAKE) -C $(TEST_GEN_MODS_DIR) clean

define INSTALL_SINGLE_RULE
	$(if $(INSTALL_LIST),@mkdir -p $(INSTALL_PATH))
	$(if $(INSTALL_LIST),rsync -a --copy-unsafe-links $(INSTALL_LIST) $(INSTALL_PATH)/)
endef

define INSTALL_MODS_RULE
	$(if $(INSTALL_LIST),@mkdir -p $(INSTALL_PATH)/$(INSTALL_LIST))
	$(if $(INSTALL_LIST),rsync -a --copy-unsafe-links $(INSTALL_LIST)/*.ko $(INSTALL_PATH)/$(INSTALL_LIST))
endef

define INSTALL_RULE
	$(eval INSTALL_LIST = $(TEST_PROGS)) $(INSTALL_SINGLE_RULE)
	$(eval INSTALL_LIST = $(TEST_PROGS_EXTENDED)) $(INSTALL_SINGLE_RULE)
	$(eval INSTALL_LIST = $(TEST_FILES)) $(INSTALL_SINGLE_RULE)
	$(eval INSTALL_LIST = $(TEST_GEN_PROGS)) $(INSTALL_SINGLE_RULE)
	$(eval INSTALL_LIST = $(TEST_CUSTOM_PROGS)) $(INSTALL_SINGLE_RULE)
	$(eval INSTALL_LIST = $(TEST_GEN_PROGS_EXTENDED)) $(INSTALL_SINGLE_RULE)
	$(eval INSTALL_LIST = $(TEST_GEN_FILES)) $(INSTALL_SINGLE_RULE)
	$(eval INSTALL_LIST = $(notdir $(TEST_GEN_MODS_DIR))) $(INSTALL_MODS_RULE)
	$(eval INSTALL_LIST = $(wildcard config settings)) $(INSTALL_SINGLE_RULE)
endef

install: all
ifdef INSTALL_PATH
	$(INSTALL_RULE)
	$(INSTALL_INCLUDES)
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

clean: $(if $(TEST_GEN_MODS_DIR),clean_mods_dir)
	$(CLEAN)

# Build with _GNU_SOURCE by default
CFLAGS += -D_GNU_SOURCE=

# Enables to extend CFLAGS and LDFLAGS from command line, e.g.
# make USERCFLAGS=-Werror USERLDFLAGS=-static
CFLAGS += $(USERCFLAGS)
LDFLAGS += $(USERLDFLAGS)

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
	$(call msg,CC,,$@)
	$(Q)$(LINK.c) $(filter-out $(LOCAL_HDRS),$^) $(LDLIBS) -o $@

$(OUTPUT)/%.o:%.S
	$(COMPILE.S) $^ -o $@

$(OUTPUT)/%:%.S
	$(LINK.S) $^ $(LDLIBS) -o $@
endif

# Extract the expected header directory
khdr_output := $(patsubst %/usr/include,%,$(filter %/usr/include,$(KHDR_INCLUDES)))

headers:
	$(Q)$(MAKE) -f $(top_srcdir)/Makefile -C $(khdr_output) headers

.PHONY: run_tests all clean install emit_tests gen_mods_dir clean_mods_dir headers
