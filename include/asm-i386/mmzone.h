/*
 * Written by Pat Gaughen (gone@us.ibm.com) Mar 2002
 *
 */

#ifndef _ASM_MMZONE_H_
#define _ASM_MMZONE_H_

#include <asm/smp.h>

#ifdef CONFIG_NUMA
extern struct pglist_data *node_data[];
#define NODE_DATA(nid)	(node_data[nid])

#ifdef CONFIG_X86_NUMAQ
	#include <asm/numaq.h>
#elif defined(CONFIG_ACPI_SRAT)/* summit or generic arch */
	#include <asm/srat.h>
#endif

extern int get_memcfg_numa_flat(void );
/*
 * This allows any one NUMA architecture to be compiled
 * for, and still fall back to the flat function if it
 * fails.
 */
static inline void get_memcfg_numa(void)
{
#ifdef CONFIG_X86_NUMAQ
	if (get_memcfg_numaq())
		return;
#elif defined(CONFIG_ACPI_SRAT)
	if (get_memcfg_from_srat())
		return;
#endif

	get_memcfg_numa_flat();
}

extern int early_pfn_to_nid(unsigned long pfn);
extern void numa_kva_reserve(void);

#else /* !CONFIG_NUMA */

#define get_memcfg_numa get_memcfg_numa_flat
#define get_zholes_size(n) (0)

static inline void numa_kva_reserve(void)
{
}
#endif /* CONFIG_NUMA */

#ifdef CONFIG_DISCONTIGMEM

/*
 * generic node memory support, the following assumptions apply:
 *
 * 1) memory comes in 256Mb contigious chunks which are either present or not
 * 2) we will not have more than 64Gb in total
 *
 * for now assume that 64Gb is max amount of RAM for whole system
 *    64Gb / 4096bytes/page = 16777216 pages
 */
#define MAX_NR_PAGES 16777216
#define MAX_ELEMENTS 256
#define PAGES_PER_ELEMENT (MAX_NR_PAGES/MAX_ELEMENTS)

extern s8 physnode_map[];

static inline int pfn_to_nid(unsigned long pfn)
{
#ifdef CONFIG_NUMA
	return((int) physnode_map[(pfn) / PAGES_PER_ELEMENT]);
#else
	return 0;
#endif
}

/*
 * Following are macros that each numa implmentation must define.
 */

#define node_start_pfn(nid)	(NODE_DATA(nid)->node_start_pfn)
#define node_end_pfn(nid)						\
({									\
	pg_data_t *__pgdat = NODE_DATA(nid);				\
	__pgdat->node_start_pfn + __pgdat->node_spanned_pages;		\
})

/* XXX: FIXME -- wli */
#define kern_addr_valid(kaddr)	(0)

#ifdef CONFIG_X86_NUMAQ            /* we have contiguous memory on NUMA-Q */
#define pfn_valid(pfn)          ((pfn) < num_physpages)
#else
static inline int pfn_valid(int pfn)
{
	int nid = pfn_to_nid(pfn);

	if (nid >= 0)
		return (pfn < node_end_pfn(nid));
	return 0;
}
#endif /* CONFIG_X86_NUMAQ */

#endif /* CONFIG_DISCONTIGMEM */

#ifdef CONFIG_NEED_MULTIPLE_NODES

/*
 * Following are macros that are specific to this numa platform.
 */
#define reserve_bootmem(addr, size) \
	reserve_bootmem_node(NODE_DATA(0), (addr), (size))
#define alloc_bootmem(x) \
	__alloc_bootmem_node(NODE_DATA(0), (x), SMP_CACHE_BYTES, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_low(x) \
	__alloc_bootmem_node(NODE_DATA(0), (x), SMP_CACHE_BYTES, 0)
#define alloc_bootmem_pages(x) \
	__alloc_bootmem_node(NODE_DATA(0), (x), PAGE_SIZE, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_low_pages(x) \
	__alloc_bootmem_node(NODE_DATA(0), (x), PAGE_SIZE, 0)
#define alloc_bootmem_node(pgdat, x)					\
({									\
	struct pglist_data  __maybe_unused			\
				*__alloc_bootmem_node__pgdat = (pgdat);	\
	__alloc_bootmem_node(NODE_DATA(0), (x), SMP_CACHE_BYTES,	\
						__pa(MAX_DMA_ADDRESS));	\
})
#define alloc_bootmem_pages_node(pgdat, x)				\
({									\
	struct pglist_data  __maybe_unused			\
				*__alloc_bootmem_node__pgdat = (pgdat);	\
	__alloc_bootmem_node(NODE_DATA(0), (x), PAGE_SIZE,		\
						__pa(MAX_DMA_ADDRESS))	\
})
#define alloc_bootmem_low_pages_node(pgdat, x)				\
({									\
	struct pglist_data  __maybe_unused			\
				*__alloc_bootmem_node__pgdat = (pgdat);	\
	__alloc_bootmem_node(NODE_DATA(0), (x), PAGE_SIZE, 0);		\
})
#endif /* CONFIG_NEED_MULTIPLE_NODES */

#endif /* _ASM_MMZONE_H_ */
