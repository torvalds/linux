# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_SAMPLE_CONNECTOR) += cn_test.o

# List of programs to build
hostprogs := ucon
always-y := $(hostprogs)

HOSTCFLAGS_ucon.o += -I$(objtree)/usr/include

all: modules

modules clean:
	$(MAKE) -C ../.. M=$(CURDIR) $@
