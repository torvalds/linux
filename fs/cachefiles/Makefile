#
# Makefile for caching in a mounted filesystem
#

cachefiles-y := \
	bind.o \
	daemon.o \
	interface.o \
	key.o \
	main.o \
	namei.o \
	rdwr.o \
	security.o \
	xattr.o

cachefiles-$(CONFIG_CACHEFILES_HISTOGRAM) += proc.o

obj-$(CONFIG_CACHEFILES) := cachefiles.o
