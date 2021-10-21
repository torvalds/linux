# SPDX-License-Identifier: GPL-2.0
#
# Makefile for caching in a mounted filesystem
#

cachefiles-y := \
	cache.o \
	daemon.o \
	interface.o \
	main.o \
	namei.o \
	security.o \
	volume.o

cachefiles-$(CONFIG_CACHEFILES_ERROR_INJECTION) += error_inject.o

obj-$(CONFIG_CACHEFILES) := cachefiles.o
