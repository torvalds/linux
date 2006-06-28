/*
 * linux/include/asm-arm/arch-h720x/memory.h
 *
 * Copyright (c) 2000 Jungjun Kim
 *
 */
#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/*
 * Page offset:
 *    ( 0xc0000000UL )
 */
#define PHYS_OFFSET	UL(0x40000000)

/*
 * Virtual view <-> DMA view memory address translations
 * virt_to_bus: Used to translate the virtual address to an
 *              address suitable to be passed to set_dma_addr
 * bus_to_virt: Used to convert an address for DMA operations
 *              to an address that the kernel can use.
 *
 * There is something to do here later !, Mar 2000, Jungjun Kim
 */

#define __virt_to_bus(x)	__virt_to_phys(x)
#define __bus_to_virt(x)	__phys_to_virt(x)

#endif
