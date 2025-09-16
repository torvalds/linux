# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the FUSE filesystem.
#

# Needed for trace events
ccflags-y = -I$(src)

obj-$(CONFIG_FUSE_FS) += fuse.o
obj-$(CONFIG_CUSE) += cuse.o
obj-$(CONFIG_VIRTIO_FS) += virtiofs.o

fuse-y := trace.o	# put trace.o first so we see ftrace errors sooner
fuse-y += dev.o dir.o file.o inode.o control.o xattr.o acl.o readdir.o ioctl.o
fuse-y += iomode.o
fuse-$(CONFIG_FUSE_DAX) += dax.o
fuse-$(CONFIG_FUSE_PASSTHROUGH) += passthrough.o backing.o
fuse-$(CONFIG_SYSCTL) += sysctl.o
fuse-$(CONFIG_FUSE_IO_URING) += dev_uring.o

virtiofs-y := virtio_fs.o
