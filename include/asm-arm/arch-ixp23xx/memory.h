/*
 * include/asm-arm/arch-ixp23xx/memory.h
 *
 * Copyright (c) 2003-2004 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

#include <asm/hardware.h>

/*
 * Physical DRAM offset.
 */
#define PHYS_OFFSET		(0x00000000)


/*
 * Virtual view <-> DMA view memory address translations
 * virt_to_bus: Used to translate the virtual address to an
 *		address suitable to be passed to set_dma_addr
 * bus_to_virt: Used to convert an address for DMA operations
 *		to an address that the kernel can use.
 */
#ifndef __ASSEMBLY__
#include <asm/mach-types.h>

#define __virt_to_bus(v)						\
	({ unsigned int ret;						\
	ret = ((__virt_to_phys(v) - 0x00000000) +			\
	 (*((volatile int *)IXP23XX_PCI_SDRAM_BAR) & 0xfffffff0)); 	\
	ret; })

#define __bus_to_virt(b)						\
	({ unsigned int data;						\
	data = *((volatile int *)IXP23XX_PCI_SDRAM_BAR);		\
	 __phys_to_virt((((b - (data & 0xfffffff0)) + 0x00000000))); })

/*
 * Coherency support.  Only supported on A2 CPUs or on A1
 * systems that have the cache coherency workaround.
 */
static inline int __ixp23xx_arch_is_coherent(void)
{
	extern unsigned int processor_id;

	if (((processor_id & 15) >= 4) || machine_is_roadrunner())
		return 1;

	return 0;
}

#define arch_is_coherent()	__ixp23xx_arch_is_coherent()

#endif


#endif
