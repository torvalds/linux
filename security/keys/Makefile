# SPDX-License-Identifier: GPL-2.0
#
# Makefile for key management
#

#
# Core
#
obj-y := \
	gc.o \
	key.o \
	keyring.o \
	keyctl.o \
	permission.o \
	process_keys.o \
	request_key.o \
	request_key_auth.o \
	user_defined.o
compat-obj-$(CONFIG_KEY_DH_OPERATIONS) += compat_dh.o
obj-$(CONFIG_KEYS_COMPAT) += compat.o $(compat-obj-y)
obj-$(CONFIG_PROC_FS) += proc.o
obj-$(CONFIG_SYSCTL) += sysctl.o
obj-$(CONFIG_PERSISTENT_KEYRINGS) += persistent.o
obj-$(CONFIG_KEY_DH_OPERATIONS) += dh.o
obj-$(CONFIG_ASYMMETRIC_KEY_TYPE) += keyctl_pkey.o

#
# Key types
#
obj-$(CONFIG_BIG_KEYS) += big_key.o
obj-$(CONFIG_TRUSTED_KEYS) += trusted-keys/
obj-$(CONFIG_ENCRYPTED_KEYS) += encrypted-keys/
