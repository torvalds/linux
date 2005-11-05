/*
 * linux/include/asm-arm/arch-cl7500/memory.h
 *
 * Copyright (c) 1996,1997,1998 Russell King.
 *
 * Changelog:
 *  20-Oct-1996	RMK	Created
 *  31-Dec-1997	RMK	Fixed definitions to reduce warnings
 *  11-Jan-1998	RMK	Uninlined to reduce hits on cache
 *  08-Feb-1998	RMK	Added __virt_to_bus and __bus_to_virt
 *  21-Mar-1999	RMK	Renamed to memory.h
 *		RMK	Added TASK_SIZE and PAGE_OFFSET
 */
#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/*
 * Physical DRAM offset.
 */
#define PHYS_OFFSET	UL(0x10000000)

/*
 * These are exactly the same on the RiscPC as the
 * physical memory view.
 */
#define __virt_to_bus(x) __virt_to_phys(x)
#define __bus_to_virt(x) __phys_to_virt(x)

#endif
