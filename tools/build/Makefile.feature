feature_dir := $(srctree)/tools/build/feature

ifneq ($(OUTPUT),)
  OUTPUT_FEATURES = $(OUTPUT)feature/
  $(shell mkdir -p $(OUTPUT_FEATURES))
endif

feature_check = $(eval $(feature_check_code))
define feature_check_code
  feature-$(1) := $(shell $(MAKE) OUTPUT=$(OUTPUT_FEATURES) CFLAGS="$(EXTRA_CFLAGS) $(FEATURE_CHECK_CFLAGS-$(1))" LDFLAGS="$(LDFLAGS) $(FEATURE_CHECK_LDFLAGS-$(1))" -C $(feature_dir) test-$1.bin >/dev/null 2>/dev/null && echo 1 || echo 0)
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
FEATURE_TESTS ?=			\
	backtrace			\
	dwarf				\
	fortify-source			\
	sync-compare-and-swap		\
	glibc				\
	gtk2				\
	gtk2-infobar			\
	libaudit			\
	libbfd				\
	libelf				\
	libelf-getphdrnum		\
	libelf-mmap			\
	libnuma				\
	numa_num_possible_cpus		\
	libperl				\
	libpython			\
	libpython-version		\
	libslang			\
	libunwind			\
	pthread-attr-setaffinity-np	\
	stackprotector-all		\
	timerfd				\
	libdw-dwarf-unwind		\
	zlib				\
	lzma				\
	get_cpuid			\
	bpf

FEATURE_DISPLAY ?=			\
	dwarf				\
	glibc				\
	gtk2				\
	libaudit			\
	libbfd				\
	libelf				\
	libnuma				\
	numa_num_possible_cpus		\
	libperl				\
	libpython			\
	libslang			\
	libunwind			\
	libdw-dwarf-unwind		\
	zlib				\
	lzma				\
	get_cpuid			\
	bpf

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
else
  $(shell $(MAKE) OUTPUT=$(OUTPUT_FEATURES) CFLAGS="$(EXTRA_CFLAGS)" LDFLAGS=$(LDFLAGS) -i -j -C $(feature_dir) $(addsuffix .bin,$(FEATURE_TESTS)) >/dev/null 2>&1)
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

FEATURE_DUMP_FILENAME = $(OUTPUT)FEATURE-DUMP$(FEATURE_USER)
FEATURE_DUMP := $(foreach feat,$(FEATURE_DISPLAY),feature-$(feat)($(feature-$(feat))))
FEATURE_DUMP_FILE := $(shell touch $(FEATURE_DUMP_FILENAME); cat $(FEATURE_DUMP_FILENAME))

ifeq ($(dwarf-post-unwind),1)
  FEATURE_DUMP += dwarf-post-unwind($(dwarf-post-unwind-text))
endif

# The $(feature_display) controls the default detection message
# output. It's set if:
# - detected features differes from stored features from
#   last build (in $(FEATURE_DUMP_FILENAME) file)
# - one of the $(FEATURE_DISPLAY) is not detected
# - VF is enabled

ifneq ("$(FEATURE_DUMP)","$(FEATURE_DUMP_FILE)")
  $(shell echo "$(FEATURE_DUMP)" > $(FEATURE_DUMP_FILENAME))
  feature_display := 1
endif

feature_display_check = $(eval $(feature_check_display_code))
define feature_display_check_code
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

  ifeq ($(dwarf-post-unwind),1)
    $(call feature_print_text,"DWARF post unwind library", $(dwarf-post-unwind-text))
  endif

  ifneq ($(feature_verbose),1)
    $(info )
  endif
endif

ifeq ($(feature_verbose),1)
  TMP := $(filter-out $(FEATURE_DISPLAY),$(FEATURE_TESTS))
  $(foreach feat,$(TMP),$(call feature_print_status,$(feat),))
  $(info )
endif
