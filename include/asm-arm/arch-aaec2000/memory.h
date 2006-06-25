/*
 *  linux/include/asm-arm/arch-aaec2000/memory.h
 *
 *  Copyright (c) 2005 Nicolas Bellido Y Ortega
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H


#define PHYS_OFFSET	UL(0xf0000000)

#define __virt_to_bus(x)	__virt_to_phys(x)
#define __bus_to_virt(x)	__phys_to_virt(x)

#ifdef CONFIG_DISCONTIGMEM

/*
 * The nodes are the followings:
 *
 *   node 0: 0xf000.0000 - 0xf3ff.ffff
 *   node 1: 0xf400.0000 - 0xf7ff.ffff
 *   node 2: 0xf800.0000 - 0xfbff.ffff
 *   node 3: 0xfc00.0000 - 0xffff.ffff
 */

/*
 * Given a kernel address, find the home node of the underlying memory.
 */
#define KVADDR_TO_NID(addr) \
	(((unsigned long)(addr) - PAGE_OFFSET) >> NODE_MAX_MEM_SHIFT)

/*
 * Given a page frame number, convert it to a node id.
 */
#define PFN_TO_NID(pfn) \
	(((pfn) - PHYS_PFN_OFFSET) >> (NODE_MAX_MEM_SHIFT - PAGE_SHIFT))

/*
 * Given a kaddr, ADDR_TO_MAPBASE finds the owning node of the memory
 * and return the mem_map of that node.
 */
#define ADDR_TO_MAPBASE(kaddr)  NODE_MEM_MAP(KVADDR_TO_NID(kaddr))

/*
 * Given a page frame number, find the owning node of the memory
 * and return the mem_map of that node.
 */
#define PFN_TO_MAPBASE(pfn)     NODE_MEM_MAP(PFN_TO_NID(pfn))

/*
 *  Given a kaddr, LOCAL_MEM_MAP finds the owning node of the memory
 *  and returns the index corresponding to the appropriate page in the
 *  node's mem_map.
 */
#define LOCAL_MAP_NR(addr) \
        (((unsigned long)(addr) & (NODE_MAX_MEM_SIZE - 1)) >> PAGE_SHIFT)

#define NODE_MAX_MEM_SHIFT	26
#define NODE_MAX_MEM_SIZE	(1 << NODE_MAX_MEM_SHIFT)

#endif /* CONFIG_DISCONTIGMEM */

#endif /* __ASM_ARCH_MEMORY_H */
