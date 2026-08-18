// SPDX-License-Identifier: GPL-2.0
/*
 * sparse memory mappings.
 */
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/mmzone.h>
#include <linux/memblock.h>
#include <linux/compiler.h>
#include <linux/highmem.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/bootmem_info.h>
#include <linux/vmstat.h>
#include "internal.h"
#include <asm/dma.h>

/*
 * Permanent SPARSEMEM data:
 *
 * 1) mem_section	- memory sections, mem_map's for valid memory
 */
#ifdef CONFIG_SPARSEMEM_EXTREME
struct mem_section **mem_section;
#else
struct mem_section mem_section[NR_SECTION_ROOTS][SECTIONS_PER_ROOT]
	____cacheline_internodealigned_in_smp;
#endif
EXPORT_SYMBOL(mem_section);

#ifdef NODE_NOT_IN_PAGE_FLAGS
/*
 * If we did not store the node number in the page then we have to
 * do a lookup in the section_to_node_table in order to find which
 * node the page belongs to.
 */
#if MAX_NUMNODES <= 256
static u8 section_to_node_table[NR_MEM_SECTIONS] __cacheline_aligned;
#else
static u16 section_to_node_table[NR_MEM_SECTIONS] __cacheline_aligned;
#endif

int memdesc_nid(memdesc_flags_t mdf)
{
	return section_to_node_table[memdesc_section(mdf)];
}
EXPORT_SYMBOL(memdesc_nid);

static void set_section_nid(unsigned long section_nr, int nid)
{
	section_to_node_table[section_nr] = nid;
}
#else /* !NODE_NOT_IN_PAGE_FLAGS */
static inline void set_section_nid(unsigned long section_nr, int nid)
{
}
#endif

#ifdef CONFIG_SPARSEMEM_EXTREME
static noinline struct mem_section __ref *sparse_index_alloc(int nid)
{
	struct mem_section *section = NULL;
	unsigned long array_size = SECTIONS_PER_ROOT *
				   sizeof(struct mem_section);

	if (slab_is_available()) {
		section = kzalloc_node(array_size, GFP_KERNEL, nid);
	} else {
		section = memblock_alloc_node(array_size, SMP_CACHE_BYTES,
					      nid);
		if (!section)
			panic("%s: Failed to allocate %lu bytes nid=%d\n",
			      __func__, array_size, nid);
	}

	return section;
}

int __meminit sparse_index_init(unsigned long section_nr, int nid)
{
	unsigned long root = SECTION_NR_TO_ROOT(section_nr);
	struct mem_section *section;

	/*
	 * An existing section is possible in the sub-section hotplug
	 * case. First hot-add instantiates, follow-on hot-add reuses
	 * the existing section.
	 *
	 * The mem_hotplug_lock resolves the apparent race below.
	 */
	if (mem_section[root])
		return 0;

	section = sparse_index_alloc(nid);
	if (!section)
		return -ENOMEM;

	mem_section[root] = section;

	return 0;
}
#else /* !SPARSEMEM_EXTREME */
int sparse_index_init(unsigned long section_nr, int nid)
{
	return 0;
}
#endif

/*
 * During early boot, before section_mem_map is used for an actual
 * mem_map, we use section_mem_map to store the section's NUMA
 * node.  This keeps us from having to use another data structure.  The
 * node information is cleared just before we store the real mem_map.
 */
static inline unsigned long sparse_encode_early_nid(int nid)
{
	return ((unsigned long)nid << SECTION_NID_SHIFT);
}

static inline int sparse_early_nid(struct mem_section *section)
{
	return (section->section_mem_map >> SECTION_NID_SHIFT);
}

/* Validate the physical addressing limitations of the model */
static void __meminit mminit_validate_memmodel_limits(unsigned long *start_pfn,
						unsigned long *end_pfn)
{
	unsigned long max_sparsemem_pfn = (DIRECT_MAP_PHYSMEM_END + 1) >> PAGE_SHIFT;

	/*
	 * Sanity checks - do not allow an architecture to pass
	 * in larger pfns than the maximum scope of sparsemem:
	 */
	if (*start_pfn > max_sparsemem_pfn) {
		mminit_dprintk(MMINIT_WARNING, "pfnvalidation",
			"Start of range %lu -> %lu exceeds SPARSEMEM max %lu\n",
			*start_pfn, *end_pfn, max_sparsemem_pfn);
		WARN_ON_ONCE(1);
		*start_pfn = max_sparsemem_pfn;
		*end_pfn = max_sparsemem_pfn;
	} else if (*end_pfn > max_sparsemem_pfn) {
		mminit_dprintk(MMINIT_WARNING, "pfnvalidation",
			"End of range %lu -> %lu exceeds SPARSEMEM max %lu\n",
			*start_pfn, *end_pfn, max_sparsemem_pfn);
		WARN_ON_ONCE(1);
		*end_pfn = max_sparsemem_pfn;
	}
}

