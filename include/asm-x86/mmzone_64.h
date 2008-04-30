/* K8 NUMA support */
/* Copyright 2002,2003 by Andi Kleen, SuSE Labs */
/* 2.5 Version loosely based on the NUMAQ Code by Pat Gaughen. */
#ifndef _ASM_X86_64_MMZONE_H
#define _ASM_X86_64_MMZONE_H 1


#ifdef CONFIG_NUMA

#define VIRTUAL_BUG_ON(x)

#include <asm/smp.h>

/* Simple perfect hash to map physical addresses to node numbers */
struct memnode {
	int shift;
	unsigned int mapsize;
	s16 *map;
	s16 embedded_map[64 - 8];
} ____cacheline_aligned; /* total size = 128 bytes */
extern struct memnode memnode;
#define memnode_shift memnode.shift
#define memnodemap memnode.map
#define memnodemapsize memnode.mapsize

extern struct pglist_data *node_data[];

static inline __attribute__((pure)) int phys_to_nid(unsigned long addr)
{
	unsigned nid;
	VIRTUAL_BUG_ON(!memnodemap);
	VIRTUAL_BUG_ON((addr >> memnode_shift) >= memnodemapsize);
	nid = memnodemap[addr >> memnode_shift];
	VIRTUAL_BUG_ON(nid >= MAX_NUMNODES || !node_data[nid]);
	return nid;
}

#define NODE_DATA(nid)		(node_data[nid])

#define node_start_pfn(nid)	(NODE_DATA(nid)->node_start_pfn)
#define node_end_pfn(nid)       (NODE_DATA(nid)->node_start_pfn +	\
				 NODE_DATA(nid)->node_spanned_pages)

extern int early_pfn_to_nid(unsigned long pfn);

#ifdef CONFIG_NUMA_EMU
#define FAKE_NODE_MIN_SIZE	(64 * 1024 * 1024)
#define FAKE_NODE_MIN_HASH_MASK	(~(FAKE_NODE_MIN_SIZE - 1UL))
#endif

#endif
#endif
