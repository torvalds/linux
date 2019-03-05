// SPDX-License-Identifier: GPL-2.0
/*
 * sparse memory mappings.
 */
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/mmzone.h>
#include <linux/bootmem.h>
#include <linux/compiler.h>
#include <linux/highmem.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>

#include "internal.h"
#include <asm/dma.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>

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

	if (slab_is_available())
		section = kzalloc_node(array_size, GFP_KERNEL, nid);
	else
		section = memblock_virt_alloc_node(array_size, nid);

	return section;
}

static int __meminit sparse_index_init(unsigned long section_nr, int nid)
{
	unsigned long root = SECTION_NR_TO_ROOT(section_nr);
	struct mem_section *section;

	if (mem_section[root])
		return -EEXIST;

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

#ifdef CONFIG_SPARSEMEM_EXTREME
int __section_nr(struct mem_section* ms)
{
	unsigned long root_nr;
	struct mem_section *root = NULL;

	for (root_nr = 0; root_nr < NR_SECTION_ROOTS; root_nr++) {
		root = __nr_to_section(root_nr * SECTIONS_PER_ROOT);
		if (!root)
			continue;

		if ((ms >= root) && (ms < (root + SECTIONS_PER_ROOT)))
		     break;
	}

	VM_BUG_ON(!root);

	return (root_nr * SECTIONS_PER_ROOT) + (ms - root);
}
#else
int __section_nr(struct mem_section* ms)
{
	return (int)(ms - mem_section[0]);
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
	return (nid << SECTION_NID_SHIFT);
}

static inline int sparse_early_nid(struct mem_section *section)
{
	return (section->section_mem_map >> SECTION_NID_SHIFT);
}

/* Validate the physical addressing limitations of the model */
void __meminit mminit_validate_memmodel_limits(unsigned long *start_pfn,
						unsigned long *end_pfn)
{
	unsigned long max_sparsemem_pfn = 1UL << (MAX_PHYSMEM_BITS-PAGE_SHIFT);

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
int __highest_present_section_nr;
static void section_mark_present(struct mem_section *ms)
{
	int section_nr = __section_nr(ms);

	if (section_nr > __highest_present_section_nr)
		__highest_present_section_nr = section_nr;

	ms->section_mem_map |= SECTION_MARKED_PRESENT;
}

static inline int next_present_section_nr(int section_nr)
{
	do {
		section_nr++;
		if (present_section_nr(section_nr))
			return section_nr;
	} while ((section_nr <= __highest_present_section_nr));

	return -1;
}
#define for_each_present_section_nr(start, section_nr)		\
	for (section_nr = next_present_section_nr(start-1);	\
	     ((section_nr != -1) &&				\
	      (section_nr <= __highest_present_section_nr));	\
	     section_nr = next_present_section_nr(section_nr))

static inline unsigned long first_present_section_nr(void)
{
	return next_present_section_nr(-1);
}

/* Record a memory area against a node. */
void __init memory_present(int nid, unsigned long start, unsigned long end)
{
	unsigned long pfn;

#ifdef CONFIG_SPARSEMEM_EXTREME
	if (unlikely(!mem_section)) {
		unsigned long size, align;

		size = sizeof(struct mem_section*) * NR_SECTION_ROOTS;
		align = 1 << (INTERNODE_CACHE_SHIFT);
		mem_section = memblock_virt_alloc(size, align);
	}
#endif

	start &= PAGE_SECTION_MASK;
	mminit_validate_memmodel_limits(&start, &end);
	for (pfn = start; pfn < end; pfn += PAGES_PER_SECTION) {
		unsigned long section = pfn_to_section_nr(pfn);
		struct mem_section *ms;

		sparse_index_init(section, nid);
		set_section_nid(section, nid);

		ms = __nr_to_section(section);
		if (!ms->section_mem_map) {
			ms->section_mem_map = sparse_encode_early_nid(nid) |
							SECTION_IS_ONLINE;
			section_mark_present(ms);
		}
	}
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
	BUILD_BUG_ON(SECTION_MAP_LAST_BIT > (1UL<<PFN_SECTION_SHIFT));
	BUG_ON(coded_mem_map & ~SECTION_MAP_MASK);
	return coded_mem_map;
}

/*
 * Decode mem_map from the coded memmap
 */
struct page *sparse_decode_mem_map(unsigned long coded_mem_map, unsigned long pnum)
{
	/* mask off the extra low bits of information */
	coded_mem_map &= SECTION_MAP_MASK;
	return ((struct page *)coded_mem_map) + section_nr_to_pfn(pnum);
}

static void __meminit sparse_init_one_section(struct mem_section *ms,
		unsigned long pnum, struct page *mem_map,
		unsigned long *pageblock_bitmap)
{
	ms->section_mem_map &= ~SECTION_MAP_MASK;
	ms->section_mem_map |= sparse_encode_mem_map(mem_map, pnum) |
							SECTION_HAS_MEM_MAP;
 	ms->pageblock_flags = pageblock_bitmap;
}

unsigned long usemap_size(void)
{
	return BITS_TO_LONGS(SECTION_BLOCKFLAGS_BITS) * sizeof(unsigned long);
}

#ifdef CONFIG_MEMORY_HOTPLUG
static unsigned long *__kmalloc_section_usemap(void)
{
	return kmalloc(usemap_size(), GFP_KERNEL);
}
#endif /* CONFIG_MEMORY_HOTPLUG */

#ifdef CONFIG_MEMORY_HOTREMOVE
static unsigned long * __init
sparse_early_usemaps_alloc_pgdat_section(struct pglist_data *pgdat,
					 unsigned long size)
{
	unsigned long goal, limit;
	unsigned long *p;
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
	goal = __pa(pgdat) & (PAGE_SECTION_MASK << PAGE_SHIFT);
	limit = goal + (1UL << PA_SECTION_SHIFT);
	nid = early_pfn_to_nid(goal >> PAGE_SHIFT);
again:
	p = memblock_virt_alloc_try_nid_nopanic(size,
						SMP_CACHE_BYTES, goal, limit,
						nid);
	if (!p && limit) {
		limit = 0;
		goto again;
	}
	return p;
}

static void __init check_usemap_section_nr(int nid, unsigned long *usemap)
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

	usemap_snr = pfn_to_section_nr(__pa(usemap) >> PAGE_SHIFT);
	pgdat_snr = pfn_to_section_nr(__pa(pgdat) >> PAGE_SHIFT);
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
static unsigned long * __init
sparse_early_usemaps_alloc_pgdat_section(struct pglist_data *pgdat,
					 unsigned long size)
{
	return memblock_virt_alloc_node_nopanic(size, pgdat->node_id);
}

static void __init check_usemap_section_nr(int nid, unsigned long *usemap)
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

struct page __init *sparse_mem_map_populate(unsigned long pnum, int nid,
		struct vmem_altmap *altmap)
{
	unsigned long size = section_map_size();
	struct page *map = sparse_buffer_alloc(size);

	if (map)
		return map;

	map = memblock_virt_alloc_try_nid(size,
					  PAGE_SIZE, __pa(MAX_DMA_ADDRESS),
					  BOOTMEM_ALLOC_ACCESSIBLE, nid);
	return map;
}
#endif /* !CONFIG_SPARSEMEM_VMEMMAP */

static void *sparsemap_buf __meminitdata;
static void *sparsemap_buf_end __meminitdata;

static void __init sparse_buffer_init(unsigned long size, int nid)
{
	WARN_ON(sparsemap_buf);	/* forgot to call sparse_buffer_fini()? */
	sparsemap_buf =
		memblock_virt_alloc_try_nid_raw(size, PAGE_SIZE,
						__pa(MAX_DMA_ADDRESS),
						BOOTMEM_ALLOC_ACCESSIBLE, nid);
	sparsemap_buf_end = sparsemap_buf + size;
}

static void __init sparse_buffer_fini(void)
{
	unsigned long size = sparsemap_buf_end - sparsemap_buf;

	if (sparsemap_buf && size > 0)
		memblock_free_early(__pa(sparsemap_buf), size);
	sparsemap_buf = NULL;
}

void * __meminit sparse_buffer_alloc(unsigned long size)
{
	void *ptr = NULL;

	if (sparsemap_buf) {
		ptr = PTR_ALIGN(sparsemap_buf, size);
		if (ptr + size > sparsemap_buf_end)
			ptr = NULL;
		else
			sparsemap_buf = ptr + size;
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
	unsigned long pnum, usemap_longs, *usemap;
	struct page *map;

	usemap_longs = BITS_TO_LONGS(SECTION_BLOCKFLAGS_BITS);
	usemap = sparse_early_usemaps_alloc_pgdat_section(NODE_DATA(nid),
							  usemap_size() *
							  map_count);
	if (!usemap) {
		pr_err("%s: node[%d] usemap allocation failed", __func__, nid);
		goto failed;
	}
	sparse_buffer_init(map_count * section_map_size(), nid);
	for_each_present_section_nr(pnum_begin, pnum) {
		if (pnum >= pnum_end)
			break;

		map = sparse_mem_map_populate(pnum, nid, NULL);
		if (!map) {
			pr_err("%s: node[%d] memory map backing failed. Some memory will not be available.",
			       __func__, nid);
			pnum_begin = pnum;
			goto failed;
		}
		check_usemap_section_nr(nid, usemap);
		sparse_init_one_section(__nr_to_section(pnum), pnum, map, usemap);
		usemap += usemap_longs;
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
	unsigned long pnum_begin = first_present_section_nr();
	int nid_begin = sparse_early_nid(__nr_to_section(pnum_begin));
	unsigned long pnum_end, map_count = 1;

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

#ifdef CONFIG_MEMORY_HOTREMOVE
/* Mark all memory sections within the pfn range as online */
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
#endif

#ifdef CONFIG_SPARSEMEM_VMEMMAP
static inline struct page *kmalloc_section_memmap(unsigned long pnum, int nid,
		struct vmem_altmap *altmap)
{
	/* This will make the necessary allocations eventually. */
	return sparse_mem_map_populate(pnum, nid, altmap);
}
static void __kfree_section_memmap(struct page *memmap,
		struct vmem_altmap *altmap)
{
	unsigned long start = (unsigned long)memmap;
	unsigned long end = (unsigned long)(memmap + PAGES_PER_SECTION);

	vmemmap_free(start, end, altmap);
}
#ifdef CONFIG_MEMORY_HOTREMOVE
static void free_map_bootmem(struct page *memmap)
{
	unsigned long start = (unsigned long)memmap;
	unsigned long end = (unsigned long)(memmap + PAGES_PER_SECTION);

	vmemmap_free(start, end, NULL);
}
#endif /* CONFIG_MEMORY_HOTREMOVE */
#else
static struct page *__kmalloc_section_memmap(void)
{
	struct page *page, *ret;
	unsigned long memmap_size = sizeof(struct page) * PAGES_PER_SECTION;

	page = alloc_pages(GFP_KERNEL|__GFP_NOWARN, get_order(memmap_size));
	if (page)
		goto got_map_page;

	ret = vmalloc(memmap_size);
	if (ret)
		goto got_map_ptr;

	return NULL;
got_map_page:
	ret = (struct page *)pfn_to_kaddr(page_to_pfn(page));
got_map_ptr:

	return ret;
}

static inline struct page *kmalloc_section_memmap(unsigned long pnum, int nid,
		struct vmem_altmap *altmap)
{
	return __kmalloc_section_memmap();
}

static void __kfree_section_memmap(struct page *memmap,
		struct vmem_altmap *altmap)
{
	if (is_vmalloc_addr(memmap))
		vfree(memmap);
	else
		free_pages((unsigned long)memmap,
			   get_order(sizeof(struct page) * PAGES_PER_SECTION));
}

#ifdef CONFIG_MEMORY_HOTREMOVE
static void free_map_bootmem(struct page *memmap)
{
	unsigned long maps_section_nr, removing_section_nr, i;
	unsigned long magic, nr_pages;
	struct page *page = virt_to_page(memmap);

	nr_pages = PAGE_ALIGN(PAGES_PER_SECTION * sizeof(struct page))
		>> PAGE_SHIFT;

	for (i = 0; i < nr_pages; i++, page++) {
		magic = (unsigned long) page->freelist;

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
#endif /* CONFIG_MEMORY_HOTREMOVE */
#endif /* CONFIG_SPARSEMEM_VMEMMAP */

/*
 * returns the number of sections whose mem_maps were properly
 * set.  If this is <=0, then that means that the passed-in
 * map was not consumed and must be freed.
 */
int __meminit sparse_add_one_section(struct pglist_data *pgdat,
		unsigned long start_pfn, struct vmem_altmap *altmap)
{
	unsigned long section_nr = pfn_to_section_nr(start_pfn);
	struct mem_section *ms;
	struct page *memmap;
	unsigned long *usemap;
	unsigned long flags;
	int ret;

	/*
	 * no locking for this, because it does its own
	 * plus, it does a kmalloc
	 */
	ret = sparse_index_init(section_nr, pgdat->node_id);
	if (ret < 0 && ret != -EEXIST)
		return ret;
	ret = 0;
	memmap = kmalloc_section_memmap(section_nr, pgdat->node_id, altmap);
	if (!memmap)
		return -ENOMEM;
	usemap = __kmalloc_section_usemap();
	if (!usemap) {
		__kfree_section_memmap(memmap, altmap);
		return -ENOMEM;
	}

	pgdat_resize_lock(pgdat, &flags);

	ms = __pfn_to_section(start_pfn);
	if (ms->section_mem_map & SECTION_MARKED_PRESENT) {
		ret = -EEXIST;
		goto out;
	}

#ifdef CONFIG_DEBUG_VM
	/*
	 * Poison uninitialized struct pages in order to catch invalid flags
	 * combinations.
	 */
	memset(memmap, PAGE_POISON_PATTERN, sizeof(struct page) * PAGES_PER_SECTION);
#endif

	section_mark_present(ms);
	sparse_init_one_section(ms, section_nr, memmap, usemap);

out:
	pgdat_resize_unlock(pgdat, &flags);
	if (ret < 0) {
		kfree(usemap);
		__kfree_section_memmap(memmap, altmap);
	}
	return ret;
}

#ifdef CONFIG_MEMORY_HOTREMOVE
#ifdef CONFIG_MEMORY_FAILURE
static void clear_hwpoisoned_pages(struct page *memmap, int nr_pages)
{
	int i;

	if (!memmap)
		return;

	for (i = 0; i < nr_pages; i++) {
		if (PageHWPoison(&memmap[i])) {
			atomic_long_sub(1, &num_poisoned_pages);
			ClearPageHWPoison(&memmap[i]);
		}
	}
}
#else
static inline void clear_hwpoisoned_pages(struct page *memmap, int nr_pages)
{
}
#endif

static void free_section_usemap(struct page *memmap, unsigned long *usemap,
		struct vmem_altmap *altmap)
{
	struct page *usemap_page;

	if (!usemap)
		return;

	usemap_page = virt_to_page(usemap);
	/*
	 * Check to see if allocation came from hot-plug-add
	 */
	if (PageSlab(usemap_page) || PageCompound(usemap_page)) {
		kfree(usemap);
		if (memmap)
			__kfree_section_memmap(memmap, altmap);
		return;
	}

	/*
	 * The usemap came from bootmem. This is packed with other usemaps
	 * on the section which has pgdat at boot time. Just keep it as is now.
	 */

	if (memmap)
		free_map_bootmem(memmap);
}

void sparse_remove_one_section(struct zone *zone, struct mem_section *ms,
		unsigned long map_offset, struct vmem_altmap *altmap)
{
	struct page *memmap = NULL;
	unsigned long *usemap = NULL, flags;
	struct pglist_data *pgdat = zone->zone_pgdat;

	pgdat_resize_lock(pgdat, &flags);
	if (ms->section_mem_map) {
		usemap = ms->pageblock_flags;
		memmap = sparse_decode_mem_map(ms->section_mem_map,
						__section_nr(ms));
		ms->section_mem_map = 0;
		ms->pageblock_flags = NULL;
	}
	pgdat_resize_unlock(pgdat, &flags);

	clear_hwpoisoned_pages(memmap + map_offset,
			PAGES_PER_SECTION - map_offset);
	free_section_usemap(memmap, usemap, altmap);
}
#endif /* CONFIG_MEMORY_HOTREMOVE */
#endif /* CONFIG_MEMORY_HOTPLUG */
