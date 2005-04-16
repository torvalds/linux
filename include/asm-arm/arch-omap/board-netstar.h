/*
 * Copyright (C) 2004 2N Telekomunikace, Ladislav Michl <michl@2n.cz>
 *
 * Hardware definitions for OMAP5910 based NetStar board.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_NETSTAR_H
#define __ASM_ARCH_NETSTAR_H

#include <asm/arch/tc.h>

#define OMAP_NAND_FLASH_START1		OMAP_CS1_PHYS + (1 << 23)
#define OMAP_NAND_FLASH_START2		OMAP_CS1_PHYS + (2 << 23)

#endif /*  __ASM_ARCH_NETSTAR_H */
