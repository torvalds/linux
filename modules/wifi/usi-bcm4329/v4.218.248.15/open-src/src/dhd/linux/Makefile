
# GNU Makefile for Broadcom Dongle Host Driver
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
# $Id: Makefile,v 1.55.2.6.2.10.6.42.4.1 2010/11/16 04:07:44 Exp $
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

# Derive LINUXVER from LINUXDIR
MYKERNEL_RELEASE_KEYWORD:="KERNELRELEASE[[:space:]]*=.*kernel.release"
MYKERNEL_DEFINITION:=$(if \
  $(shell grep $(MYKERNEL_RELEASE_KEYWORD) $(LINUXDIR)/Makefile 2> /dev/null),\
  grep $(MYKERNEL_RELEASE_KEYWORD) $(LINUXDIR)/Makefile,\
  cat $(LINUXDIR)/Makefile)

LINUXVER:=$(shell ($(MYKERNEL_DEFINITION); echo "show_kernel_version_number$$$$:;@echo \$$(KERNELRELEASE)") 2> /dev/null | $(MAKE) --no-print-directory -k -C $(LINUXDIR) MYUNAME="" -f - show_kernel_version_number$$$$ 2> /dev/null)

ifeq ($(LINUXVER),)
     $(error LINUXVER=$(LINUXVER) is empty)
endif # LINUXVER

$(warning Found LINUXVER=$(LINUXVER))
$(warning Found LINUXDIR=$(LINUXDIR))

# check if 2.4 kernel or 2.5+ kernel
#BCM_KVER:=$(shell echo $(LINUXVER) | cut -c1-3 | sed 's/2\.[56]/2\.6/')
BCM_KVER:=$(shell echo $(LINUXVER) | cut -c1-3 | sed 's/[23]\.[056]/2\.6+/')

# Allow CROSS_COMPILE to specify compiler base
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
NM := $(CROSS_COMPILE)nm
OBJCOPY := $(CROSS_COMPILE)objcopy

# driver source base and C file path
ifeq ($(SRCBASE),)
SRCBASE := $(shell /bin/pwd)/../..
FWBASE := $(SRCBASE)/../../firmware/4329inuse
endif
vpath %.c $(SRCBASE)/dhd/sys $(SRCBASE)/shared $(SRCBASE)/bcmsdio/sys $(SRCBASE)/wl/sys $(SRCBASE)/crypto

## Initialize DFLAGS
DFLAGS :=


# basic options (defines in DFLAGS, includes in IFLAGS)
DFLAGS += -DLINUX -DSRCBASE=\"$(SRCBASE)\" -DBCMDRIVER -DBCMDONGLEHOST -DDHDTHREAD -DBCMWPA2 -DBCMWAPI_WPI
DFLAGS += -DUNRELEASEDCHIP
ifeq ($(BCMQT),1)
	DFLAGS += -DBCMSLTGT -DBCMQT
endif
ifeq ($(WLTEST),1)
	DFLAGS += -DWLTEST -DIOCTL_RESP_TIMEOUT=20000
	DFLAGS += -DDHD_SPROM
endif


# Past 2.6.29 kernels, arch specific bits are re-organized in linux kernel. So
# append new include paths to existing ones to get 2.6.29+ kernels compile

# Default DHDARCH is x86
ifdef ARCH
   DHDARCH ?= $(ARCH)
else
   DHDARCH ?= x86
endif

ifneq ($(findstring native,$(TARGET)),)
   DHDARCH = x86
endif
ifneq ($(findstring mips,$(TARGET)),)
   DHDARCH = mips
endif
ifneq ($(findstring arm,$(TARGET)),)
   DHDARCH = arm
endif

INSTALL_DIR ?= ./obj

# First include from linux kernel dirs
IFLAGS := -I$(LINUXDIR)/include 
IFLAGS += -I$(LINUXDIR)/include/asm/mach-default 
# Followed by 2.6.29+ specific paths
IFLAGS += -I$(LINUXDIR)/arch/$(DHDARCH)/include
IFLAGS += -I$(LINUXDIR)/arch/$(DHDARCH)/include/asm/mach-default

# From current workspace
IFLAGS += -I. 
IFLAGS += -I$(SRCBASE)/include 
IFLAGS += -I$(SRCBASE)/shared 
IFLAGS += -I$(SRCBASE)/dhd/sys 
IFLAGS += -I$(SRCBASE)/dongle 
IFLAGS += -I$(SRCBASE)/wl/sys

