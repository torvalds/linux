#
# Makefile for CEPH filesystem.
#

ifneq ($(KERNELRELEASE),)

obj-$(CONFIG_CEPH_LIB) += libceph.o

libceph-objs := ceph_common.o messenger.o msgpool.o buffer.o pagelist.o \
	mon_client.o \
	osd_client.o osdmap.o crush/crush.o crush/mapper.o crush/hash.o \
	debugfs.o \
	auth.o auth_none.o \
	crypto.o armor.o \
	auth_x.o \
	ceph_fs.o ceph_strings.o ceph_hash.o \
	pagevec.o

else
#Otherwise we were called directly from the command
# line; invoke the kernel build system.

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default: all

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) CONFIG_CEPH_LIB=m modules

modules_install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) CONFIG_CEPH_LIB=m modules_install

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

endif
