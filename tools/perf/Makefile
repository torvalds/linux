# SPDX-License-Identifier: GPL-2.0
#
# This is a simple wrapper Makefile that calls the main Makefile.perf
# with a -j option to do parallel builds
#
# If you want to invoke the perf build in some non-standard way then
# you can use the 'make -f Makefile.perf' method to invoke it.
#

#
# Clear out the built-in rules GNU make defines by default (such as .o targets),
# so that we pass through all targets to Makefile.perf:
#
.SUFFIXES:

#
# If no parallel build was requested, do a parallel build with multiple jobs,
# based on the number of CPUs online in this system: 'make -j8' on an 8-CPU
# system, etc.
#
# If MAKEFLAGS contains '-j' or 'jobserver', JOBS is intentionally left
# uninitialized so the native GNU Make jobserver can seamlessly control the
# parallel bounds of the build.
#
# (To override it, run 'make JOBS=1' and similar.)
#
ifeq ($(findstring -j,$(MAKEFLAGS)),)
  ifeq ($(findstring jobserver,$(MAKEFLAGS)),)
    ifeq ($(JOBS),)
      JOBS := $(shell (getconf _NPROCESSORS_ONLN || grep -E -c '^processor|^CPU[0-9]' /proc/cpuinfo) 2>/dev/null)
      ifeq ($(JOBS),0)
        JOBS := 1
      endif
    endif
    PARALLEL_OPT := -j$(JOBS)
  endif
endif


#
# Only pass canonical directory names as the output directory:
#
ifneq ($(O),)
  FULL_O := $(shell cd $(PWD); readlink -f $(O) || echo $(O))
endif

#
# Only accept the 'DEBUG' variable from the command line:
#
ifeq ("$(origin DEBUG)", "command line")
  ifeq ($(DEBUG),)
    override DEBUG = 0
  else
    SET_DEBUG = "DEBUG=$(DEBUG)"
  endif
else
  override DEBUG = 0
endif

ifeq ($(JOBS),1)
  BUILD_TYPE := sequential
else
  BUILD_TYPE := parallel
endif

define print_msg
  @printf '  BUILD:   Doing '\''make\033[33m%s\033[m'\'' $(BUILD_TYPE) build\n' "$(if $(JOBS), -j$(JOBS),)"
endef

define make
  @$(MAKE) -f Makefile.perf --no-print-directory $(PARALLEL_OPT) O=$(FULL_O) $(SET_DEBUG) $@
endef

#
# Needed if no target specified:
# (Except for tags and TAGS targets. The reason is that the
# Makefile does not treat tags/TAGS as targets but as files
# and thus won't rebuilt them once they are in place.)
#
all tags TAGS:
	$(print_msg)
	$(make)

ifdef MAKECMDGOALS
has_clean := 0
ifneq ($(filter clean,$(MAKECMDGOALS)),)
  has_clean := 1
endif # clean

ifeq ($(has_clean),1)
  rest := $(filter-out clean,$(MAKECMDGOALS))
  ifneq ($(rest),)
$(rest): clean
  endif # rest
endif # has_clean

ifneq ($(filter install-build-deps,$(MAKECMDGOALS)),)
  after_deps := $(filter-out clean install-build-deps,$(MAKECMDGOALS))
  ifneq ($(after_deps),)
$(after_deps): install-build-deps
  endif
endif

ifneq ($(filter all,$(MAKECMDGOALS)),)
  install_goals := $(filter-out install-build-deps,$(filter install%,$(MAKECMDGOALS)))
  ifneq ($(install_goals),)
$(install_goals): all
  endif
endif
endif # MAKECMDGOALS

#
# Explicitly disable parallelism for the clean target.
#
clean:
	$(make) -j1

#
# The build-test target is not really parallel, don't print the jobs info,
# it also uses only the tests/make targets that don't pollute the source
# repository, i.e. that uses O= or builds the tarpkg outside the source
# repo directories.
#
# For a full test, use:
#
# make -C tools/perf -f tests/make
#
build-test:
	@$(MAKE) SHUF=1 -f tests/make REUSE_FEATURES_DUMP=1 MK=Makefile SET_PARALLEL=1 --no-print-directory tarpkg
	@$(MAKE) SHUF=1 -f tests/make REUSE_FEATURES_DUMP=1 MK=Makefile SET_PARALLEL=1 --no-print-directory make_static
	@$(MAKE) SHUF=1 -f tests/make REUSE_FEATURES_DUMP=1 MK=Makefile SET_PARALLEL=1 --no-print-directory make_with_gtk4
	@$(MAKE) SHUF=1 -f tests/make REUSE_FEATURES_DUMP=1 MK=Makefile SET_PARALLEL=1 --no-print-directory out

build-test-tarball:
	@$(MAKE) -f tests/make REUSE_FEATURES_DUMP=1 MK=Makefile SET_PARALLEL=1 --no-print-directory out

check-headers:
	@./check-headers.sh

sync-headers:
	@./scripts/sync-headers.sh $(SYNC_HEADERS_ARGS)

#
# All other targets get passed through:
#
%: FORCE
	$(print_msg)
	$(make)

.PHONY: all clean tags TAGS FORCE Makefile build-test build-test-tarball check-headers sync-headers
