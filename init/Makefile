# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

ccflags-y := -fno-function-sections -fno-data-sections

obj-y                          := main.o version.o mounts.o
ifneq ($(CONFIG_BLK_DEV_INITRD),y)
obj-y                          += noinitramfs.o
else
obj-$(CONFIG_BLK_DEV_INITRD)   += initramfs.o
endif
obj-$(CONFIG_GENERIC_CALIBRATE_DELAY) += calibrate.o

obj-y                          += init_task.o

mounts-y			:= do_mounts.o
mounts-$(CONFIG_BLK_DEV_RAM)	+= do_mounts_rd.o
mounts-$(CONFIG_BLK_DEV_INITRD)	+= do_mounts_initrd.o

#
# UTS_VERSION
#

smp-flag-$(CONFIG_SMP)			:= SMP
preempt-flag-$(CONFIG_PREEMPT_BUILD)	:= PREEMPT
preempt-flag-$(CONFIG_PREEMPT_DYNAMIC)	:= PREEMPT_DYNAMIC
preempt-flag-$(CONFIG_PREEMPT_RT)	:= PREEMPT_RT

build-version = $(or $(KBUILD_BUILD_VERSION), $(build-version-auto))
build-timestamp = $(or $(KBUILD_BUILD_TIMESTAMP), $(build-timestamp-auto))

# Maximum length of UTS_VERSION is 64 chars
filechk_uts_version = \
	utsver=$$(echo '$(pound)'"$(build-version)" $(smp-flag-y) $(preempt-flag-y) "$(build-timestamp)" | cut -b -64); \
	echo '$(pound)'define UTS_VERSION \""$${utsver}"\"

#
# Build version.c with temporary UTS_VERSION
#

$(obj)/utsversion-tmp.h: FORCE
	$(call filechk,uts_version)

clean-files += utsversion-tmp.h

$(obj)/version.o: $(obj)/utsversion-tmp.h
CFLAGS_version.o := -include $(obj)/utsversion-tmp.h

#
# Build version-timestamp.c with final UTS_VERSION
#

include/generated/utsversion.h: build-version-auto = $(shell $(srctree)/$(src)/build-version)
include/generated/utsversion.h: build-timestamp-auto = $(shell LC_ALL=C date)
include/generated/utsversion.h: FORCE
	$(call filechk,uts_version)

$(obj)/version-timestamp.o: include/generated/utsversion.h
CFLAGS_version-timestamp.o := -include include/generated/utsversion.h
