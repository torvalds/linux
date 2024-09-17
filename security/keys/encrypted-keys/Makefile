# SPDX-License-Identifier: GPL-2.0
#
# Makefile for encrypted keys
#

obj-$(CONFIG_ENCRYPTED_KEYS) += encrypted-keys.o

encrypted-keys-y := encrypted.o ecryptfs_format.o
masterkey-$(CONFIG_TRUSTED_KEYS) := masterkey_trusted.o
masterkey-$(CONFIG_TRUSTED_KEYS)-$(CONFIG_ENCRYPTED_KEYS) := masterkey_trusted.o
encrypted-keys-y += $(masterkey-y) $(masterkey-m-m)
