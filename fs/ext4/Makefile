#
# Makefile for the linux ext4-filesystem routines.
#

obj-$(CONFIG_EXT4DEV_FS) += ext4dev.o

ext4dev-y	:= balloc.o bitmap.o dir.o file.o fsync.o ialloc.o inode.o \
		   ioctl.o namei.o super.o symlink.o hash.o resize.o extents.o \
		   ext4_jbd2.o

ext4dev-$(CONFIG_EXT4DEV_FS_XATTR)	+= xattr.o xattr_user.o xattr_trusted.o
ext4dev-$(CONFIG_EXT4DEV_FS_POSIX_ACL)	+= acl.o
ext4dev-$(CONFIG_EXT4DEV_FS_SECURITY)	+= xattr_security.o
