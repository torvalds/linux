ifndef CROSS_COMPILE
uname_M := $(shell uname -m 2>/dev/null || echo not)
ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/x86/ -e s/x86_64/x86/)

ifeq ($(ARCH),x86)
CC := $(CROSS_COMPILE)gcc
PROGS := hpet_example

all: $(PROGS)

clean:
	rm -fr $(PROGS)

endif
endif
