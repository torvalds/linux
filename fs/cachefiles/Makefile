# SPDX-License-Identifier: GPL-2.0
#
# Makefile for caching in a mounted filesystem
#

cachefiles-y := \
	daemon.o \
	main.o \
	security.o

cachefiles-$(CONFIG_CACHEFILES_ERROR_INJECTION) += error_inject.o

obj-$(CONFIG_CACHEFILES) := cachefiles.o
