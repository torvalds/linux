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

export JOBS

define print_msg
  @printf '    BUILD: Doing '\''make \033[33m-j'$(JOBS)'\033[m'\'' parallel build\n'
endef

define make
  @$(MAKE) -f Makefile.perf --no-print-directory -j$(JOBS) $@
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
