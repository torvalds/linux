#
# Makefile for the Linux /dev/pts virtual filesystem.
#

obj-$(CONFIG_UNIX98_PTYS)		+= devpts.o

devpts-$(CONFIG_UNIX98_PTYS)		:= inode.o
devpts-$(CONFIG_DEVPTS_FS_SECURITY)	+= xattr_security.o
