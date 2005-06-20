/*
 *  linux/include/asm-arm/arch-aaec2000/hardware.h
 *
 *  Copyright (c) 2005 Nicolas Bellido Y Ortega
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <linux/config.h>

/* The kernel is loaded at physical address 0xf8000000.
 * We map the IO space a bit after
 */
#define PIO_APB_BASE	0x80000000
#define VIO_APB_BASE	0xf8000000
#define IO_APB_LENGTH	0x2000
#define PIO_AHB_BASE	0x80002000
#define VIO_AHB_BASE	0xf8002000
#define IO_AHB_LENGTH	0x2000

#define VIO_BASE    VIO_APB_BASE
#define PIO_BASE    PIO_APB_BASE

#define io_p2v(x) ( (x) - PIO_BASE + VIO_BASE )
#define io_v2p(x) ( (x) + PIO_BASE - VIO_BASE )

#ifndef __ASSEMBLY__

#include <asm/types.h>

/* FIXME: Is it needed to optimize this a la pxa ?? */
#define __REG(x)    (*((volatile u32 *)io_p2v(x)))
#define __PREG(x)   (io_v2p((u32)&(x)))

#else /* __ASSEMBLY__ */

#define __REG(x)    io_p2v(x)
#define __PREG(x)   io_v2p(x)

#endif

#include "aaec2000.h"

#endif /* __ASM_ARCH_HARDWARE_H */
