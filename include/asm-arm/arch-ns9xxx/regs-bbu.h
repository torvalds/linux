/*
 * include/asm-arm/arch-ns9xxx/regs-bbu.h
 *
 * Copyright (C) 2006 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef __ASM_ARCH_REGSBBU_H
#define __ASM_ARCH_REGSBBU_H

#include <asm/hardware.h>

/* BBus Utility */

/* GPIO Configuration Register */
#define BBU_GC(x)	__REG2(0x9060000c, (x))

#endif /* ifndef __ASM_ARCH_REGSBBU_H */
