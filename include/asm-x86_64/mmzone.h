/* K8 NUMA support */
/* Copyright 2002,2003 by Andi Kleen, SuSE Labs */
/* 2.5 Version loosely based on the NUMAQ Code by Pat Gaughen. */
#ifndef _ASM_X86_64_MMZONE_H
#define _ASM_X86_64_MMZONE_H 1

#include <linux/config.h>

#ifdef CONFIG_NUMA

#define VIRTUAL_BUG_ON(x) 

#include <asm/smp.h>

/* Should really switch to dynamic allocation at some point */
#define NODEMAPSIZE 0x4fff

/* Simple perfect hash to map physical addresses to node numbers */
struct memnode {
	int shift;
	u8 map[NODEMAPSIZE];
} ____cacheline_aligned;
extern struct memnode memnode;
#define memnode_shift memnode.shift
#define memnodemap memnode.map

extern struct pglist_data *node_data[];

static inline __attribute__((pure)) int phys_to_nid(unsigned long addr) 
{ 
	unsigned nid; 
	VIRTUAL_BUG_ON((addr >> memnode_shift) >= NODEMAPSIZE);
	nid = memnodemap[addr >> memnode_shift]; 
	VIRTUAL_BUG_ON(nid >= MAX_NUMNODES || !node_data[nid]); 
	return nid; 
} 

#define NODE_DATA(nid)		(node_data[nid])

#define node_start_pfn(nid)	(NODE_DATA(nid)->node_start_pfn)
#define node_end_pfn(nid)       (NODE_DATA(nid)->node_start_pfn + \
				 NODE_DATA(nid)->node_spanned_pages)

#ifdef CONFIG_DISCONTIGMEM
#define pfn_to_nid(pfn) phys_to_nid((unsigned long)(pfn) << PAGE_SHIFT)
#define kvaddr_to_nid(kaddr)	phys_to_nid(__pa(kaddr))

extern int pfn_valid(unsigned long pfn);
#endif

#endif
#endif