ifneq ($(wildcard $(LINUXDIR)/.config),)
include $(LINUXDIR)/.config
else
# This is dangerous, since we don't know if they are really configured.
CONFIG_WIRELESS_EXT=y
DFLAGS += -DCONFIG_WIRELESS_EXT
endif

ifeq ($(CONFIG_MMC_MSM7X00A),y)
DFLAGS += -Dlinux
DFLAGS += -DDHD_SDALIGN=64 -DMAX_HDR_READ=64 -DDHD_FIRSTREAD=64
endif

WFLAGS := -Wall -Wstrict-prototypes
ifeq (,$(findstring 2.4.18,$(LINUXVER)))
#WFLAGS += -Werror
endif

CFILES:= dhd_linux.c linux_osl.c bcmutils.c dhd_common.c dhd_custom_gpio.c
CFILES += siutils.c sbutils.c aiutils.c hndpmu.c

# threading options
ifneq ($(findstring -nothread-,-$(TARGET)-),)
DFLAGS += -UDHDTHREAD
endif

# Building gpl provides thread prioritization
ifneq ($(findstring -gpl-,-$(TARGET)-),)
CFILES += dhd_linux_sched.c
DFLAGS += -DDHD_GPL -DDHD_SCHED
endif

ifeq ($(WLTEST),1)
	CFILES += bcmsrom.c bcmotp.c
endif

ifeq ($(CONFIG_NET_RADIO),y)
CFILES +=  bcmwifi.c
CFILES += wl_iw.c
else
	ifeq ($(CONFIG_WIRELESS_EXT),y)
	CFILES += bcmwifi.c
	CFILES += wl_iw.c
	endif
endif
ifeq ($(BCM_KVER), 2.6+)
CFG80211_KVER:=$(shell echo $(LINUXVER) | cut -c5-6 | sed 's/3[1-9]/true/')
ifeq ($(CFG80211_KVER), true)
	ifeq ($(CONFIG_CFG80211),m)
	DFLAGS += -DCONFIG_CFG80211
	CFILES += wl_cfg80211.c
	endif
endif
endif


OFILES=$(CFILES:.c=.o)

# Make debug a separate option
ifneq ($(findstring -debug-,-$(TARGET)-),)
DFLAGS += -DDHD_DEBUG -DSDTEST
endif

# Make big-endian a separate option
ifneq ($(findstring -be-,-$(TARGET)-),)
DFLAGS += -DIL_BIGENDIAN
endif

ifneq ($(findstring -dnglimage-,-$(TARGET)-),)
## Embeddable dongle image name
DNGL_IMAGE_NAME ?= 4325b0/sdio-g-cdc-reclaim-idsup-wme
DFLAGS += -DBCMEMBEDIMAGE -DIMAGE_NAME="$(DNGL_IMAGE_NAME)"
IFLAGS += -I$(SRCBASE)/dongle/rte/wl/builds/$(DNGL_IMAGE_NAME)
endif

ifneq ($(findstring -cdc-,-$(TARGET)-),)
DFLAGS += -DBDC -DTOE 
DFLAGS += -DDHD_BCMEVENTS -DSHOW_EVENTS
CFILES += dhd_cdc.c
ifneq ($(findstring -apsta-,-$(TARGET)-),)
DFLAGS += -DAP -DAPSTA_PINGTEST
endif
endif
ifneq ($(findstring -rndis-,-$(TARGET)-),)
DFLAGS += -DRNDIS
CFILES += dhd_rndis.c
endif
		  
