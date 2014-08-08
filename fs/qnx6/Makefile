#
# Makefile for the linux qnx4-filesystem routines.
#

obj-$(CONFIG_QNX6FS_FS) += qnx6.o

qnx6-objs := inode.o dir.o namei.o super_mmi.o
ccflags-$(CONFIG_QNX6FS_DEBUG)	+= -DDEBUG
