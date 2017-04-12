ifndef CROSS_COMPILE
uname_M := $(shell uname -m 2>/dev/null || echo not)
ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/x86/ -e s/x86_64/x86/)

ifeq ($(ARCH),x86)

PROGS := mpssd
CC = $(CROSS_COMPILE)gcc
CFLAGS := -I../../../usr/include -I../../../tools/include

ifdef DEBUG
CFLAGS += -DDEBUG=$(DEBUG)
endif

all: $(PROGS)
mpssd: mpssd.c sysfs.c
	$(CC) $(CFLAGS) mpssd.c sysfs.c -o mpssd -lpthread

install:
	install mpssd /usr/sbin/mpssd
	install micctrl /usr/sbin/micctrl

clean:
	rm -fr $(PROGS)

endif
endif
