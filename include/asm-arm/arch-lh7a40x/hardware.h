/* include/asm-arm/arch-lh7a40x/hardware.h
 *
 *  Copyright (C) 2004 Coastal Environmental Systems
 *
 *  [ Substantially cribbed from include/asm-arm/arch-pxa/hardware.h ]
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <asm/sizes.h>		/* Added for the sake of amba-clcd driver */

#define io_p2v(x) (0xf0000000 | (((x) & 0xfff00000) >> 4) | ((x) & 0x0000ffff))
#define io_v2p(x) (             (((x) & 0x0fff0000) << 4) | ((x) & 0x0000ffff))

#ifdef __ASSEMBLY__

# define __REG(x)	io_p2v(x)
# define __PREG(x)	io_v2p(x)

#else

# if 0
#  define __REG(x)	(*((volatile u32 *)io_p2v(x)))
# else
/*
 * This __REG() version gives the same results as the one above,  except
 * that we are fooling gcc somehow so it generates far better and smaller
 * assembly code for access to contigous registers.  It's a shame that gcc
 * doesn't guess this by itself.
 */
#include <asm/types.h>
typedef struct { volatile u32 offset[4096]; } __regbase;
# define __REGP(x)	((__regbase *)((x)&~4095))->offset[((x)&4095)>>2]
# define __REG(x)	__REGP(io_p2v(x))
typedef struct { volatile u16 offset[4096]; } __regbase16;
# define __REGP16(x)	((__regbase16 *)((x)&~4095))->offset[((x)&4095)>>1]
# define __REG16(x)	__REGP16(io_p2v(x))
typedef struct { volatile u8 offset[4096]; } __regbase8;
# define __REGP8(x)	((__regbase8 *)((x)&~4095))->offset[(x)&4095]
# define __REG8(x)	__REGP8(io_p2v(x))
#endif

/* Let's kick gcc's ass again... */
# define __REG2(x,y)	\
	( __builtin_constant_p(y) ? (__REG((x) + (y))) \
				  : (*(volatile u32 *)((u32)&__REG(x) + (y))) )

# define __PREG(x)	(io_v2p((u32)&(x)))

#endif

#define MASK_AND_SET(v,m,s)	(v) = ((v)&~(m))|(s)

#include "registers.h"

#endif  /* _ASM_ARCH_HARDWARE_H */
