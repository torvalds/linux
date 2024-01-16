# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux isofs filesystem routines.
#

obj-$(CONFIG_ISO9660_FS) += isofs.o

isofs-objs-y 			:= namei.o inode.o dir.o util.o rock.o export.o
isofs-objs-$(CONFIG_JOLIET)	+= joliet.o
isofs-objs-$(CONFIG_ZISOFS)	+= compress.o
isofs-objs			:= $(isofs-objs-y)
