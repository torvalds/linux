/* K8 NUMA support */
/* Copyright 2002,2003 by Andi Kleen, SuSE Labs */
/* 2.5 Version loosely based on the NUMAQ Code by Pat Gaughen. */
#ifndef _ASM_X86_64_MMZONE_H
#define _ASM_X86_64_MMZONE_H 1

#include <linux/config.h>

#ifdef CONFIG_DISCONTIGMEM

#define VIRTUAL_BUG_ON(x) 

#include <asm/smp.h>

#define NODEMAPSIZE 0xff

/* Simple perfect hash to map physical addresses to node numbers */
extern int memnode_shift; 
extern u8  memnodemap[NODEMAPSIZE]; 
extern int maxnode;

extern struct pglist_data *node_data[];

static inline __attribute__((pure)) int phys_to_nid(unsigned long addr) 
{ 
	int nid; 
	VIRTUAL_BUG_ON((addr >> memnode_shift) >= NODEMAPSIZE);
	nid = memnodemap[addr >> memnode_shift]; 
	VIRTUAL_BUG_ON(nid > maxnode); 
	return nid; 
} 

#define pfn_to_nid(pfn) phys_to_nid((unsigned long)(pfn) << PAGE_SHIFT)

#define kvaddr_to_nid(kaddr)	phys_to_nid(__pa(kaddr))
#define NODE_DATA(nid)		(node_data[nid])

#define node_mem_map(nid)	(NODE_DATA(nid)->node_mem_map)

#define node_mem_map(nid)	(NODE_DATA(nid)->node_mem_map)
#define node_start_pfn(nid)	(NODE_DATA(nid)->node_start_pfn)
#define node_end_pfn(nid)       (NODE_DATA(nid)->node_start_pfn + \
				 NODE_DATA(nid)->node_spanned_pages)

#define local_mapnr(kvaddr) \
	( (__pa(kvaddr) >> PAGE_SHIFT) - node_start_pfn(kvaddr_to_nid(kvaddr)) )

/* AK: this currently doesn't deal with invalid addresses. We'll see 
   if the 2.5 kernel doesn't pass them
   (2.4 used to). */
#define pfn_to_page(pfn) ({ \
	int nid = phys_to_nid(((unsigned long)(pfn)) << PAGE_SHIFT); 	\
	((pfn) - node_start_pfn(nid)) + node_mem_map(nid);		\
})

#define page_to_pfn(page) \
	(long)(((page) - page_zone(page)->zone_mem_map) + page_zone(page)->zone_start_pfn)

#define pfn_valid(pfn) ((pfn) >= num_physpages ? 0 : \
			({ u8 nid__ = pfn_to_nid(pfn); \
			   nid__ != 0xff && (pfn) >= node_start_pfn(nid__) && (pfn) <= node_end_pfn(nid__); }))
#endif
#endif