/*
 * There are a number of times that we loop over NR_MEM_SECTIONS,
 * looking for section_present() on each.  But, when we have very
 * large physical address spaces, NR_MEM_SECTIONS can also be
 * very large which makes the loops quite long.
 *
 * Keeping track of this gives us an easy way to break out of
 * those loops early.
 */
unsigned long __highest_present_section_nr;

static inline unsigned long first_present_section_nr(void)
{
	return next_present_section_nr(-1);
}

/* Record a memory area against a node. */
static void __init memory_present(int nid, unsigned long start, unsigned long end)
{
	unsigned long pfn;

	start &= PAGE_SECTION_MASK;
	mminit_validate_memmodel_limits(&start, &end);
	for (pfn = start; pfn < end; pfn += PAGES_PER_SECTION) {
		unsigned long section_nr = pfn_to_section_nr(pfn);
		struct mem_section *ms;

		sparse_index_init(section_nr, nid);
		set_section_nid(section_nr, nid);

		ms = __nr_to_section(section_nr);
		if (!ms->section_mem_map) {
			ms->section_mem_map = sparse_encode_early_nid(nid) |
							SECTION_IS_ONLINE;
			__section_mark_present(ms, section_nr);
		}
	}
}

/*
 * Mark all memblocks as present using memory_present().
 * This is a convenience function that is useful to mark all of the systems
 * memory as present during initialization.
 */
static void __init memblocks_present(void)
{
	unsigned long start, end;
	int i, nid;

#ifdef CONFIG_SPARSEMEM_EXTREME
	unsigned long size, align;

	size = sizeof(struct mem_section *) * NR_SECTION_ROOTS;
	align = 1 << (INTERNODE_CACHE_SHIFT);
	mem_section = memblock_alloc_or_panic(size, align);
#endif

	for_each_mem_pfn_range(i, MAX_NUMNODES, &start, &end, &nid)
		memory_present(nid, start, end);
}

static unsigned long usemap_size(void)
{
	return BITS_TO_LONGS(SECTION_BLOCKFLAGS_BITS) * sizeof(unsigned long);
}

size_t mem_section_usage_size(void)
{
	return sizeof(struct mem_section_usage) + usemap_size();
}

#ifdef CONFIG_SPARSEMEM_VMEMMAP
unsigned long __init section_map_size(void)
{
	return ALIGN(sizeof(struct page) * PAGES_PER_SECTION, PMD_SIZE);
}

#else
unsigned long __init section_map_size(void)
{
	return PAGE_ALIGN(sizeof(struct page) * PAGES_PER_SECTION);
}

struct page __init *__populate_section_memmap(unsigned long pfn,
		unsigned long nr_pages, int nid, struct vmem_altmap *altmap,
		struct dev_pagemap *pgmap)
{
	unsigned long size = section_map_size();
	struct page *map;
	phys_addr_t addr = __pa(MAX_DMA_ADDRESS);

	map = memmap_alloc(size, size, addr, nid, false);
	if (!map)
		panic("%s: Failed to allocate %lu bytes align=0x%lx nid=%d from=%pa\n",
		      __func__, size, PAGE_SIZE, nid, &addr);

	return map;
}
#endif /* !CONFIG_SPARSEMEM_VMEMMAP */

void __weak __meminit vmemmap_populate_print_last(void)
{
}

static void *sparse_usagebuf __meminitdata;
static void *sparse_usagebuf_end __meminitdata;

/*
 * Helper function that is used for generic section initialization, and
 * can also be used by any hooks added above.
 */
