# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

TESTS = test-cpumap test-threadmap test-evlist test-evsel

TESTS_SO := $(addsuffix -so,$(TESTS))
TESTS_A  := $(addsuffix -a,$(TESTS))

# Set compile option CFLAGS
ifdef EXTRA_CFLAGS
  CFLAGS := $(EXTRA_CFLAGS)
else
  CFLAGS := -g -Wall
endif

all:

include $(srctree)/tools/scripts/Makefile.include

INCLUDE = -I$(srctree)/tools/lib/perf/include -I$(srctree)/tools/include -I$(srctree)/tools/lib

$(TESTS_A): FORCE
	$(QUIET_LINK)$(CC) $(INCLUDE) $(CFLAGS) -o $@ $(subst -a,.c,$@) ../libperf.a $(LIBAPI)

$(TESTS_SO): FORCE
	$(QUIET_LINK)$(CC) $(INCLUDE) $(CFLAGS) -L.. -o $@ $(subst -so,.c,$@) $(LIBAPI) -lperf

all: $(TESTS_A) $(TESTS_SO)

run:
	@echo "running static:"
	@for i in $(TESTS_A); do ./$$i; done
	@echo "running dynamic:"
	@for i in $(TESTS_SO); do LD_LIBRARY_PATH=../ ./$$i; done

clean:
	$(call QUIET_CLEAN, tests)$(RM) $(TESTS_A) $(TESTS_SO)

.PHONY: all clean FORCE
