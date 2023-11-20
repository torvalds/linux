# SPDX-License-Identifier: GPL-2.0

fscache-y := \
	fscache_cache.o \
	fscache_cookie.o \
	fscache_io.o \
	fscache_main.o \
	fscache_volume.o

fscache-$(CONFIG_PROC_FS) += fscache_proc.o
fscache-$(CONFIG_FSCACHE_STATS) += fscache_stats.o

obj-$(CONFIG_FSCACHE) := fscache.o

netfs-y := \
	buffered_read.o \
	io.o \
	iterator.o \
	main.o \
	objects.o

netfs-$(CONFIG_NETFS_STATS) += stats.o

obj-$(CONFIG_NETFS_SUPPORT) += netfs.o