void __init sparse_init_early_section(int nid, struct page *map,
				      unsigned long pnum, unsigned long flags)
{
	BUG_ON(!sparse_usagebuf || sparse_usagebuf >= sparse_usagebuf_end);
	sparse_init_one_section(__nr_to_section(pnum), pnum, map,
			sparse_usagebuf, SECTION_IS_EARLY | flags);
	sparse_usagebuf = (void *)sparse_usagebuf + mem_section_usage_size();
}

static int __init sparse_usage_init(int nid, unsigned long map_count)
{
	unsigned long size;

	size = mem_section_usage_size() * map_count;
	sparse_usagebuf = memblock_alloc_node(size, SMP_CACHE_BYTES, nid);
	if (!sparse_usagebuf) {
		sparse_usagebuf_end = NULL;
		return -ENOMEM;
	}

	sparse_usagebuf_end = sparse_usagebuf + size;
	return 0;
}

static void __init sparse_usage_fini(void)
{
	sparse_usagebuf = sparse_usagebuf_end = NULL;
}

/*
 * Initialize sparse on a specific node. The node spans [pnum_begin, pnum_end)
 * And number of present sections in this node is map_count.
 */
static void __init sparse_init_nid(int nid, unsigned long pnum_begin,
				   unsigned long pnum_end,
				   unsigned long map_count)
{
	unsigned long pnum;
	struct page *map;
	struct mem_section *ms;

	if (sparse_usage_init(nid, map_count)) {
		pr_err("%s: node[%d] usemap allocation failed", __func__, nid);
		goto failed;
	}

	sparse_vmemmap_init_nid_early(nid);

	for_each_present_section_nr(pnum_begin, pnum) {
		unsigned long pfn = section_nr_to_pfn(pnum);

		if (pnum >= pnum_end)
			break;

		ms = __nr_to_section(pnum);
		if (!preinited_vmemmap_section(ms)) {
			map = __populate_section_memmap(pfn, PAGES_PER_SECTION,
					nid, NULL, NULL);
			if (!map) {
				pr_err("%s: node[%d] memory map backing failed. Some memory will not be available.",
				       __func__, nid);
				pnum_begin = pnum;
				sparse_usage_fini();
				goto failed;
			}
			memmap_boot_pages_add(DIV_ROUND_UP(PAGES_PER_SECTION * sizeof(struct page),
							   PAGE_SIZE));
			sparse_init_early_section(nid, map, pnum, 0);
		}
	}
	sparse_usage_fini();
	return;
failed:
	/*
	 * We failed to allocate, mark all the following pnums as not present,
	 * except the ones already initialized earlier.
	 */
	for_each_present_section_nr(pnum_begin, pnum) {
		if (pnum >= pnum_end)
			break;
		ms = __nr_to_section(pnum);
		if (!preinited_vmemmap_section(ms))
			ms->section_mem_map = 0;
	}
}

/*
 * Allocate the accumulated non-linear sections, allocate a mem_map
 * for each and record the physical to section mapping.
 */
void __init sparse_init(void)
{
	unsigned long pnum_end, pnum_begin, map_count = 1;
	int nid_begin;

	/* see include/linux/mmzone.h 'struct mem_section' definition */
	BUILD_BUG_ON(!is_power_of_2(sizeof(struct mem_section)));
	memblocks_present();

	if (compound_info_has_mask()) {
		VM_WARN_ON_ONCE(!IS_ALIGNED((unsigned long) pfn_to_page(0),
				    MAX_FOLIO_VMEMMAP_ALIGN));
	}

	pnum_begin = first_present_section_nr();
	nid_begin = sparse_early_nid(__nr_to_section(pnum_begin));

	/* Setup pageblock_order for HUGETLB_PAGE_SIZE_VARIABLE */
	set_pageblock_order();

	for_each_present_section_nr(pnum_begin + 1, pnum_end) {
		int nid = sparse_early_nid(__nr_to_section(pnum_end));

		if (nid == nid_begin) {
			map_count++;
			continue;
		}
		/* Init node with sections in range [pnum_begin, pnum_end) */
		sparse_init_nid(nid_begin, pnum_begin, pnum_end, map_count);
		nid_begin = nid;
		pnum_begin = pnum_end;
		map_count = 1;
	}
	/* cover the last node */
	sparse_init_nid(nid_begin, pnum_begin, pnum_end, map_count);
	vmemmap_populate_print_last();
}
