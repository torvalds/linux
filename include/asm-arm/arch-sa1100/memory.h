/*
 * linux/include/asm-arm/arch-sa1100/memory.h
 *
 * Copyright (C) 1999-2000 Nicolas Pitre <nico@cam.org>
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

#include <linux/config.h>
#include <asm/sizes.h>

/*
 * Physical DRAM offset is 0xc0000000 on the SA1100
 */
#define PHYS_OFFSET	(0xc0000000UL)

#ifndef __ASSEMBLY__

#ifdef CONFIG_SA1111
static inline void
__arch_adjust_zones(int node, unsigned long *size, unsigned long *holes)
{
	unsigned int sz = SZ_1M >> PAGE_SHIFT;

	if (node != 0)
		sz = 0;

	size[1] = size[0] - sz;
	size[0] = sz;
}

#define arch_adjust_zones(node, size, holes) \
	__arch_adjust_zones(node, size, holes)

#define ISA_DMA_THRESHOLD	(PHYS_OFFSET + SZ_1M - 1)

#endif
#endif

/*
 * Virtual view <-> DMA view memory address translations
 * virt_to_bus: Used to translate the virtual address to an
 *		address suitable to be passed to set_dma_addr
 * bus_to_virt: Used to convert an address for DMA operations
 *		to an address that the kernel can use.
 *
 * On the SA1100, bus addresses are equivalent to physical addresses.
 */
#define __virt_to_bus(x)	 __virt_to_phys(x)
#define __bus_to_virt(x)	 __phys_to_virt(x)

#ifdef CONFIG_DISCONTIGMEM
/*
 * Because of the wide memory address space between physical RAM banks on the 
 * SA1100, it's much convenient to use Linux's NUMA support to implement our 
 * memory map representation.  Assuming all memory nodes have equal access 
 * characteristics, we then have generic discontiguous memory support.
 *
 * Of course, all this isn't mandatory for SA1100 implementations with only
 * one used memory bank.  For those, simply undefine CONFIG_DISCONTIGMEM.
 *
 * The nodes are matched with the physical memory bank addresses which are 
 * incidentally the same as virtual addresses.
 * 
 * 	node 0:  0xc0000000 - 0xc7ffffff
 * 	node 1:  0xc8000000 - 0xcfffffff
 * 	node 2:  0xd0000000 - 0xd7ffffff
 * 	node 3:  0xd8000000 - 0xdfffffff
 */

/*
 * Given a kernel address, find the home node of the underlying memory.
 */
#define KVADDR_TO_NID(addr) (((unsigned long)(addr) - PAGE_OFFSET) >> 27)

/*
 * Given a page frame number, convert it to a node id.
 */
#define PFN_TO_NID(pfn)		(((pfn) - PHYS_PFN_OFFSET) >> (27 - PAGE_SHIFT))

/*
 * Given a kaddr, ADDR_TO_MAPBASE finds the owning node of the memory
 * and return the mem_map of that node.
 */
#define ADDR_TO_MAPBASE(kaddr)	NODE_MEM_MAP(KVADDR_TO_NID(kaddr))

/*
 * Given a page frame number, find the owning node of the memory
 * and return the mem_map of that node.
 */
#define PFN_TO_MAPBASE(pfn)	NODE_MEM_MAP(PFN_TO_NID(pfn))

/*
 * Given a kaddr, LOCAL_MEM_MAP finds the owning node of the memory
 * and returns the index corresponding to the appropriate page in the
 * node's mem_map.
 */
#define LOCAL_MAP_NR(addr) \
	(((unsigned long)(addr) & 0x07ffffff) >> PAGE_SHIFT)

#endif

#endif
