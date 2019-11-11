# SPDX-License-Identifier: GPL-2.0
#
# Makefile for caching inode integrity data (iint)
#

obj-$(CONFIG_INTEGRITY) += integrity.o

integrity-y := iint.o
integrity-$(CONFIG_INTEGRITY_AUDIT) += integrity_audit.o
integrity-$(CONFIG_INTEGRITY_SIGNATURE) += digsig.o
integrity-$(CONFIG_INTEGRITY_ASYMMETRIC_KEYS) += digsig_asymmetric.o
integrity-$(CONFIG_INTEGRITY_PLATFORM_KEYRING) += platform_certs/platform_keyring.o
integrity-$(CONFIG_LOAD_UEFI_KEYS) += platform_certs/efi_parser.o \
				      platform_certs/load_uefi.o \
				      platform_certs/keyring_handler.o
integrity-$(CONFIG_LOAD_IPL_KEYS) += platform_certs/load_ipl_s390.o

obj-$(CONFIG_IMA)			+= ima/
obj-$(CONFIG_EVM)			+= evm/
