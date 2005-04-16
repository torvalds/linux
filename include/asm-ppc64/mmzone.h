/*
 * Written by Kanoj Sarcar (kanoj@sgi.com) Aug 99
 *
 * PowerPC64 port:
 * Copyright (C) 2002 Anton Blanchard, IBM Corp.
 */
#ifndef _ASM_MMZONE_H_
#define _ASM_MMZONE_H_

#include <linux/config.h>
#include <asm/smp.h>

#ifdef CONFIG_DISCONTIGMEM

extern struct pglist_data *node_data[];

/*
 * Following are specific to this numa platform.
 */

extern int numa_cpu_lookup_table[];
extern char *numa_memory_lookup_table;
extern cpumask_t numa_cpumask_lookup_table[];
extern int nr_cpus_in_node[];

/* 16MB regions */
#define MEMORY_INCREMENT_SHIFT 24
#define MEMORY_INCREMENT (1UL << MEMORY_INCREMENT_SHIFT)

/* NUMA debugging, will not work on a DLPAR machine */
#undef DEBUG_NUMA

static inline int pa_to_nid(unsigned long pa)
{
	int nid;

	nid = numa_memory_lookup_table[pa >> MEMORY_INCREMENT_SHIFT];

#ifdef DEBUG_NUMA
	/* the physical address passed in is not in the map for the system */
	if (nid == -1) {
		printk("bad address: %lx\n", pa);
		BUG();
	}
#endif

	return nid;
}

#define pfn_to_nid(pfn)		pa_to_nid((pfn) << PAGE_SHIFT)

/*
 * Return a pointer to the node data for node n.
 */
#define NODE_DATA(nid)		(node_data[nid])

#define node_localnr(pfn, nid)	((pfn) - NODE_DATA(nid)->node_start_pfn)

/*
 * Following are macros that each numa implmentation must define.
 */

/*
 * Given a kernel address, find the home node of the underlying memory.
 */
#define kvaddr_to_nid(kaddr)	pa_to_nid(__pa(kaddr))

#define node_mem_map(nid)	(NODE_DATA(nid)->node_mem_map)
#define node_start_pfn(nid)	(NODE_DATA(nid)->node_start_pfn)
#define node_end_pfn(nid)	(NODE_DATA(nid)->node_end_pfn)

#define local_mapnr(kvaddr) \
	( (__pa(kvaddr) >> PAGE_SHIFT) - node_start_pfn(kvaddr_to_nid(kvaddr)) 

/* Written this way to avoid evaluating arguments twice */
#define discontigmem_pfn_to_page(pfn) \
({ \
	unsigned long __tmp = pfn; \
	(node_mem_map(pfn_to_nid(__tmp)) + \
	 node_localnr(__tmp, pfn_to_nid(__tmp))); \
})

#define discontigmem_page_to_pfn(p) \
({ \
	struct page *__tmp = p; \
	(((__tmp) - page_zone(__tmp)->zone_mem_map) + \
	 page_zone(__tmp)->zone_start_pfn); \
})

/* XXX fix for discontiguous physical memory */
#define discontigmem_pfn_valid(pfn)		((pfn) < num_physpages)

#endif /* CONFIG_DISCONTIGMEM */
#endif /* _ASM_MMZONE_H_ */
