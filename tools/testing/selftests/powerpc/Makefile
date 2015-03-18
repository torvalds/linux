# Makefile for powerpc selftests

# ARCH can be overridden by the user for cross compiling
ARCH ?= $(shell uname -m)
ARCH := $(shell echo $(ARCH) | sed -e s/ppc.*/powerpc/)

ifeq ($(ARCH),powerpc)

GIT_VERSION = $(shell git describe --always --long --dirty || echo "unknown")

CC := $(CROSS_COMPILE)$(CC)
CFLAGS := -Wall -O2 -flto -Wall -Werror -DGIT_VERSION='"$(GIT_VERSION)"' -I$(CURDIR) $(CFLAGS)

export CC CFLAGS

SUB_DIRS = pmu copyloops mm tm primitives stringloops vphn

endif

all: $(SUB_DIRS)

$(SUB_DIRS):
	$(MAKE) -k -C $@ all

run_tests: all
	@for TARGET in $(SUB_DIRS); do \
		$(MAKE) -C $$TARGET run_tests; \
	done;

clean:
	@for TARGET in $(SUB_DIRS); do \
		$(MAKE) -C $$TARGET clean; \
	done;
	rm -f tags

tags:
	find . -name '*.c' -o -name '*.h' | xargs ctags

.PHONY: all run_tests clean tags $(SUB_DIRS)
