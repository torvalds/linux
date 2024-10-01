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

int page_to_nid(const struct page *page)
{
	return section_to_node_table[page_to_section(page)];
}
EXPORT_SYMBOL(page_to_nid);

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

static int __meminit sparse_index_init(unsigned long section_nr, int nid)
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
static inline int sparse_index_init(unsigned long section_nr, int nid)
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
	unsigned long max_sparsemem_pfn = (PHYSMEM_END + 1) >> PAGE_SHIFT;

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
static void __section_mark_present(struct mem_section *ms,
		unsigned long section_nr)
{
	if (section_nr > __highest_present_section_nr)
		__highest_present_section_nr = section_nr;

	ms->section_mem_map |= SECTION_MARKED_PRESENT;
}

#define for_each_present_section_nr(start, section_nr)		\
	for (section_nr = next_present_section_nr(start-1);	\
	     section_nr != -1;								\
	     section_nr = next_present_section_nr(section_nr))

static inline unsigned long first_present_section_nr(void)
{
	return next_present_section_nr(-1);
}

#ifdef CONFIG_SPARSEMEM_VMEMMAP
static void subsection_mask_set(unsigned long *map, unsigned long pfn,
		unsigned long nr_pages)
{
	int idx = subsection_map_index(pfn);
	int end = subsection_map_index(pfn + nr_pages - 1);

	bitmap_set(map, idx, end - idx + 1);
}

void __init subsection_map_init(unsigned long pfn, unsigned long nr_pages)
{
	int end_sec_nr = pfn_to_section_nr(pfn + nr_pages - 1);
	unsigned long nr, start_sec_nr = pfn_to_section_nr(pfn);

	for (nr = start_sec_nr; nr <= end_sec_nr; nr++) {
		struct mem_section *ms;
		unsigned long pfns;

		pfns = min(nr_pages, PAGES_PER_SECTION
				- (pfn & ~PAGE_SECTION_MASK));
		ms = __nr_to_section(nr);
		subsection_mask_set(ms->usage->subsection_map, pfn, pfns);

		pr_debug("%s: sec: %lu pfns: %lu set(%d, %d)\n", __func__, nr,
				pfns, subsection_map_index(pfn),
				subsection_map_index(pfn + pfns - 1));

		pfn += pfns;
		nr_pages -= pfns;
	}
}
#else
void __init subsection_map_init(unsigned long pfn, unsigned long nr_pages)
{
}
#endif

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
	if (unlikely(!mem_section)) {
		unsigned long size, align;

		size = sizeof(struct mem_section *) * NR_SECTION_ROOTS;
		align = 1 << (INTERNODE_CACHE_SHIFT);
		mem_section = memblock_alloc(size, align);
		if (!mem_section)
			panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
			      __func__, size, align);
	}
#endif

	for_each_mem_pfn_range(i, MAX_NUMNODES, &start, &end, &nid)
		memory_present(nid, start, end);
}

/*
 * Subtle, we encode the real pfn into the mem_map such that
 * the identity pfn - section_mem_map will return the actual
 * physical page frame number.
 */
static unsigned long sparse_encode_mem_map(struct page *mem_map, unsigned long pnum)
{
	unsigned long coded_mem_map =
		(unsigned long)(mem_map - (section_nr_to_pfn(pnum)));
	BUILD_BUG_ON(SECTION_MAP_LAST_BIT > PFN_SECTION_SHIFT);
	BUG_ON(coded_mem_map & ~SECTION_MAP_MASK);
	return coded_mem_map;
}

#ifdef CONFIG_MEMORY_HOTPLUG
/*
 * Decode mem_map from the coded memmap
 */
struct page *sparse_decode_mem_map(unsigned long coded_mem_map, unsigned long pnum)
{
	/* mask off the extra low bits of information */
	coded_mem_map &= SECTION_MAP_MASK;
	return ((struct page *)coded_mem_map) + section_nr_to_pfn(pnum);
}
#endif /* CONFIG_MEMORY_HOTPLUG */

