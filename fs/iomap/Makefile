# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright (c) 2019 Oracle.
# All Rights Reserved.
#

ccflags-y += -I $(srctree)/$(src)		# needed for trace events

obj-$(CONFIG_FS_IOMAP)		+= iomap.o

iomap-y				+= trace.o \
				   buffered-io.o \
				   direct-io.o \
				   fiemap.o \
				   iter.o \
				   seek.o
iomap-$(CONFIG_SWAP)		+= swapfile.o
