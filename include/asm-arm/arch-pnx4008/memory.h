/*
 * linux/include/asm-arm/arch-pnx4008/memory.h
 *
 * Copyright (c) 2005 Philips Semiconductors
 * Copyright (c) 2005 MontaVista Software, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/*
 * Physical DRAM offset.
 */
#define PHYS_OFFSET     (0x80000000)

#define __virt_to_bus(x) ((x) - PAGE_OFFSET + PHYS_OFFSET)
#define __bus_to_virt(x) ((x) + PAGE_OFFSET - PHYS_OFFSET)

#endif
