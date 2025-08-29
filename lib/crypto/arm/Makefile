# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_CRYPTO_BLAKE2S_ARM) += libblake2s-arm.o
libblake2s-arm-y := blake2s-core.o blake2s-glue.o

obj-$(CONFIG_CRYPTO_CHACHA20_NEON) += chacha-neon.o
chacha-neon-y := chacha-scalar-core.o chacha-glue.o
chacha-neon-$(CONFIG_KERNEL_MODE_NEON) += chacha-neon-core.o
