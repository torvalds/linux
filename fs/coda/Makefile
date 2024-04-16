# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Linux Coda filesystem routines.
#

obj-$(CONFIG_CODA_FS) += coda.o

coda-objs := psdev.o cache.o cnode.o inode.o dir.o file.o upcall.o \
	     coda_linux.o symlink.o pioctl.o
coda-$(CONFIG_SYSCTL) += sysctl.o

# If you want debugging output, please uncomment the following line.

# ccflags-y := -DDEBUG -DDEBUG_SMB_MALLOC=1
