#
# Makefile for the linux smb-filesystem routines.
#

obj-$(CONFIG_SMB_FS) += smbfs.o

smbfs-objs := proc.o dir.o cache.o sock.o inode.o file.o ioctl.o getopt.o \
		symlink.o smbiod.o request.o

# If you want debugging output, you may add these flags to the EXTRA_CFLAGS
# SMBFS_PARANOIA should normally be enabled.

EXTRA_CFLAGS += -DSMBFS_PARANOIA
#EXTRA_CFLAGS += -DSMBFS_DEBUG
#EXTRA_CFLAGS += -DSMBFS_DEBUG_VERBOSE
#EXTRA_CFLAGS += -DDEBUG_SMB_TIMESTAMP
#EXTRA_CFLAGS += -Werror

