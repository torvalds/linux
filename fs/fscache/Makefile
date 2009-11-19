#
# Makefile for general filesystem caching code
#

fscache-y := \
	cache.o \
	cookie.o \
	fsdef.o \
	main.o \
	netfs.o \
	object.o \
	operation.o \
	page.o

fscache-$(CONFIG_PROC_FS) += proc.o
fscache-$(CONFIG_FSCACHE_STATS) += stats.o
fscache-$(CONFIG_FSCACHE_HISTOGRAM) += histogram.o
fscache-$(CONFIG_FSCACHE_OBJECT_LIST) += object-list.o

obj-$(CONFIG_FSCACHE) := fscache.o
