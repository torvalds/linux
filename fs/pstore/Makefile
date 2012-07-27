#
# Makefile for the linux pstorefs routines.
#

obj-y += pstore.o

pstore-objs += inode.o platform.o

ramoops-objs += ram.o ram_core.o
obj-$(CONFIG_PSTORE_RAM)	+= ramoops.o
