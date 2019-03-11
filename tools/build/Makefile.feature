feature_dir := $(srctree)/tools/build/feature

ifneq ($(OUTPUT),)
  OUTPUT_FEATURES = $(OUTPUT)feature/
  $(shell mkdir -p $(OUTPUT_FEATURES))
endif

feature_check = $(eval $(feature_check_code))
define feature_check_code
  feature-$(1) := $(shell $(MAKE) OUTPUT=$(OUTPUT_FEATURES) CFLAGS="$(EXTRA_CFLAGS) $(FEATURE_CHECK_CFLAGS-$(1))" CXXFLAGS="$(EXTRA_CXXFLAGS) $(FEATURE_CHECK_CXXFLAGS-$(1))" LDFLAGS="$(LDFLAGS) $(FEATURE_CHECK_LDFLAGS-$(1))" -C $(feature_dir) $(OUTPUT_FEATURES)test-$1.bin >/dev/null 2>/dev/null && echo 1 || echo 0)
endef

feature_set = $(eval $(feature_set_code))
define feature_set_code
  feature-$(1) := 1
endef

#
# Build the feature check binaries in parallel, ignore errors, ignore return value and suppress output:
#

#
# Note that this is not a complete list of all feature tests, just
# those that are typically built on a fully configured system.
#
# [ Feature tests not mentioned here have to be built explicitly in
#   the rule that uses them - an example for that is the 'bionic'
#   feature check. ]
#
FEATURE_TESTS_BASIC :=                  \
        backtrace                       \
        dwarf                           \
        dwarf_getlocations              \
        eventfd                         \
        fortify-source                  \
        sync-compare-and-swap           \
        get_current_dir_name            \
        glibc                           \
        gtk2                            \
        gtk2-infobar                    \
        libaudit                        \
        libbfd                          \
        libelf                          \
        libelf-getphdrnum               \
        libelf-gelf_getnote             \
        libelf-getshdrstrndx            \
        libelf-mmap                     \
        libnuma                         \
        numa_num_possible_cpus          \
        libperl                         \
        libpython                       \
        libpython-version               \
        libslang                        \
        libcrypto                       \
        libunwind                       \
        pthread-attr-setaffinity-np     \
        pthread-barrier     		\
        reallocarray                    \
        stackprotector-all              \
        timerfd                         \
        libdw-dwarf-unwind              \
        zlib                            \
        lzma                            \
        get_cpuid                       \
        bpf                             \
        sched_getcpu			\
        sdt				\
        setns				\
        libaio

# FEATURE_TESTS_BASIC + FEATURE_TESTS_EXTRA is the complete list
# of all feature tests
FEATURE_TESTS_EXTRA :=                  \
         bionic                         \
         compile-32                     \
         compile-x32                    \
         cplus-demangle                 \
         hello                          \
         libbabeltrace                  \
         libbfd-liberty                 \
         libbfd-liberty-z               \
         libopencsd                     \
         libunwind-x86                  \
         libunwind-x86_64               \
         libunwind-arm                  \
         libunwind-aarch64              \
         libunwind-debug-frame          \
         libunwind-debug-frame-arm      \
         libunwind-debug-frame-aarch64  \
         cxx                            \
         llvm                           \
         llvm-version                   \
         clang

FEATURE_TESTS ?= $(FEATURE_TESTS_BASIC)

ifeq ($(FEATURE_TESTS),all)
  FEATURE_TESTS := $(FEATURE_TESTS_BASIC) $(FEATURE_TESTS_EXTRA)
endif

FEATURE_DISPLAY ?=              \
         dwarf                  \
         dwarf_getlocations     \
         glibc                  \
         gtk2                   \
         libaudit               \
         libbfd                 \
         libelf                 \
         libnuma                \
         numa_num_possible_cpus \
         libperl                \
         libpython              \
         libslang               \
         libcrypto              \
         libunwind              \
         libdw-dwarf-unwind     \
         zlib                   \
         lzma                   \
         get_cpuid              \
         bpf			\
         libaio

# Set FEATURE_CHECK_(C|LD)FLAGS-all for all FEATURE_TESTS features.
# If in the future we need per-feature checks/flags for features not
# mentioned in this list we need to refactor this ;-).
set_test_all_flags = $(eval $(set_test_all_flags_code))
define set_test_all_flags_code
  FEATURE_CHECK_CFLAGS-all  += $(FEATURE_CHECK_CFLAGS-$(1))
  FEATURE_CHECK_LDFLAGS-all += $(FEATURE_CHECK_LDFLAGS-$(1))
