#
# Makefile for building the BASC module as part of the kernel tree.
#
#KBUILD_CFLAGS += -g -Wall

subdir-$(CONFIG_SECURITY_MEDUSA)	+= l1
subdir-$(CONFIG_SECURITY_MEDUSA)        += l2
subdir-$(CONFIG_SECURITY_MEDUSA)        += l3
subdir-$(CONFIG_SECURITY_MEDUSA)        += l4-constable

obj-$(CONFIG_SECURITY_MEDUSA) += l1/built-in.o
obj-$(CONFIG_SECURITY_MEDUSA) += l2/built-in.o
obj-$(CONFIG_SECURITY_MEDUSA) += l3/built-in.o
obj-$(CONFIG_SECURITY_MEDUSA) += l4-constable/built-in.o

