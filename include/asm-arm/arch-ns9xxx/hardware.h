/*
 * include/asm-arm/arch-ns9xxx/hardware.h
 *
 * Copyright (C) 2006 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <asm/memory.h>

/*
 * NetSilicon NS9xxx internal mapping:
 *
 * physical                <--> virtual
 * 0x90000000 - 0x906fffff <--> 0xf9000000 - 0xf96fffff
 * 0xa0100000 - 0xa0afffff <--> 0xfa100000 - 0xfaafffff
 */
#define io_p2v(x)	(0xf0000000 \
			 + (((x) & 0xf0000000) >> 4) \
			 + ((x) & 0x00ffffff))

#define io_v2p(x)	((((x) & 0x0f000000) << 4) \
			 + ((x) & 0x00ffffff))

#define __REGBIT(bit)		((u32)1 << (bit))
#define __REGBITS(hbit, lbit)	((((u32)1 << ((hbit) - (lbit) + 1)) - 1) << (lbit))
#define __REGVAL(mask, value)	(((value) * ((mask) & (-(mask))) & (mask)))

#ifndef __ASSEMBLY__

#  define __REG(x)	(*((volatile u32 *)io_p2v((x))))
#  define __REG2(x, y)	(*((volatile u32 *)io_p2v((x)) + (y)))

#  define __REGB(x)	(*((volatile u8 *)io_p2v((x))))
#  define __REGB2(x)	(*((volatile u8 *)io_p2v((x)) + (y)))

#  define REGSET(var, reg, field, value)				\
	((var) = (((var)						\
		   & ~(reg ## _ ## field & 				\
		       ~ reg ## _ ## field ## _ ## value))		\
		  | (reg ## _ ## field ## _ ## value)))

#  define REGSETIM(var, reg, field, value)				\
	((var) = (((var)						\
		   & ~(reg ## _ ## field & 				\
		       ~(__REGVAL(reg ## _ ## field, value))))		\
		  | (__REGVAL(reg ## _ ## field, value))))

#  define REGGET(reg, field)						\
	((reg & (reg ## _ ## field)) / (field & (-field)))

#else

#  define __REG(x)	io_p2v(x)
#  define __REG2(x, y)	io_p2v((x) + (y))

#  define __REGB(x)	__REG((x))
#  define __REGB2(x, y)	__REG2((x), (y))

#endif

#endif /* ifndef __ASM_ARCH_HARDWARE_H */
