# SPDX-License-Identifier: GPL-2.0
#
# Makefile for general filesystem caching code
#

fscache-y := \
	cache.o \
	cookie.o \
	io.o \
	main.o \
	volume.o

fscache-$(CONFIG_PROC_FS) += proc.o
fscache-$(CONFIG_FSCACHE_STATS) += stats.o

obj-$(CONFIG_FSCACHE) := fscache.o
