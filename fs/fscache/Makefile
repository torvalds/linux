# SPDX-License-Identifier: GPL-2.0
#
# Makefile for general filesystem caching code
#

fscache-y := \
	cache.o \
	main.o

fscache-$(CONFIG_PROC_FS) += proc.o
fscache-$(CONFIG_FSCACHE_STATS) += stats.o

obj-$(CONFIG_FSCACHE) := fscache.o
