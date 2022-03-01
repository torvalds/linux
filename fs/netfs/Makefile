# SPDX-License-Identifier: GPL-2.0

netfs-y := \
	buffered_read.o \
	io.o \
	objects.o

netfs-$(CONFIG_NETFS_STATS) += stats.o

obj-$(CONFIG_NETFS_SUPPORT) := netfs.o
