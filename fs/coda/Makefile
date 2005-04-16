#
# Makefile for the Linux Coda filesystem routines.
#

obj-$(CONFIG_CODA_FS) += coda.o

coda-objs := psdev.o cache.o cnode.o inode.o dir.o file.o upcall.o \
	     coda_linux.o symlink.o pioctl.o sysctl.o 

# If you want debugging output, please uncomment the following line.

# EXTRA_CFLAGS += -DDEBUG -DDEBUG_SMB_MALLOC=1
