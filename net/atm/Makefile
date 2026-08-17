# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the ATM Protocol Families.
#

atm-y		:= pvc.o ioctl.o common.o atm_misc.o raw.o resources.o atm_sysfs.o

obj-$(CONFIG_ATM) += atm.o
obj-$(CONFIG_ATM_BR2684) += br2684.o
atm-$(CONFIG_PROC_FS) += proc.o

obj-$(CONFIG_PPPOATM) += pppoatm.o
