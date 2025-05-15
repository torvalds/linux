# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright (c) 2019 Oracle.
# All Rights Reserved.
#

ccflags-y += -I $(src)		# needed for trace events

obj-$(CONFIG_FS_IOMAP)		+= iomap.o

iomap-y				+= trace.o \
				   iter.o
iomap-$(CONFIG_BLOCK)		+= buffered-io.o \
				   direct-io.o \
				   ioend.o \
				   fiemap.o \
				   seek.o
iomap-$(CONFIG_SWAP)		+= swapfile.o
