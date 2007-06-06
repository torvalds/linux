#ifndef __ASM_SH_MMZONE_H
#define __ASM_SH_MMZONE_H

#ifdef __KERNEL__

#ifdef CONFIG_NEED_MULTIPLE_NODES
extern struct pglist_data *node_data[];
#define NODE_DATA(nid)		(node_data[nid])

#define node_start_pfn(nid)	(NODE_DATA(nid)->node_start_pfn)
#define node_end_pfn(nid)	(NODE_DATA(nid)->node_start_pfn + \
				 NODE_DATA(nid)->node_spanned_pages)

static inline int pfn_to_nid(unsigned long pfn)
{
	int nid;

	for (nid = 0; nid < MAX_NUMNODES; nid++)
		if (pfn >= node_start_pfn(nid) && pfn <= node_end_pfn(nid))
			break;

	return nid;
}

static inline struct pglist_data *pfn_to_pgdat(unsigned long pfn)
{
	return NODE_DATA(pfn_to_nid(pfn));
}

/* arch/sh/mm/numa.c */
void __init setup_bootmem_node(int nid, unsigned long start, unsigned long end);
#else
static inline void
setup_bootmem_node(int nid, unsigned long start, unsigned long end)
{
}
#endif /* CONFIG_NEED_MULTIPLE_NODES */

/* Platform specific mem init */
void __init plat_mem_setup(void);

/* arch/sh/kernel/setup.c */
void __init setup_bootmem_allocator(unsigned long start_pfn);

#endif /* __KERNEL__ */
#endif /* __ASM_SH_MMZONE_H */
