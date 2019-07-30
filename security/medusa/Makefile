#
# Makefile for building the BASC module as part of the kernel tree.
#
#KBUILD_CFLAGS += -g -Wall

subdir-$(CONFIG_SECURITY_MEDUSA)	+= l0
subdir-$(CONFIG_SECURITY_MEDUSA)	+= l1
subdir-$(CONFIG_SECURITY_MEDUSA)        += l2
subdir-$(CONFIG_SECURITY_MEDUSA)        += l3

#obj-$(CONFIG_SECURITY_MEDUSA) += l0/
obj-$(CONFIG_SECURITY_MEDUSA) += l1/
obj-$(CONFIG_SECURITY_MEDUSA) += l2/
obj-$(CONFIG_SECURITY_MEDUSA) += l3/
obj-$(CONFIG_SECURITY_MEDUSA_L4_CONSTABLE) += l4-constable/