ifneq ($(findstring -usb-,-$(TARGET)-),)
DFLAGS += -DBCMDHDUSB
CFILES += dhd_usb_linux.c
endif
ifneq ($(findstring -sdio-,-$(TARGET)-),)
DFLAGS += -DBCMSDIO
CFILES += dhd_sdio.c
endif
ifneq ($(findstring -sdstd-,$(TARGET)-),)
DFLAGS += -DBCMSDIO -DBCMSDIOH_STD
CFILES += dhd_sdio.c bcmsdh.c bcmsdstd.c bcmsdstd_linux.c bcmsdh_linux.c
endif
ifneq ($(findstring -softap-,-$(TARGET)-),)
DFLAGS += -DSOFTAP
CFILES += sha1.c md5.c
endif
ifneq ($(findstring -nexus-,-$(TARGET)-),)
DFLAGS += -DOEM_ANDROID -DEMBEDDED_PLATFORM -DARP_OFFLOAD_SUPPORT -DPKT_FILTER_SUPPORT
DFLAGS += -Dlinux
DFLAGS += -DBCMDBG
DFLAGS += -DDHD_USE_STATIC_BUF
DFLAGS += -DCUSTOMER_HW2
DFLAGS += -DCUSTOM_OOB_GPIO_NUM=152
DFLAGS += -DOOB_INTR_ONLY
DFLAGS += -DMMC_SDIO_ABORT
DFLAGS += -DSOFTAP
DFLAGS += -DKEEP_ALIVE
DFLAGS += -DPNO_SUPPORT
else
ifneq ($(findstring -oob-,-$(TARGET)-),)
DFLAGS += -DOOB_INTR_ONLY
DFLAGS += -DHW_OOB
DFLAGS += -DMMC_SDIO_ABORT
else
ifneq ($(findstring -sdmmc-,-$(TARGET)-),)
DFLAGS += -DSDIO_ISR_THREAD
DFLAGS += -DEMBEDDED_PLATFORM
#DFLAGS += -DPNO_SUPPORT
#DFLAGS += -DCSCAN
endif
endif
endif
ifneq ($(findstring -sdmmc-,-$(TARGET)-),)
DFLAGS += -DBCMSDIO -DDHD_GPL -DBCMLXSDMMC -DBCMPLATFORM_BUS 
CFILES += dhd_sdio.c bcmsdh_sdmmc.c bcmsdh.c bcmsdh_linux.c bcmsdh_sdmmc_linux.c
endif
ifneq ($(findstring -sdspi-,$(TARGET)-),)
DFLAGS += -DBCMSDIO -DBCMSDIOH_SPI -DTESTDONGLE # -DBCMSDYIELD
CFILES += dhd_sdio.c bcmsdh.c bcmsdspi.c bcmsdspi_linux.c bcmsdh_linux.c 
endif
ifneq ($(findstring -pci,$(TARGET)-),)
CFILES += bcmpcispi.c
endif
ifneq ($(findstring -sdext-,$(TARGET)-),)
DFLAGS += -DBCMSDIO -DTESTDONGLE
CFILES += dhd_sdio.c
endif
ifneq ($(findstring -intc1,$(shell echo $(LINUXVER))),)
DFLAGS += -DSANDGATE2G
endif

ifeq ($(OEM_ANDROID),1)
DFLAGS += -DOEM_ANDROID -DEMBEDDED_PLATFORM -DARP_OFFLOAD_SUPPORT -DPKT_FILTER_SUPPORT
endif
ifeq ($(OEM_CHROMIUMOS),1)
DFLAGS += -DOEM_CHROMIUMOS -DEMBEDDED_PLATFORM -DARP_OFFLOAD_SUPPORT -DPKT_FILTER_SUPPORT
endif

CFLAGS += -fshort-wchar $(DFLAGS) $(WFLAGS) $(IFLAGS) $(CUSTOM_FLAGS)



LDFLAGS := -r
MODULES := dhd.o
ifeq ($(BCM_KVER), 2.6+)
  ##Kernel module names in 2.6 kernel have .ko suffix
  KMODULES:=dhd.ko
  ifneq ($(findstring -nexus-,$(TARGET)-),)
    KMODULES:=bcm4329.ko
  endif
else
  KMODULES:=$(MODULES)
endif


# host options
HOSTCC := $(CC)
ifneq ($(BCM_KVER), 2.6+)
  HOSTCFLAGS := $(CFLAGS) $(shell $(MAKE) --no-print-directory -s -C $(LINUXDIR) script 'SCRIPT=@echo $$(CFLAGS) $$(MODFLAGS)')
else
  HOSTCFLAGS := $(CFLAGS) -D__KERNEL__
  DHDCFLAGS = $(HOSTCFLAGS) -I$(shell pwd)
  export DHDCFLAGS
  DHDOFILES = $(OFILES)
  export DHDOFILES
endif

