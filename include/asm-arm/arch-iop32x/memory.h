/*
 * include/asm-arm/arch-iop32x/memory.h
 */

#ifndef __MEMORY_H
#define __MEMORY_H

#include <asm/hardware.h>

/*
 * Physical DRAM offset.
 */
#define PHYS_OFFSET	UL(0xa0000000)

/*
 * Virtual view <-> PCI DMA view memory address translations
 * virt_to_bus: Used to translate the virtual address to an
 *		address suitable to be passed to set_dma_addr
 * bus_to_virt: Used to convert an address for DMA operations
 *		to an address that the kernel can use.
 */
#define __virt_to_bus(x)	(__virt_to_phys(x))
#define __bus_to_virt(x)	(__phys_to_virt(x))


#endif
