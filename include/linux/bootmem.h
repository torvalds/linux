/*
 * Discontiguous memory support, Kanoj Sarcar, SGI, Nov 1999
 */
#ifndef _LINUX_BOOTMEM_H
#define _LINUX_BOOTMEM_H

#include <asm/pgtable.h>
#include <asm/dma.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/mmzone.h>

/*
 *  simple boot-time physical memory area allocator.
 */

extern unsigned long max_low_pfn;
extern unsigned long min_low_pfn;

/*
 * highest page
 */
extern unsigned long max_pfn;

#ifdef CONFIG_CRASH_DUMP
extern unsigned long saved_max_pfn;
#endif

/*
 * node_bootmem_map is a map pointer - the bits represent all physical 
 * memory pages (including holes) on the node.
 */
typedef struct bootmem_data {
	unsigned long node_boot_start;
	unsigned long node_low_pfn;
	void *node_bootmem_map;
	unsigned long last_offset;
	unsigned long last_pos;
	unsigned long last_success;	/* Previous allocation point.  To speed
					 * up searching */
	struct list_head list;
} bootmem_data_t;

extern unsigned long __init bootmem_bootmap_pages (unsigned long);
extern unsigned long __init init_bootmem (unsigned long addr, unsigned long memend);
extern void __init free_bootmem (unsigned long addr, unsigned long size);
extern void * __init __alloc_bootmem (unsigned long size, unsigned long align, unsigned long goal);
extern void * __init __alloc_bootmem_nopanic (unsigned long size, unsigned long align, unsigned long goal);
extern void * __init __alloc_bootmem_low(unsigned long size,
					 unsigned long align,
					 unsigned long goal);
extern void * __init __alloc_bootmem_low_node(pg_data_t *pgdat,
					      unsigned long size,
					      unsigned long align,
					      unsigned long goal);
extern void * __init __alloc_bootmem_core(struct bootmem_data *bdata,
		unsigned long size, unsigned long align, unsigned long goal,
		unsigned long limit);
#ifndef CONFIG_HAVE_ARCH_BOOTMEM_NODE
extern void __init reserve_bootmem (unsigned long addr, unsigned long size);
#define alloc_bootmem(x) \
	__alloc_bootmem((x), SMP_CACHE_BYTES, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_low(x) \
	__alloc_bootmem_low((x), SMP_CACHE_BYTES, 0)
#define alloc_bootmem_pages(x) \
	__alloc_bootmem((x), PAGE_SIZE, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_low_pages(x) \
	__alloc_bootmem_low((x), PAGE_SIZE, 0)
#endif /* !CONFIG_HAVE_ARCH_BOOTMEM_NODE */
extern unsigned long __init free_all_bootmem (void);
extern void * __init __alloc_bootmem_node (pg_data_t *pgdat, unsigned long size, unsigned long align, unsigned long goal);
extern unsigned long __init init_bootmem_node (pg_data_t *pgdat, unsigned long freepfn, unsigned long startpfn, unsigned long endpfn);
extern void __init reserve_bootmem_node (pg_data_t *pgdat, unsigned long physaddr, unsigned long size);
extern void __init free_bootmem_node (pg_data_t *pgdat, unsigned long addr, unsigned long size);
extern unsigned long __init free_all_bootmem_node (pg_data_t *pgdat);
#ifndef CONFIG_HAVE_ARCH_BOOTMEM_NODE
#define alloc_bootmem_node(pgdat, x) \
	__alloc_bootmem_node((pgdat), (x), SMP_CACHE_BYTES, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_pages_node(pgdat, x) \
	__alloc_bootmem_node((pgdat), (x), PAGE_SIZE, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_low_pages_node(pgdat, x) \
	__alloc_bootmem_low_node((pgdat), (x), PAGE_SIZE, 0)
#endif /* !CONFIG_HAVE_ARCH_BOOTMEM_NODE */

#ifdef CONFIG_HAVE_ARCH_ALLOC_REMAP
extern void *alloc_remap(int nid, unsigned long size);
#else
static inline void *alloc_remap(int nid, unsigned long size)
{
	return NULL;
}
#endif

extern unsigned long __meminitdata nr_kernel_pages;
extern unsigned long nr_all_pages;

extern void *__init alloc_large_system_hash(const char *tablename,
					    unsigned long bucketsize,
					    unsigned long numentries,
					    int scale,
					    int flags,
					    unsigned int *_hash_shift,
					    unsigned int *_hash_mask,
					    unsigned long limit);

#define HASH_HIGHMEM	0x00000001	/* Consider highmem? */
#define HASH_EARLY	0x00000002	/* Allocating during early boot? */

/* Only NUMA needs hash distribution.
 * IA64 is known to have sufficient vmalloc space.
 */
#if defined(CONFIG_NUMA) && defined(CONFIG_IA64)
#define HASHDIST_DEFAULT 1
#else
#define HASHDIST_DEFAULT 0
#endif
extern int __initdata hashdist;		/* Distribute hashes across NUMA nodes? */


#endif /* _LINUX_BOOTMEM_H */