TARGETS := \
	dhd-cdc-usb dhd-cdc-sdstd \
	dhd-cdc-sdspi-pci dhd-cdc-sdmmc-gpl dhd-cdc-sdmmc-oob-gpl dhd-cdc-sdmmc-nexus-gpl\
	dhd-cdc-usb-apsta dhd-cdc-usb-gpl \
	dhd-cdc-sdstd-apsta \
	dhd-cdc-sdmmc-softap-gpl


TARGETS += \
	dhd-cdc-sdio-dnglimage dhd-cdc-sdspi-pci-dnglimage \
	dhd-cdc-gspi-pci
#ifdef RNDIS
TARGETS += dhd-rndis-usb
#endif
TARGETS += dhd-cdc-sdext-be
TARGETS += dhd-cdc-sdext-be-dnglimage
ifneq ($(findstring -intc1,$(shell echo $(LINUXVER))),)
TARGETS += dhd-cdc-sdio dhd-cdc-sdiofd # dhd-cdc-sdmmc
endif
TARGETS += $(foreach tgt, $(TARGETS), $(tgt)-debug)

OBJDIR=$(TARGET)-$(LINUXVER)$(if $(BCMQT),-bcmqt)

all: $(filter %-sdio %-sdbcm %-sdstd %-usb %sdspi-pci %-sdiofd %-sdmmc, $(TARGETS))
sdio: $(filter %-sdio %-sdbcm %-sdstd, $(TARGETS))
usb: $(filter %-usb, $(TARGETS))
sdspi: $(filter %-sdspi-pci %-sdspi-cheetah, %-sdspi-u2c $(TARGETS))

# Allow making target with the LINUXVER suffix already on it.
# (Typical of command line tab completion; trailing slash still not allowed)
%-$(LINUXVER): force
	$(MAKE) $(@:%-$(LINUXVER)=%)

$(TARGETS):
	@echo "MAKING $@"
	$(MAKE) TARGET=$@ objdir

# Show compiler version, for the current target build
showenv:
	@echo "CC = $(CC) (ver=`$(CC) -dumpversion`; host=`hostname`; processor=`uname -m`)"

objdir: showenv
	@echo "Making objdir $(OBJDIR)"
	@echo "TARGET is $(TARGET)"
	mkdir -p $(OBJDIR)
ifneq ($(BCM_KVER), 2.6+)
	$(MAKE) -C $(OBJDIR) -f $(SRCBASE)/dhd/linux/Makefile SRCBASE=$(SRCBASE) dep
endif
	$(MAKE) -C $(OBJDIR) -f $(SRCBASE)/dhd/linux/Makefile SRCBASE=$(SRCBASE) modules
ifeq ($(BCM_KVER), 2.6+)
	$(OBJCOPY) --strip-unneeded $(OBJDIR)/$(KMODULES) $(OBJDIR)/$(KMODULES).stripped
else
	$(OBJCOPY) --strip-unneeded $(OBJDIR)/dhd.o $(OBJDIR)/dhd.o.stripped
endif
	test -r $(INSTALL_DIR) || mkdir $(INSTALL_DIR);
	cp $(OBJDIR)/$(KMODULES) $(INSTALL_DIR)/usi4329_dhd.ko
	cp $(FWBASE)/fw_bcm4329.bin $(INSTALL_DIR)/usi4329_fw.bin
	cp $(FWBASE)/usi4329_nvram.txt $(INSTALL_DIR)/usi4329_nvram.txt    

dep: $(foreach file,$(CFILES),.$(file).depend)
.%.c.depend: %.c
	$(HOSTCC) $(HOSTCFLAGS) -M $< > $@ || (rm -f $@; exit 1)
.%.c.depend::
	touch $@

ifeq ($(BCM_KVER), 2.6+)
modules: $(OFILES)
	test -r ./Makefile || ln -s $(SRCBASE)/dhd/linux/makefile.26 ./Makefile
	$(MAKE) -C $(LINUXDIR) M=$(shell pwd) $(if $(VERBOSE),V=1) modules
else
modules: $(MODULES)
endif

$(MODULES): $(OFILES)
	$(LD) $(LDFLAGS) -o $@ $^

ifeq ($(BCM_KVER), 2.6+)
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
	rm -rf dhd-* $(INSTALL_DIR)/usi4329*

ifneq ($(wildcard .*.depend),)
include $(wildcard .*.depend)
endif
ifneq ($(wildcard .*.flags),)
include $(wildcard .*.flags)
endif
