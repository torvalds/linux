# SPDX-License-Identifier: GPL-2.0-only

EROFS_VERSION = "1.0"

ccflags-y += -DEROFS_VERSION=\"$(EROFS_VERSION)\"

obj-$(CONFIG_EROFS_FS) += erofs.o
erofs-objs := super.o inode.o data.o namei.o dir.o utils.o
erofs-$(CONFIG_EROFS_FS_XATTR) += xattr.o
erofs-$(CONFIG_EROFS_FS_ZIP) += decompressor.o zmap.o zdata.o

