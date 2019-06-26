# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_FS_ENCRYPTION)	+= fscrypto.o

fscrypto-y := crypto.o fname.o hooks.o keyinfo.o policy.o
fscrypto-$(CONFIG_BLOCK) += bio.o
