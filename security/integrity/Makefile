# SPDX-License-Identifier: GPL-2.0
#
# Makefile for caching inode integrity data (iint)
#

obj-$(CONFIG_INTEGRITY) += integrity.o

integrity-y := iint.o
integrity-$(CONFIG_INTEGRITY_AUDIT) += integrity_audit.o
integrity-$(CONFIG_INTEGRITY_SIGNATURE) += digsig.o
integrity-$(CONFIG_INTEGRITY_ASYMMETRIC_KEYS) += digsig_asymmetric.o

subdir-$(CONFIG_IMA)			+= ima
obj-$(CONFIG_IMA)			+= ima/
subdir-$(CONFIG_EVM)			+= evm
obj-$(CONFIG_EVM)			+= evm/
