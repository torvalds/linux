# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_CRYPTO_CHACHA_MIPS) += chacha-mips.o
chacha-mips-y := chacha-core.o chacha-glue.o
AFLAGS_chacha-core.o += -O2 # needed to fill branch delay slots
