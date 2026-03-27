# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_XOR_BLOCKS)	+= xor.o

xor-y				+= xor-core.o
xor-y				+= xor-8regs.o
xor-y				+= xor-32regs.o
xor-y				+= xor-8regs-prefetch.o
xor-y				+= xor-32regs-prefetch.o

xor-$(CONFIG_ALPHA)		+= alpha/xor.o
xor-$(CONFIG_ARM)		+= arm/xor.o
ifeq ($(CONFIG_ARM),y)
xor-$(CONFIG_KERNEL_MODE_NEON)	+= arm/xor-neon.o arm/xor-neon-glue.o
endif


CFLAGS_arm/xor-neon.o		+= $(CC_FLAGS_FPU)
CFLAGS_REMOVE_arm/xor-neon.o	+= $(CC_FLAGS_NO_FPU)
