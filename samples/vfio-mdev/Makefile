#
# Makefile for mtty.c file
#
KERNEL_DIR:=/lib/modules/$(shell uname -r)/build

obj-m:=mtty.o

modules clean modules_install:
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(PWD) $@

default: modules

module: modules
