#
# Makefile for the Linux ufs filesystem routines.
#

obj-$(CONFIG_UFS_FS) += ufs.o

ufs-objs := balloc.o cylinder.o dir.o file.o ialloc.o inode.o \
	    namei.o super.o symlink.o truncate.o util.o
ccflags-$(CONFIG_UFS_DEBUG)    += -DDEBUG
