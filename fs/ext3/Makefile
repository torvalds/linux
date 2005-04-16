#
# Makefile for the linux ext3-filesystem routines.
#

obj-$(CONFIG_EXT3_FS) += ext3.o

ext3-y	:= balloc.o bitmap.o dir.o file.o fsync.o ialloc.o inode.o \
	   ioctl.o namei.o super.o symlink.o hash.o resize.o

ext3-$(CONFIG_EXT3_FS_XATTR)	 += xattr.o xattr_user.o xattr_trusted.o
ext3-$(CONFIG_EXT3_FS_POSIX_ACL) += acl.o
ext3-$(CONFIG_EXT3_FS_SECURITY)	 += xattr_security.o
