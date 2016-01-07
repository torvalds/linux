#
# Makefile for the Linux kernel part of Medusa DS9 Security System,
# L4/constable
#

export-objs :=  teleport_cycle teleport_reset

l4-constable-y:= teleport.o chardev.o
obj-$(CONFIG_SECURITY_MEDUSA_L4_CONSTABLE) := l4-constable.o

