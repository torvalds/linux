# SPDX-License-Identifier: GPL-2.0-or-newer
#
# Copyright (c) 2019 Oracle.
# All Rights Reserved.
#
obj-$(CONFIG_FS_IOMAP)		+= iomap.o

iomap-y				+= \
					fiemap.o \
					seek.o

iomap-$(CONFIG_SWAP)		+= swapfile.o
