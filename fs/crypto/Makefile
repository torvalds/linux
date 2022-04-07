# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_FS_ENCRYPTION)	+= fscrypto.o

ccflags-y += -DDEFAULT_SYMBOL_NAMESPACE=ANDROID_GKI_VFS_EXPORT_ONLY

fscrypto-y := crypto.o \
	      fname.o \
	      hkdf.o \
	      hooks.o \
	      keyring.o \
	      keysetup.o \
	      keysetup_v1.o \
	      policy.o

fscrypto-$(CONFIG_BLOCK) += bio.o
fscrypto-$(CONFIG_FS_ENCRYPTION_INLINE_CRYPT) += inline_crypt.o
