# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux fat filesystem support.
#

obj-$(CONFIG_FAT_FS) += fat.o
obj-$(CONFIG_VFAT_FS) += vfat.o
obj-$(CONFIG_MSDOS_FS) += msdos.o

fat-y := cache.o dir.o fatent.o file.o inode.o misc.o nfs.o
vfat-y := namei_vfat.o
msdos-y := namei_msdos.o
