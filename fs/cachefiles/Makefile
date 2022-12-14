# SPDX-License-Identifier: GPL-2.0
#
# Makefile for caching in a mounted filesystem
#

cachefiles-y := \
	cache.o \
	daemon.o \
	interface.o \
	io.o \
	key.o \
	main.o \
	namei.o \
	security.o \
	volume.o \
	xattr.o

cachefiles-$(CONFIG_CACHEFILES_ERROR_INJECTION) += error_inject.o
cachefiles-$(CONFIG_CACHEFILES_ONDEMAND) += ondemand.o

obj-$(CONFIG_CACHEFILES) := cachefiles.o
