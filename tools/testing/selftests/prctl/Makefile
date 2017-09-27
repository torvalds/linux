ifndef CROSS_COMPILE
uname_M := $(shell uname -m 2>/dev/null || echo not)
ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/x86/ -e s/x86_64/x86/)

ifeq ($(ARCH),x86)
TEST_PROGS := disable-tsc-ctxt-sw-stress-test disable-tsc-on-off-stress-test \
		disable-tsc-test
all: $(TEST_PROGS)

include ../lib.mk

clean:
	rm -fr $(TEST_PROGS)
endif
endif
