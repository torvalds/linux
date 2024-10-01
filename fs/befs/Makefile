# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the linux BeOS filesystem routines.
#
 
obj-$(CONFIG_BEFS_FS) += befs.o
ccflags-$(CONFIG_BEFS_DEBUG)    += -DDEBUG
befs-objs := datastream.o btree.o super.o inode.o debug.o io.o linuxvfs.o
