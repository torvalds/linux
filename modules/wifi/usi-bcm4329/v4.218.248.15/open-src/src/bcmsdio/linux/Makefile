
# GNU Makefile for Broadcom BCMSDH Lower-level Driver
#
# Copyright (C) 1999-2010, Broadcom Corporation
# 
#      Unless you and Broadcom execute a separate written software license
# agreement governing use of this software, this software is licensed to you
# under the terms of the GNU General Public License version 2 (the "GPL"),
# available at http://www.broadcom.com/licenses/GPLv2.php, with the
# following added to such license:
# 
#      As a special exception, the copyright holders of this software give you
# permission to link this software with independent modules, and to copy and
# distribute the resulting executable under terms of your choice, provided that
# you also meet, for each linked independent module, the terms and conditions of
# the license of that module.  An independent module is a module which is not
# derived from this software.  The special exception does not apply to any
# modifications of the software.
# 
#      Notwithstanding the above, under no circumstances may you combine this
# software in any way with any other Broadcom software provided under a license
# other than the GPL, without Broadcom's express prior written consent.
#
# $Id: Makefile,v 1.5.8.4.6.2 2010/04/09 23:54:38 Exp $
#

# Try a couple of places for LINUXDIR if not specified
ifeq ($(LINUXDIR),)
ifeq ($(LINUXVER),)
# Neither one is specified, use uname for version
LINUXVER := $(shell uname -r)
endif
ifneq ($(wildcard /lib/modules/$(LINUXVER)/build/include/linux/version.h),)
LINUXDIR := /lib/modules/$(LINUXVER)/build
else
ifneq ($(wildcard /tools/linux/src/linux-$(LINUXVER)/include/linux/version.h),)
LINUXDIR := /tools/linux/src/linux-$(LINUXVER)
else
LINUXDIR := /usr/src/linux
endif
endif
endif

LINUXVER := $(shell { cat $(LINUXDIR)/Makefile; \
	echo "bcm$$$$:;@echo \$$(KERNELRELEASE)"; } | \
	$(MAKE) --no-print-directory $(if $(ARCH),ARCH=$(ARCH),) -C $(LINUXDIR) -f - bcm$$$$)

# check if 2.4 kernel or 2.5+ kernel
BCM_KVER:=$(shell echo $(LINUXVER) | cut -c1-3 | sed 's/2\.[56]/2\.6/')

# Allow CROSS_COMPILE to specify compiler base
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
NM := $(CROSS_COMPILE)nm
OBJCOPY := $(CROSS_COMPILE)objcopy

# driver source base and C file path
ifeq ($(SRCBASE),)
SRCBASE := $(shell /bin/pwd)/../..
endif
vpath %.c $(SRCBASE)/shared $(SRCBASE)/bcmsdio/sys $(SRCBASE)/wl/sys

## Initialize DFLAGS
DFLAGS :=


# basic options (defines in DFLAGS, includes in IFLAGS)
DFLAGS += -DLINUX -DSRCBASE=\"$(SRCBASE)\" -DBCMDRIVER -DBCMSDH_MODULE
DFLAGS += -DBCMDONGLEHOST
DFLAGS += -DBCM4325

IFLAGS := -I$(LINUXDIR)/include -I$(LINUXDIR)/include/asm/mach-default -I. -I$(SRCBASE)/include -I$(SRCBASE)/shared -I$(SRCBASE)/dongle -I$(SRCBASE)/wl/sys

WFLAGS := -Wall -Wstrict-prototypes
ifeq (,$(findstring 2.4.18,$(LINUXVER)))
WFLAGS += -Werror
endif

CFILES:= bcmsdh_linux.c linux_osl.c bcmsdh.c
CFILES += siutils.c sbutils.c aiutils.c bcmutils.c hndpmu.c

OFILES=$(CFILES:.c=.o)

# Make debug a separate option
ifneq ($(findstring -debug-,-$(TARGET)-),)
DFLAGS += -DBCMDBG -DSDTEST
endif

# Make big-endian a separate option
ifneq ($(findstring -be-,-$(TARGET)-),)
DFLAGS += -DIL_BIGENDIAN
endif

