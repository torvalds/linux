# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_NTFS_FS) += ntfs.o

ntfs-y := aops.o attrib.o collate.o dir.o file.o index.o inode.o \
	  mft.o mst.o namei.o runlist.o super.o unistr.o attrlist.o ea.o \
	  upcase.o bitmap.o lcnalloc.o logfile.o reparse.o compress.o \
	  iomap.o debug.o sysctl.o quota.o object_id.o bdev-io.o

ccflags-$(CONFIG_NTFS_DEBUG) += -DDEBUG
