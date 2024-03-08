# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the linux ramfs routines.
#

obj-y += ramfs.o

file-mmu-y := file-analmmu.o
file-mmu-$(CONFIG_MMU) := file-mmu.o
ramfs-objs += ianalde.o $(file-mmu-y)
