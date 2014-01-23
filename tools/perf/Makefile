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
  JOBS := $(shell grep -c ^processor /proc/cpuinfo 2>/dev/null)
  ifeq ($(JOBS),)
    JOBS := 1
  endif
endif

#
# Only pass canonical directory names as the output directory:
#
ifneq ($(O),)
  FULL_O := $(shell readlink -f $(O) || echo $(O))
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
#
all:
	$(print_msg)
	$(make)

#
# The clean target is not really parallel, don't print the jobs info:
#
clean:
	$(make)

#
# All other targets get passed through:
#
%:
	$(print_msg)
	$(make)