static void __meminit sparse_init_one_section(struct mem_section *ms,
		unsigned long pnum, struct page *mem_map,
		struct mem_section_usage *usage, unsigned long flags)
{
	ms->section_mem_map &= ~SECTION_MAP_MASK;
	ms->section_mem_map |= sparse_encode_mem_map(mem_map, pnum)
		| SECTION_HAS_MEM_MAP | flags;
	ms->usage = usage;
}

static unsigned long usemap_size(void)
{
	return BITS_TO_LONGS(SECTION_BLOCKFLAGS_BITS) * sizeof(unsigned long);
}

size_t mem_section_usage_size(void)
{
	return sizeof(struct mem_section_usage) + usemap_size();
}

#ifdef CONFIG_MEMORY_HOTREMOVE
static inline phys_addr_t pgdat_to_phys(struct pglist_data *pgdat)
{
#ifndef CONFIG_NUMA
	VM_BUG_ON(pgdat != &contig_page_data);
	return __pa_symbol(&contig_page_data);
#else
	return __pa(pgdat);
#endif
}

static struct mem_section_usage * __init
sparse_early_usemaps_alloc_pgdat_section(struct pglist_data *pgdat,
					 unsigned long size)
{
	struct mem_section_usage *usage;
	unsigned long goal, limit;
	int nid;
	/*
	 * A page may contain usemaps for other sections preventing the
	 * page being freed and making a section unremovable while
	 * other sections referencing the usemap remain active. Similarly,
	 * a pgdat can prevent a section being removed. If section A
	 * contains a pgdat and section B contains the usemap, both
	 * sections become inter-dependent. This allocates usemaps
	 * from the same section as the pgdat where possible to avoid
	 * this problem.
	 */
	goal = pgdat_to_phys(pgdat) & (PAGE_SECTION_MASK << PAGE_SHIFT);
	limit = goal + (1UL << PA_SECTION_SHIFT);
	nid = early_pfn_to_nid(goal >> PAGE_SHIFT);
again:
	usage = memblock_alloc_try_nid(size, SMP_CACHE_BYTES, goal, limit, nid);
	if (!usage && limit) {
		limit = MEMBLOCK_ALLOC_ACCESSIBLE;
		goto again;
	}
	return usage;
}

static void __init check_usemap_section_nr(int nid,
		struct mem_section_usage *usage)
{
	unsigned long usemap_snr, pgdat_snr;
	static unsigned long old_usemap_snr;
	static unsigned long old_pgdat_snr;
	struct pglist_data *pgdat = NODE_DATA(nid);
	int usemap_nid;

	/* First call */
	if (!old_usemap_snr) {
		old_usemap_snr = NR_MEM_SECTIONS;
		old_pgdat_snr = NR_MEM_SECTIONS;
	}

	usemap_snr = pfn_to_section_nr(__pa(usage) >> PAGE_SHIFT);
	pgdat_snr = pfn_to_section_nr(pgdat_to_phys(pgdat) >> PAGE_SHIFT);
	if (usemap_snr == pgdat_snr)
		return;

	if (old_usemap_snr == usemap_snr && old_pgdat_snr == pgdat_snr)
		/* skip redundant message */
		return;

	old_usemap_snr = usemap_snr;
	old_pgdat_snr = pgdat_snr;

	usemap_nid = sparse_early_nid(__nr_to_section(usemap_snr));
	if (usemap_nid != nid) {
		pr_info("node %d must be removed before remove section %ld\n",
			nid, usemap_snr);
		return;
	}
	/*
	 * There is a circular dependency.
	 * Some platforms allow un-removable section because they will just
	 * gather other removable sections for dynamic partitioning.
	 * Just notify un-removable section's number here.
	 */
	pr_info("Section %ld and %ld (node %d) have a circular dependency on usemap and pgdat allocations\n",
		usemap_snr, pgdat_snr, nid);
}
#else
static struct mem_section_usage * __init
sparse_early_usemaps_alloc_pgdat_section(struct pglist_data *pgdat,
					 unsigned long size)
{
	return memblock_alloc_node(size, SMP_CACHE_BYTES, pgdat->node_id);
}

static void __init check_usemap_section_nr(int nid,
		struct mem_section_usage *usage)
{
}
#endif /* CONFIG_MEMORY_HOTREMOVE */

