#
# Makefile for CEPH filesystem.
#

ifneq ($(KERNELRELEASE),)

obj-$(CONFIG_CEPH_FS) += ceph.o

ceph-objs := super.o inode.o dir.o file.o locks.o addr.o ioctl.o \
	export.o caps.o snap.o xattr.o \
	messenger.o msgpool.o buffer.o pagelist.o \
	mds_client.o mdsmap.o \
	mon_client.o \
	osd_client.o osdmap.o crush/crush.o crush/mapper.o crush/hash.o \
	debugfs.o \
	auth.o auth_none.o \
	crypto.o armor.o \
	auth_x.o \
	ceph_fs.o ceph_strings.o ceph_hash.o ceph_frag.o

else
#Otherwise we were called directly from the command
# line; invoke the kernel build system.

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default: all

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) CONFIG_CEPH_FS=m modules

modules_install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) CONFIG_CEPH_FS=m modules_install

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

endif
