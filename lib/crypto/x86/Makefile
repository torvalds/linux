# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_CRYPTO_BLAKE2S_X86) += libblake2s-x86_64.o
libblake2s-x86_64-y := blake2s-core.o blake2s-glue.o

obj-$(CONFIG_CRYPTO_CHACHA20_X86_64) += chacha-x86_64.o
chacha-x86_64-y := chacha-avx2-x86_64.o chacha-ssse3-x86_64.o chacha-avx512vl-x86_64.o chacha_glue.o
