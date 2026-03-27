# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_XOR_BLOCKS)	+= xor.o

xor-y				+= xor-core.o
xor-y				+= xor-8regs.o
xor-y				+= xor-32regs.o
xor-y				+= xor-8regs-prefetch.o
xor-y				+= xor-32regs-prefetch.o

xor-$(CONFIG_ALPHA)		+= alpha/xor.o
