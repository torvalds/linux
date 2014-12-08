#
# Makefile for the linux pstorefs routines.
#

obj-y += pstore.o

pstore-objs += inode.o platform.o
obj-$(CONFIG_PSTORE_FTRACE)	+= ftrace.o

obj-$(CONFIG_PSTORE_PMSG)	+= pmsg.o

ramoops-objs += ram.o ram_core.o
obj-$(CONFIG_PSTORE_RAM)	+= ramoops.o