#ifdef CONFIG_SPARSEMEM_VMEMMAP
static unsigned long __init section_map_size(void)
{
	return ALIGN(sizeof(struct page) * PAGES_PER_SECTION, PMD_SIZE);
}

#else
static unsigned long __init section_map_size(void)
{
	return PAGE_ALIGN(sizeof(struct page) * PAGES_PER_SECTION);
}

struct page __init *__populate_section_memmap(unsigned long pfn,
		unsigned long nr_pages, int nid, struct vmem_altmap *altmap,
		struct dev_pagemap *pgmap)
{
	unsigned long size = section_map_size();
	struct page *map = sparse_buffer_alloc(size);
	phys_addr_t addr = __pa(MAX_DMA_ADDRESS);

	if (map)
		return map;

	map = memmap_alloc(size, size, addr, nid, false);
	if (!map)
		panic("%s: Failed to allocate %lu bytes align=0x%lx nid=%d from=%pa\n",
		      __func__, size, PAGE_SIZE, nid, &addr);

	return map;
}
#endif /* !CONFIG_SPARSEMEM_VMEMMAP */

static void *sparsemap_buf __meminitdata;
static void *sparsemap_buf_end __meminitdata;

static inline void __meminit sparse_buffer_free(unsigned long size)
{
	WARN_ON(!sparsemap_buf || size == 0);
	memblock_free(sparsemap_buf, size);
}

static void __init sparse_buffer_init(unsigned long size, int nid)
{
	phys_addr_t addr = __pa(MAX_DMA_ADDRESS);
	WARN_ON(sparsemap_buf);	/* forgot to call sparse_buffer_fini()? */
	/*
	 * Pre-allocated buffer is mainly used by __populate_section_memmap
	 * and we want it to be properly aligned to the section size - this is
	 * especially the case for VMEMMAP which maps memmap to PMDs
	 */
	sparsemap_buf = memmap_alloc(size, section_map_size(), addr, nid, true);
	sparsemap_buf_end = sparsemap_buf + size;
#ifndef CONFIG_SPARSEMEM_VMEMMAP
	memmap_boot_pages_add(DIV_ROUND_UP(size, PAGE_SIZE));
#endif
}

static void __init sparse_buffer_fini(void)
{
	unsigned long size = sparsemap_buf_end - sparsemap_buf;

	if (sparsemap_buf && size > 0)
		sparse_buffer_free(size);
	sparsemap_buf = NULL;
}

void * __meminit sparse_buffer_alloc(unsigned long size)
{
	void *ptr = NULL;

	if (sparsemap_buf) {
		ptr = (void *) roundup((unsigned long)sparsemap_buf, size);
		if (ptr + size > sparsemap_buf_end)
			ptr = NULL;
		else {
			/* Free redundant aligned space */
			if ((unsigned long)(ptr - sparsemap_buf) > 0)
				sparse_buffer_free((unsigned long)(ptr - sparsemap_buf));
			sparsemap_buf = ptr + size;
		}
	}
	return ptr;
}

void __weak __meminit vmemmap_populate_print_last(void)
{
}

/*
 * Initialize sparse on a specific node. The node spans [pnum_begin, pnum_end)
 * And number of present sections in this node is map_count.
 */
