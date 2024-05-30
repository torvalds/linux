# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux isofs filesystem routines.
#

obj-$(CONFIG_ISO9660_FS) += isofs.o

isofs-y 		:= namei.o inode.o dir.o util.o rock.o export.o
isofs-$(CONFIG_JOLIET)	+= joliet.o
isofs-$(CONFIG_ZISOFS)	+= compress.o
