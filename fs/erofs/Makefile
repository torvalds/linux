# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_EROFS_FS) += erofs.o
erofs-objs := super.o inode.o data.o namei.o dir.o sysfs.o
erofs-$(CONFIG_EROFS_FS_XATTR) += xattr.o
erofs-$(CONFIG_EROFS_FS_ZIP) += decompressor.o zmap.o zdata.o zutil.o
erofs-$(CONFIG_EROFS_FS_ZIP_LZMA) += decompressor_lzma.o
erofs-$(CONFIG_EROFS_FS_ONDEMAND) += fscache.o