static void __init sparse_init_nid(int nid, unsigned long pnum_begin,
				   unsigned long pnum_end,
				   unsigned long map_count)
{
	struct mem_section_usage *usage;
	unsigned long pnum;
	struct page *map;

	usage = sparse_early_usemaps_alloc_pgdat_section(NODE_DATA(nid),
			mem_section_usage_size() * map_count);
	if (!usage) {
		pr_err("%s: node[%d] usemap allocation failed", __func__, nid);
		goto failed;
	}
	sparse_buffer_init(map_count * section_map_size(), nid);
	for_each_present_section_nr(pnum_begin, pnum) {
		unsigned long pfn = section_nr_to_pfn(pnum);

		if (pnum >= pnum_end)
			break;

		map = __populate_section_memmap(pfn, PAGES_PER_SECTION,
				nid, NULL, NULL);
		if (!map) {
			pr_err("%s: node[%d] memory map backing failed. Some memory will not be available.",
			       __func__, nid);
			pnum_begin = pnum;
			sparse_buffer_fini();
			goto failed;
		}
		check_usemap_section_nr(nid, usage);
		sparse_init_one_section(__nr_to_section(pnum), pnum, map, usage,
				SECTION_IS_EARLY);
		usage = (void *) usage + mem_section_usage_size();
	}
	sparse_buffer_fini();
	return;
failed:
	/* We failed to allocate, mark all the following pnums as not present */
	for_each_present_section_nr(pnum_begin, pnum) {
		struct mem_section *ms;

		if (pnum >= pnum_end)
			break;
		ms = __nr_to_section(pnum);
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

#ifdef CONFIG_MEMORY_HOTPLUG

/* Mark all memory sections within the pfn range as online */
void online_mem_sections(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long pfn;

	for (pfn = start_pfn; pfn < end_pfn; pfn += PAGES_PER_SECTION) {
		unsigned long section_nr = pfn_to_section_nr(pfn);
		struct mem_section *ms;

		/* onlining code should never touch invalid ranges */
		if (WARN_ON(!valid_section_nr(section_nr)))
			continue;

		ms = __nr_to_section(section_nr);
		ms->section_mem_map |= SECTION_IS_ONLINE;
	}
}

/* Mark all memory sections within the pfn range as offline */
void offline_mem_sections(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long pfn;

	for (pfn = start_pfn; pfn < end_pfn; pfn += PAGES_PER_SECTION) {
		unsigned long section_nr = pfn_to_section_nr(pfn);
		struct mem_section *ms;

		/*
		 * TODO this needs some double checking. Offlining code makes
		 * sure to check pfn_valid but those checks might be just bogus
		 */
		if (WARN_ON(!valid_section_nr(section_nr)))
			continue;

		ms = __nr_to_section(section_nr);
		ms->section_mem_map &= ~SECTION_IS_ONLINE;
	}
}

#ifdef CONFIG_SPARSEMEM_VMEMMAP
static struct page * __meminit populate_section_memmap(unsigned long pfn,
		unsigned long nr_pages, int nid, struct vmem_altmap *altmap,
		struct dev_pagemap *pgmap)
{
	return __populate_section_memmap(pfn, nr_pages, nid, altmap, pgmap);
}

static void depopulate_section_memmap(unsigned long pfn, unsigned long nr_pages,
		struct vmem_altmap *altmap)
{
	unsigned long start = (unsigned long) pfn_to_page(pfn);
	unsigned long end = start + nr_pages * sizeof(struct page);

	memmap_pages_add(-1L * (DIV_ROUND_UP(end - start, PAGE_SIZE)));
	vmemmap_free(start, end, altmap);
}
static void free_map_bootmem(struct page *memmap)
{
	unsigned long start = (unsigned long)memmap;
	unsigned long end = (unsigned long)(memmap + PAGES_PER_SECTION);

	vmemmap_free(start, end, NULL);
}

static int clear_subsection_map(unsigned long pfn, unsigned long nr_pages)
{
	DECLARE_BITMAP(map, SUBSECTIONS_PER_SECTION) = { 0 };
	DECLARE_BITMAP(tmp, SUBSECTIONS_PER_SECTION) = { 0 };
	struct mem_section *ms = __pfn_to_section(pfn);
	unsigned long *subsection_map = ms->usage
		? &ms->usage->subsection_map[0] : NULL;

	subsection_mask_set(map, pfn, nr_pages);
	if (subsection_map)
		bitmap_and(tmp, map, subsection_map, SUBSECTIONS_PER_SECTION);

	if (WARN(!subsection_map || !bitmap_equal(tmp, map, SUBSECTIONS_PER_SECTION),
				"section already deactivated (%#lx + %ld)\n",
				pfn, nr_pages))
		return -EINVAL;

	bitmap_xor(subsection_map, map, subsection_map, SUBSECTIONS_PER_SECTION);
	return 0;
}

static bool is_subsection_map_empty(struct mem_section *ms)
{
	return bitmap_empty(&ms->usage->subsection_map[0],
			    SUBSECTIONS_PER_SECTION);
}

static int fill_subsection_map(unsigned long pfn, unsigned long nr_pages)
{
	struct mem_section *ms = __pfn_to_section(pfn);
	DECLARE_BITMAP(map, SUBSECTIONS_PER_SECTION) = { 0 };
	unsigned long *subsection_map;
	int rc = 0;

	subsection_mask_set(map, pfn, nr_pages);

	subsection_map = &ms->usage->subsection_map[0];

	if (bitmap_empty(map, SUBSECTIONS_PER_SECTION))
		rc = -EINVAL;
	else if (bitmap_intersects(map, subsection_map, SUBSECTIONS_PER_SECTION))
		rc = -EEXIST;
	else
		bitmap_or(subsection_map, map, subsection_map,
				SUBSECTIONS_PER_SECTION);

	return rc;
}
#else
static struct page * __meminit populate_section_memmap(unsigned long pfn,
		unsigned long nr_pages, int nid, struct vmem_altmap *altmap,
		struct dev_pagemap *pgmap)
{
	return kvmalloc_node(array_size(sizeof(struct page),
					PAGES_PER_SECTION), GFP_KERNEL, nid);
}

static void depopulate_section_memmap(unsigned long pfn, unsigned long nr_pages,
		struct vmem_altmap *altmap)
{
	kvfree(pfn_to_page(pfn));
}

static void free_map_bootmem(struct page *memmap)
{
	unsigned long maps_section_nr, removing_section_nr, i;
	unsigned long magic, nr_pages;
	struct page *page = virt_to_page(memmap);

	nr_pages = PAGE_ALIGN(PAGES_PER_SECTION * sizeof(struct page))
		>> PAGE_SHIFT;

	for (i = 0; i < nr_pages; i++, page++) {
		magic = page->index;

		BUG_ON(magic == NODE_INFO);

		maps_section_nr = pfn_to_section_nr(page_to_pfn(page));
		removing_section_nr = page_private(page);

		/*
		 * When this function is called, the removing section is
		 * logical offlined state. This means all pages are isolated
		 * from page allocator. If removing section's memmap is placed
		 * on the same section, it must not be freed.
		 * If it is freed, page allocator may allocate it which will
		 * be removed physically soon.
		 */
		if (maps_section_nr != removing_section_nr)
			put_page_bootmem(page);
	}
}

static int clear_subsection_map(unsigned long pfn, unsigned long nr_pages)
{
	return 0;
}

static bool is_subsection_map_empty(struct mem_section *ms)
{
	return true;
}

static int fill_subsection_map(unsigned long pfn, unsigned long nr_pages)
{
	return 0;
}
#endif /* CONFIG_SPARSEMEM_VMEMMAP */

/*
 * To deactivate a memory region, there are 3 cases to handle across
 * two configurations (SPARSEMEM_VMEMMAP={y,n}):
 *
 * 1. deactivation of a partial hot-added section (only possible in
 *    the SPARSEMEM_VMEMMAP=y case).
 *      a) section was present at memory init.
 *      b) section was hot-added post memory init.
 * 2. deactivation of a complete hot-added section.
 * 3. deactivation of a complete section from memory init.
 *
 * For 1, when subsection_map does not empty we will not be freeing the
 * usage map, but still need to free the vmemmap range.
 *
 * For 2 and 3, the SPARSEMEM_VMEMMAP={y,n} cases are unified
 */
static void section_deactivate(unsigned long pfn, unsigned long nr_pages,
		struct vmem_altmap *altmap)
{
	struct mem_section *ms = __pfn_to_section(pfn);
	bool section_is_early = early_section(ms);
	struct page *memmap = NULL;
	bool empty;

	if (clear_subsection_map(pfn, nr_pages))
		return;

	empty = is_subsection_map_empty(ms);
	if (empty) {
		unsigned long section_nr = pfn_to_section_nr(pfn);

		/*
		 * Mark the section invalid so that valid_section()
		 * return false. This prevents code from dereferencing
		 * ms->usage array.
		 */
		ms->section_mem_map &= ~SECTION_HAS_MEM_MAP;

		/*
		 * When removing an early section, the usage map is kept (as the
		 * usage maps of other sections fall into the same page). It
		 * will be re-used when re-adding the section - which is then no
		 * longer an early section. If the usage map is PageReserved, it
		 * was allocated during boot.
		 */
		if (!PageReserved(virt_to_page(ms->usage))) {
			kfree_rcu(ms->usage, rcu);
			WRITE_ONCE(ms->usage, NULL);
		}
		memmap = sparse_decode_mem_map(ms->section_mem_map, section_nr);
	}

	/*
	 * The memmap of early sections is always fully populated. See
	 * section_activate() and pfn_valid() .
	 */
	if (!section_is_early)
		depopulate_section_memmap(pfn, nr_pages, altmap);
	else if (memmap)
		free_map_bootmem(memmap);

	if (empty)
		ms->section_mem_map = (unsigned long)NULL;
}

static struct page * __meminit section_activate(int nid, unsigned long pfn,
		unsigned long nr_pages, struct vmem_altmap *altmap,
		struct dev_pagemap *pgmap)
{
	struct mem_section *ms = __pfn_to_section(pfn);
	struct mem_section_usage *usage = NULL;
	struct page *memmap;
	int rc;

	if (!ms->usage) {
		usage = kzalloc(mem_section_usage_size(), GFP_KERNEL);
		if (!usage)
			return ERR_PTR(-ENOMEM);
		ms->usage = usage;
	}

	rc = fill_subsection_map(pfn, nr_pages);
	if (rc) {
		if (usage)
			ms->usage = NULL;
		kfree(usage);
		return ERR_PTR(rc);
	}

	/*
	 * The early init code does not consider partially populated
	 * initial sections, it simply assumes that memory will never be
	 * referenced.  If we hot-add memory into such a section then we
	 * do not need to populate the memmap and can simply reuse what
	 * is already there.
	 */
	if (nr_pages < PAGES_PER_SECTION && early_section(ms))
		return pfn_to_page(pfn);

	memmap = populate_section_memmap(pfn, nr_pages, nid, altmap, pgmap);
	if (!memmap) {
		section_deactivate(pfn, nr_pages, altmap);
		return ERR_PTR(-ENOMEM);
	}

	return memmap;
}

/**
 * sparse_add_section - add a memory section, or populate an existing one
 * @nid: The node to add section on
 * @start_pfn: start pfn of the memory range
 * @nr_pages: number of pfns to add in the section
 * @altmap: alternate pfns to allocate the memmap backing store
 * @pgmap: alternate compound page geometry for devmap mappings
 *
 * This is only intended for hotplug.
 *
 * Note that only VMEMMAP supports sub-section aligned hotplug,
 * the proper alignment and size are gated by check_pfn_span().
 *
 *
 * Return:
 * * 0		- On success.
 * * -EEXIST	- Section has been present.
 * * -ENOMEM	- Out of memory.
 */
int __meminit sparse_add_section(int nid, unsigned long start_pfn,
		unsigned long nr_pages, struct vmem_altmap *altmap,
		struct dev_pagemap *pgmap)
{
	unsigned long section_nr = pfn_to_section_nr(start_pfn);
	struct mem_section *ms;
	struct page *memmap;
	int ret;

	ret = sparse_index_init(section_nr, nid);
	if (ret < 0)
		return ret;

	memmap = section_activate(nid, start_pfn, nr_pages, altmap, pgmap);
	if (IS_ERR(memmap))
		return PTR_ERR(memmap);

	/*
	 * Poison uninitialized struct pages in order to catch invalid flags
	 * combinations.
	 */
	if (!altmap || !altmap->inaccessible)
		page_init_poison(memmap, sizeof(struct page) * nr_pages);

	ms = __nr_to_section(section_nr);
	set_section_nid(section_nr, nid);
	__section_mark_present(ms, section_nr);

	/* Align memmap to section boundary in the subsection case */
	if (section_nr_to_pfn(section_nr) != start_pfn)
		memmap = pfn_to_page(section_nr_to_pfn(section_nr));
	sparse_init_one_section(ms, section_nr, memmap, ms->usage, 0);

	return 0;
}

void sparse_remove_section(unsigned long pfn, unsigned long nr_pages,
			   struct vmem_altmap *altmap)
{
	struct mem_section *ms = __pfn_to_section(pfn);

	if (WARN_ON_ONCE(!valid_section(ms)))
		return;

	section_deactivate(pfn, nr_pages, altmap);
}
#endif /* CONFIG_MEMORY_HOTPLUG */