ifneq ($(findstring -sdstd,$(TARGET)-),)
DFLAGS += -DBCMSDIO -DBCMSDIOH_STD
CFILES += bcmsdstd.c bcmsdstd_linux.c
endif
ifneq ($(findstring -intc1,$(shell echo $(LINUXVER))),)
DFLAGS += -DSANDGATE2G
endif
ifneq ($(findstring -sdspi-,$(TARGET)-),)
DFLAGS += -DBCMSDIO -DBCMSDIOH_SPI -DTESTDONGLE
CFILES += bcmsdspi.c bcmsdspi_linux.c 
endif

CFLAGS += -fshort-wchar $(DFLAGS) $(WFLAGS) $(IFLAGS)

LDFLAGS := -r
MODULES := bcmsdh_driver.o
ifeq ($(BCM_KVER), 2.6)
  ##Kernel module names in 2.6 kernel have .ko suffix
  KMODULES:=bcmsdh_driver.ko
else
  KMODULES:=$(MODULES)
endif

# host options
HOSTCC := $(CC)
ifneq ($(BCM_KVER), 2.6)
  HOSTCFLAGS := $(CFLAGS) $(shell $(MAKE) --no-print-directory -s -C $(LINUXDIR) script 'SCRIPT=@echo $$(CFLAGS) $$(MODFLAGS)')
else
  HOSTCFLAGS := $(CFLAGS) -D__KERNEL__
  BCMSDHCFLAGS = $(HOSTCFLAGS) -I$(shell pwd)
  export BCMSDHCFLAGS
  BCMSDHOFILES = $(OFILES)
  export BCMSDHOFILES
endif

TARGETS := \
	bcmsdh-sdstd
ifneq ($(findstring -intc1,$(shell echo $(LINUXVER))),)
TARGETS := bcmsdh-sdiofd
endif
TARGETS += $(foreach tgt, $(TARGETS), $(tgt)-debug)

OBJDIR=$(TARGET)-$(LINUXVER)$(if $(BCMQT),-bcmqt)

all: $(TARGETS)
sdspi: $(filter %-sdspi-pci %-sdspi-cheetah, %-sdspi-u2c $(TARGETS))

# Allow making target with the LINUXVER suffix already on it.
# (Typical of command line tab completion; trailing slash still not allowed)
%-$(LINUXVER): force
	$(MAKE) $(@:%-$(LINUXVER)=%)

$(TARGETS):
	@echo "MAKING $@"
	$(MAKE) TARGET=$@ objdir

objdir:
	@echo "Making objdir $(OBJDIR)"
	@echo "TARGET is $(TARGET)"
	mkdir -p $(OBJDIR)
	$(MAKE) -C $(OBJDIR) -f ../Makefile SRCBASE=$(SRCBASE) dep
	$(MAKE) -C $(OBJDIR) -f ../Makefile SRCBASE=$(SRCBASE) modules
ifeq ($(BCM_KVER), 2.6)
	$(OBJCOPY) --strip-unneeded $(OBJDIR)/bcmsdh_driver.ko $(OBJDIR)/bcmsdh_driver.ko.stripped
else
	$(OBJCOPY) --strip-unneeded $(OBJDIR)/bcmsdh_driver.o $(OBJDIR)/bcmsdh_driver.o.stripped
endif

dep: $(foreach file,$(CFILES),.$(file).depend)
.%.c.depend: %.c
	$(HOSTCC) $(HOSTCFLAGS) -M $< > $@
.%.c.depend::
	touch $@

ifeq ($(BCM_KVER), 2.6)
modules: $(OFILES)
	test -r ./Makefile || ln -s ../makefile.26 ./Makefile
	$(MAKE) -C $(LINUXDIR) M=$(shell pwd) $(if $(VERBOSE),V=1) modules
else
modules: $(MODULES)
endif

bcmsdh_driver.o: $(OFILES)
	$(LD) $(LDFLAGS) -o $@ $^

ifeq ($(BCM_KVER), 2.6)
%.o: %.c
  # when make is called from 2.6, vpath doesn't work so we need to link the files.
	test -r ./$< || ln -s $< .
else
%.o: %.c
	$(HOSTCC) $(HOSTCFLAGS) -c -o $@ $<
	@( \
	echo 'ifneq ($$(HOSTCFLAGS),$(HOSTCFLAGS))' ; \
	echo '$@: force' ; \
	echo 'endif' ; \
	) > .$*.c.flags
endif

force:

clean:
	rm -rf bcmsdh-*

ifneq ($(wildcard .*.depend),)
include $(wildcard .*.depend)
endif
ifneq ($(wildcard .*.flags),)
include $(wildcard .*.flags)
endif
