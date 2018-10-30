/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Discontiguous memory support, Kanoj Sarcar, SGI, Nov 1999
 */
#ifndef _LINUX_BOOTMEM_H
#define _LINUX_BOOTMEM_H

#include <linux/mmzone.h>
#include <linux/mm_types.h>
#include <asm/dma.h>
#include <asm/processor.h>

/*
 *  simple boot-time physical memory area allocator.
 */

extern unsigned long max_low_pfn;
extern unsigned long min_low_pfn;

/*
 * highest page
 */
extern unsigned long max_pfn;
/*
 * highest possible page
 */
extern unsigned long long max_possible_pfn;

extern unsigned long memblock_free_all(void);
extern void reset_node_managed_pages(pg_data_t *pgdat);
extern void reset_all_zones_managed_pages(void);

/* We are using top down, so it is safe to use 0 here */
#define BOOTMEM_LOW_LIMIT 0

#ifndef ARCH_LOW_ADDRESS_LIMIT
#define ARCH_LOW_ADDRESS_LIMIT  0xffffffffUL
#endif

/* FIXME: use MEMBLOCK_ALLOC_* variants here */
#define BOOTMEM_ALLOC_ACCESSIBLE	0
#define BOOTMEM_ALLOC_ANYWHERE		(~(phys_addr_t)0)

/* FIXME: Move to memblock.h at a point where we remove nobootmem.c */
void *memblock_alloc_try_nid_raw(phys_addr_t size, phys_addr_t align,
				      phys_addr_t min_addr,
				      phys_addr_t max_addr, int nid);
void *memblock_alloc_try_nid_nopanic(phys_addr_t size,
		phys_addr_t align, phys_addr_t min_addr,
		phys_addr_t max_addr, int nid);
void *memblock_alloc_try_nid(phys_addr_t size, phys_addr_t align,
		phys_addr_t min_addr, phys_addr_t max_addr, int nid);
void __memblock_free_early(phys_addr_t base, phys_addr_t size);
void __memblock_free_late(phys_addr_t base, phys_addr_t size);

static inline void * __init memblock_alloc(
					phys_addr_t size,  phys_addr_t align)
{
	return memblock_alloc_try_nid(size, align, BOOTMEM_LOW_LIMIT,
					    BOOTMEM_ALLOC_ACCESSIBLE,
					    NUMA_NO_NODE);
}

static inline void * __init memblock_alloc_raw(
					phys_addr_t size,  phys_addr_t align)
{
	return memblock_alloc_try_nid_raw(size, align, BOOTMEM_LOW_LIMIT,
					    BOOTMEM_ALLOC_ACCESSIBLE,
					    NUMA_NO_NODE);
}

static inline void * __init memblock_alloc_from(
		phys_addr_t size, phys_addr_t align, phys_addr_t min_addr)
{
	return memblock_alloc_try_nid(size, align, min_addr,
				      BOOTMEM_ALLOC_ACCESSIBLE,
				      NUMA_NO_NODE);
}

static inline void * __init memblock_alloc_nopanic(
					phys_addr_t size, phys_addr_t align)
{
	return memblock_alloc_try_nid_nopanic(size, align,
						    BOOTMEM_LOW_LIMIT,
						    BOOTMEM_ALLOC_ACCESSIBLE,
						    NUMA_NO_NODE);
}

static inline void * __init memblock_alloc_low(
					phys_addr_t size, phys_addr_t align)
{
	return memblock_alloc_try_nid(size, align,
						   BOOTMEM_LOW_LIMIT,
						   ARCH_LOW_ADDRESS_LIMIT,
						   NUMA_NO_NODE);
}
static inline void * __init memblock_alloc_low_nopanic(
					phys_addr_t size, phys_addr_t align)
{
	return memblock_alloc_try_nid_nopanic(size, align,
						   BOOTMEM_LOW_LIMIT,
						   ARCH_LOW_ADDRESS_LIMIT,
						   NUMA_NO_NODE);
}

static inline void * __init memblock_alloc_from_nopanic(
		phys_addr_t size, phys_addr_t align, phys_addr_t min_addr)
{
	return memblock_alloc_try_nid_nopanic(size, align, min_addr,
						    BOOTMEM_ALLOC_ACCESSIBLE,
						    NUMA_NO_NODE);
}

static inline void * __init memblock_alloc_node(
		phys_addr_t size, phys_addr_t align, int nid)
{
	return memblock_alloc_try_nid(size, align, BOOTMEM_LOW_LIMIT,
					    BOOTMEM_ALLOC_ACCESSIBLE, nid);
}

static inline void * __init memblock_alloc_node_nopanic(
						phys_addr_t size, int nid)
{
	return memblock_alloc_try_nid_nopanic(size, 0, BOOTMEM_LOW_LIMIT,
						    BOOTMEM_ALLOC_ACCESSIBLE,
						    nid);
}

static inline void __init memblock_free_early(
					phys_addr_t base, phys_addr_t size)
{
	__memblock_free_early(base, size);
}

static inline void __init memblock_free_early_nid(
				phys_addr_t base, phys_addr_t size, int nid)
{
	__memblock_free_early(base, size);
}

static inline void __init memblock_free_late(
					phys_addr_t base, phys_addr_t size)
{
	__memblock_free_late(base, size);
}

extern void *alloc_large_system_hash(const char *tablename,
				     unsigned long bucketsize,
				     unsigned long numentries,
				     int scale,
				     int flags,
				     unsigned int *_hash_shift,
				     unsigned int *_hash_mask,
				     unsigned long low_limit,
				     unsigned long high_limit);

#define HASH_EARLY	0x00000001	/* Allocating during early boot? */
#define HASH_SMALL	0x00000002	/* sub-page allocation allowed, min
					 * shift passed via *_hash_shift */
#define HASH_ZERO	0x00000004	/* Zero allocated hash table */

/* Only NUMA needs hash distribution. 64bit NUMA architectures have
 * sufficient vmalloc space.
 */
#ifdef CONFIG_NUMA
#define HASHDIST_DEFAULT IS_ENABLED(CONFIG_64BIT)
extern int hashdist;		/* Distribute hashes across NUMA nodes? */
#else
#define hashdist (0)
#endif


#endif /* _LINUX_BOOTMEM_H */
