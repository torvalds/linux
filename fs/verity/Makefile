# SPDX-License-Identifier: GPL-2.0

ccflags-y += -DDEFAULT_SYMBOL_NAMESPACE=ANDROID_GKI_VFS_EXPORT_ONLY

obj-$(CONFIG_FS_VERITY) += enable.o \
			   hash_algs.o \
			   init.o \
			   measure.o \
			   open.o \
			   read_metadata.o \
			   verify.o

obj-$(CONFIG_FS_VERITY_BUILTIN_SIGNATURES) += signature.o
