/*
 *  linux/include/asm-sh/mmzone.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_SH_MMZONE_H
#define __ASM_SH_MMZONE_H

#include <linux/config.h>

#ifdef CONFIG_DISCONTIGMEM

/* Currently, just for HP690 */
#define PHYSADDR_TO_NID(phys)	((((phys) - __MEMORY_START) >= 0x01000000)?1:0)

extern pg_data_t discontig_page_data[MAX_NUMNODES];
extern bootmem_data_t discontig_node_bdata[MAX_NUMNODES];

/*
 * Following are macros that each numa implmentation must define.
 */

/*
 * Given a kernel address, find the home node of the underlying memory.
 */
#define KVADDR_TO_NID(kaddr)	PHYSADDR_TO_NID(__pa(kaddr))

/*
 * Return a pointer to the node data for node n.
 */
#define NODE_DATA(nid)		(&discontig_page_data[nid])

/*
 * NODE_MEM_MAP gives the kaddr for the mem_map of the node.
 */
#define NODE_MEM_MAP(nid)	(NODE_DATA(nid)->node_mem_map)

#define phys_to_page(phys)						\
({ unsigned int node = PHYSADDR_TO_NID(phys); 		      		\
   NODE_MEM_MAP(node)				 		 	\
     + (((phys) - NODE_DATA(node)->node_start_paddr) >> PAGE_SHIFT); })

static inline int is_valid_page(struct page *page)
{
	unsigned int i;

	for (i = 0; i < MAX_NUMNODES; i++) {
		if (page >= NODE_MEM_MAP(i) &&
		    page < NODE_MEM_MAP(i) + NODE_DATA(i)->node_size)
			return 1;
	}
	return 0;
}

#define VALID_PAGE(page)	is_valid_page(page)
#define page_to_phys(page)	PHYSADDR(page_address(page))

#endif /* CONFIG_DISCONTIGMEM */
#endif