endef

$(foreach feat,$(FEATURE_TESTS),$(call set_test_all_flags,$(feat)))

#
# Special fast-path for the 'all features are available' case:
#
$(call feature_check,all,$(MSG))

#
# Just in case the build freshly failed, make sure we print the
# feature matrix:
#
ifeq ($(feature-all), 1)
  #
  # test-all.c passed - just set all the core feature flags to 1:
  #
  $(foreach feat,$(FEATURE_TESTS),$(call feature_set,$(feat)))
  #
  # test-all.c does not comprise these tests, so we need to
  # for this case to get features proper values
  #
  $(call feature_check,compile-32)
  $(call feature_check,compile-x32)
  $(call feature_check,bionic)
  $(call feature_check,libbabeltrace)
else
  $(foreach feat,$(FEATURE_TESTS),$(call feature_check,$(feat)))
endif

#
# Print the result of the feature test:
#
feature_print_status = $(eval $(feature_print_status_code)) $(info $(MSG))

define feature_print_status_code
  ifeq ($(feature-$(1)), 1)
    MSG = $(shell printf '...%30s: [ \033[32mon\033[m  ]' $(1))
  else
    MSG = $(shell printf '...%30s: [ \033[31mOFF\033[m ]' $(1))
  endif
endef

feature_print_text = $(eval $(feature_print_text_code)) $(info $(MSG))
define feature_print_text_code
    MSG = $(shell printf '...%30s: %s' $(1) $(2))
endef

#
# generates feature value assignment for name, like:
#   $(call feature_assign,dwarf) == feature-dwarf=1
#
feature_assign = feature-$(1)=$(feature-$(1))

FEATURE_DUMP_FILENAME = $(OUTPUT)FEATURE-DUMP$(FEATURE_USER)
FEATURE_DUMP := $(shell touch $(FEATURE_DUMP_FILENAME); cat $(FEATURE_DUMP_FILENAME))

feature_dump_check = $(eval $(feature_dump_check_code))
define feature_dump_check_code
  ifeq ($(findstring $(1),$(FEATURE_DUMP)),)
    $(2) := 1
  endif
endef

#
# First check if any test from FEATURE_DISPLAY
# and set feature_display := 1 if it does
$(foreach feat,$(FEATURE_DISPLAY),$(call feature_dump_check,$(call feature_assign,$(feat)),feature_display))

#
# Now also check if any other test changed,
# so we force FEATURE-DUMP generation
$(foreach feat,$(FEATURE_TESTS),$(call feature_dump_check,$(call feature_assign,$(feat)),feature_dump_changed))

# The $(feature_display) controls the default detection message
# output. It's set if:
# - detected features differes from stored features from
#   last build (in $(FEATURE_DUMP_FILENAME) file)
# - one of the $(FEATURE_DISPLAY) is not detected
# - VF is enabled

ifeq ($(feature_dump_changed),1)
  $(shell rm -f $(FEATURE_DUMP_FILENAME))
  $(foreach feat,$(FEATURE_TESTS),$(shell echo "$(call feature_assign,$(feat))" >> $(FEATURE_DUMP_FILENAME)))
endif

feature_display_check = $(eval $(feature_check_display_code))
define feature_check_display_code
  ifneq ($(feature-$(1)), 1)
    feature_display := 1
  endif
endef

$(foreach feat,$(FEATURE_DISPLAY),$(call feature_display_check,$(feat)))

ifeq ($(VF),1)
  feature_display := 1
  feature_verbose := 1
endif

ifeq ($(feature_display),1)
  $(info )
  $(info Auto-detecting system features:)
  $(foreach feat,$(FEATURE_DISPLAY),$(call feature_print_status,$(feat),))
  ifneq ($(feature_verbose),1)
    $(info )
  endif
endif

ifeq ($(feature_verbose),1)
  TMP := $(filter-out $(FEATURE_DISPLAY),$(FEATURE_TESTS))
  $(foreach feat,$(TMP),$(call feature_print_status,$(feat),))
  $(info )
endif
