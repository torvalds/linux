# SPDX-License-Identifier: GPL-2.0
#
# Makefile for io_uring

obj-$(CONFIG_IO_URING)		+= io_uring.o xattr.o nop.o fs.o splice.o \
					sync.o advise.o filetable.o \
					openclose.o uring_cmd.o
obj-$(CONFIG_IO_WQ)		+= io-wq.o
