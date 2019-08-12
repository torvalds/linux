# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright (c) 2019 Oracle.
# All Rights Reserved.
#
obj-$(CONFIG_FS_IOMAP)		+= iomap.o

iomap-y				+= \
					apply.o \
					buffered-io.o \
					direct-io.o \
					fiemap.o \
					seek.o

iomap-$(CONFIG_SWAP)		+= swapfile.o
