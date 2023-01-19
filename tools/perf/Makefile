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
# We don't want to pass along options like -j:
#
unexport MAKEFLAGS

#
# Do a parallel build with multiple jobs, based on the number of CPUs online
# in this system: 'make -j8' on a 8-CPU system, etc.
#
# (To override it, run 'make JOBS=1' and similar.)
#
ifeq ($(JOBS),)
  JOBS := $(shell (getconf _NPROCESSORS_ONLN || grep -E -c '^processor|^CPU[0-9]' /proc/cpuinfo) 2>/dev/null)
  ifeq ($(JOBS),0)
    JOBS := 1
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

define print_msg
  @printf '  BUILD:   Doing '\''make \033[33m-j'$(JOBS)'\033[m'\'' parallel build\n'
endef

define make
  @$(MAKE) -f Makefile.perf --no-print-directory -j$(JOBS) O=$(FULL_O) $(SET_DEBUG) $@
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
	@$(MAKE) SHUF=1 -f tests/make REUSE_FEATURES_DUMP=1 MK=Makefile SET_PARALLEL=1 --no-print-directory tarpkg make_static make_with_gtk2 out

build-test-tarball:
	@$(MAKE) -f tests/make REUSE_FEATURES_DUMP=1 MK=Makefile SET_PARALLEL=1 --no-print-directory out

#
# All other targets get passed through:
#
%: FORCE
	$(print_msg)
	$(make)

.PHONY: tags TAGS FORCE Makefile
