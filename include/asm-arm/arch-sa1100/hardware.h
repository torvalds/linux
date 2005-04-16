/*
 * linux/include/asm-arm/arch-sa1100/hardware.h
 *
 * Copyright (C) 1998 Nicolas Pitre <nico@cam.org>
 *
 * This file contains the hardware definitions for SA1100 architecture
 *
 * 2000/05/23 John Dorsey <john+@cs.cmu.edu>
 *      Definitions for SA1111 added.
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <linux/config.h>

/* Flushing areas */
#define FLUSH_BASE_PHYS		0xe0000000	/* SA1100 zero bank */
#define FLUSH_BASE		0xf5000000
#define FLUSH_BASE_MINICACHE	0xf5800000
#define UNCACHEABLE_ADDR	0xfa050000


/*
 * We requires absolute addresses i.e. (PCMCIA_IO_0_BASE + 0x3f8) for 
 * in*()/out*() macros to be usable for all cases.
 */
#define PCIO_BASE		0


/*
 * SA1100 internal I/O mappings
 *
 * We have the following mapping:
 *      phys            virt
 *      80000000        f8000000
 *      90000000        fa000000
 *      a0000000        fc000000
 *      b0000000        fe000000
 */

#define VIO_BASE        0xf8000000	/* virtual start of IO space */
#define VIO_SHIFT       3		/* x = IO space shrink power */
#define PIO_START       0x80000000	/* physical start of IO space */

#define io_p2v( x )             \
   ( (((x)&0x00ffffff) | (((x)&0x30000000)>>VIO_SHIFT)) + VIO_BASE )
#define io_v2p( x )             \
   ( (((x)&0x00ffffff) | (((x)&(0x30000000>>VIO_SHIFT))<<VIO_SHIFT)) + PIO_START )

#ifndef __ASSEMBLY__
#include <asm/types.h>

#if 0
# define __REG(x)	(*((volatile u32 *)io_p2v(x)))
#else
/*
 * This __REG() version gives the same results as the one above,  except
 * that we are fooling gcc somehow so it generates far better and smaller
 * assembly code for access to contigous registers.  It's a shame that gcc
 * doesn't guess this by itself.
 */
typedef struct { volatile u32 offset[4096]; } __regbase;
# define __REGP(x)	((__regbase *)((x)&~4095))->offset[((x)&4095)>>2]
# define __REG(x)	__REGP(io_p2v(x))
#endif

# define __PREG(x)	(io_v2p((u32)&(x)))

#else

# define __REG(x)	io_p2v(x)
# define __PREG(x)	io_v2p(x)

#endif

#include "SA-1100.h"

#ifdef CONFIG_SA1101
#include "SA-1101.h"
#endif

#endif  /* _ASM_ARCH_HARDWARE_H */
