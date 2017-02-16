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
struct mem_section *mem_section[NR_SECTION_ROOTS]
	____cacheline_internodealigned_in_smp;
static DEFINE_SPINLOCK(mem_section_lock); /* atomically instantiate new entries */
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
		if (node_state(nid, N_HIGH_MEMORY))
			section = kzalloc_node(array_size, GFP_KERNEL, nid);
		else
			section = kzalloc(array_size, GFP_KERNEL);
	} else {
		section = memblock_virt_alloc_node(array_size, nid);
	}

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

	spin_lock(&mem_section_lock);
	if (mem_section[root] == NULL) {
		mem_section[root] = section;
		section = NULL;
	}
	spin_unlock(&mem_section_lock);

	/*
	 * The only time we expect adding a section may race is during
	 * post-meminit hotplug. So, there is no expectation that 'section'
	 * leaks in the !slab_is_available() case.
	 */
	if (section && slab_is_available()) {
		kfree(section);
		return -EEXIST;
	}

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
	struct mem_section* root;

	for (root_nr = 0; root_nr < NR_SECTION_ROOTS; root_nr++) {
		root = __nr_to_section(root_nr * SECTIONS_PER_ROOT);
		if (!root)
			continue;

		if ((ms >= root) && (ms < (root + SECTIONS_PER_ROOT)))
		     break;
	}

	VM_BUG_ON(root_nr == NR_SECTION_ROOTS);

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

static int __init section_active_index(phys_addr_t phys)
{
	return (phys & ~(PA_SECTION_MASK)) / SECTION_ACTIVE_SIZE;
}

static unsigned long section_active_mask(unsigned long pfn,
					 unsigned long nr_pages)
{
	int idx_start, idx_size;
	phys_addr_t start, size;

	WARN_ON((pfn & ~PAGE_SECTION_MASK) + nr_pages > PAGES_PER_SECTION);
	if (!nr_pages)
		return 0;

	/*
	 * The size is the number of pages left in the section or
	 * nr_pages, whichever is smaller. The size will be rounded up
	 * to the next SECTION_ACTIVE_SIZE boundary, the start will be
	 * rounded down.
	 */
	start = PFN_PHYS(pfn);
	size = PFN_PHYS(min_not_zero(nr_pages, PAGES_PER_SECTION
				- (pfn & ~PAGE_SECTION_MASK)));
	size = ALIGN(size, SECTION_ACTIVE_SIZE);

	idx_start = section_active_index(start);
	idx_size = section_active_index(size);

	if (idx_size == 0)
		return ULONG_MAX; /* full section */
	return ((1UL << idx_size) - 1) << idx_start;
}

void __init section_active_init(unsigned long pfn, unsigned long nr_pages)
{
	int end_sec = pfn_to_section_nr(pfn + nr_pages - 1);
	int i, start_sec = pfn_to_section_nr(pfn);

	if (!nr_pages)
		return;

	for (i = start_sec; i <= end_sec; i++) {
		struct mem_section *ms;
		unsigned long mask;
		unsigned long pfns;

		pfns = min(nr_pages, PAGES_PER_SECTION
				- (pfn & ~PAGE_SECTION_MASK));
		mask = section_active_mask(pfn, pfns);

		ms = __nr_to_section(i);
		pr_debug("%s: sec: %d mask: %#018lx\n", __func__, i, mask);
		ms->usage->map_active = mask;

		pfn += pfns;
		nr_pages -= pfns;
	}
}

/* Record a memory area against a node. */
void __init memory_present(int nid, unsigned long start, unsigned long end)
{
	unsigned long pfn;

	start &= PAGE_SECTION_MASK;
	mminit_validate_memmodel_limits(&start, &end);
	for (pfn = start; pfn < end; pfn += PAGES_PER_SECTION) {
		unsigned long section = pfn_to_section_nr(pfn);
		struct mem_section *ms;

		sparse_index_init(section, nid);
		set_section_nid(section, nid);

		ms = __nr_to_section(section);
		if (!ms->section_mem_map)
			ms->section_mem_map = sparse_encode_early_nid(nid) |
							SECTION_MARKED_PRESENT;
	}
}

/*
 * Only used by the i386 NUMA architecures, but relatively
 * generic code.
 */
unsigned long __init node_memmap_size_bytes(int nid, unsigned long start_pfn,
						     unsigned long end_pfn)
{
	unsigned long pfn;
	unsigned long nr_pages = 0;

	mminit_validate_memmodel_limits(&start_pfn, &end_pfn);
	for (pfn = start_pfn; pfn < end_pfn; pfn += PAGES_PER_SECTION) {
		if (nid != early_pfn_to_nid(pfn))
			continue;

		if (pfn_present(pfn))
			nr_pages += PAGES_PER_SECTION;
	}

	return nr_pages * sizeof(struct page);
}

/*
 * Subtle, we encode the real pfn into the mem_map such that
 * the identity pfn - section_mem_map will return the actual
 * physical page frame number.
 */
static unsigned long sparse_encode_mem_map(struct page *mem_map, unsigned long pnum)
{
	return (unsigned long)(mem_map - (section_nr_to_pfn(pnum)));
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
		struct mem_section_usage *usage)
{
	/*
	 * Given that SPARSEMEM_VMEMMAP=y supports sub-section hotplug,
	 * ->section_mem_map can not be guaranteed to point to a full
	 *  section's worth of memory.  The field is only valid / used
	 *  in the SPARSEMEM_VMEMMAP=n case.
	 */
	if (IS_ENABLED(CONFIG_SPARSEMEM_VMEMMAP))
		mem_map = NULL;

	ms->section_mem_map &= ~SECTION_MAP_MASK;
	ms->section_mem_map |= sparse_encode_mem_map(mem_map, pnum) |
		SECTION_HAS_MEM_MAP;
	ms->usage = usage;
}

unsigned long usemap_size(void)
{
	unsigned long size_bytes;
	size_bytes = roundup(SECTION_BLOCKFLAGS_BITS, 8) / 8;
	size_bytes = roundup(size_bytes, sizeof(unsigned long));
	return size_bytes;
}

#ifdef CONFIG_MEMORY_HOTPLUG
static struct mem_section_usage *__alloc_section_usage(void)
{
	struct mem_section_usage *usage;

	usage = kzalloc(sizeof(*usage) + usemap_size(), GFP_KERNEL);
	/* TODO: allocate the map_active bitmap */
	return usage;
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

static void __init check_usemap_section_nr(int nid,
		struct mem_section_usage *usage)
{
	unsigned long usemap_snr, pgdat_snr;
	static unsigned long old_usemap_snr = NR_MEM_SECTIONS;
	static unsigned long old_pgdat_snr = NR_MEM_SECTIONS;
	struct pglist_data *pgdat = NODE_DATA(nid);
	int usemap_nid;

	usemap_snr = pfn_to_section_nr(__pa(usage) >> PAGE_SHIFT);
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

static void __init check_usemap_section_nr(int nid,
		struct mem_section_usage *usage)
{
}
#endif /* CONFIG_MEMORY_HOTREMOVE */

static void __init sparse_early_usemaps_alloc_node(void *data,
				 unsigned long pnum_begin,
				 unsigned long pnum_end,
				 unsigned long usage_count, int nodeid)
{
	void *usage;
	unsigned long pnum;
	struct mem_section_usage **usage_map = data;
	int size = sizeof(struct mem_section_usage) + usemap_size();

	usage = sparse_early_usemaps_alloc_pgdat_section(NODE_DATA(nodeid),
							  size * usage_count);
	if (!usage) {
		pr_warn("%s: allocation failed\n", __func__);
		return;
	}

	memset(usage, 0, size * usage_count);
	for (pnum = pnum_begin; pnum < pnum_end; pnum++) {
		if (!present_section_nr(pnum))
			continue;
		usage_map[pnum] = usage;
		usage += size;
		check_usemap_section_nr(nodeid, usage_map[pnum]);
	}
}

#ifndef CONFIG_SPARSEMEM_VMEMMAP
struct page __init *__populate_section_memmap(unsigned long pfn,
		unsigned long nr_pages, int nid)
{
	struct page *map;
	unsigned long size;

	map = alloc_remap(nid, sizeof(struct page) * PAGES_PER_SECTION);
	if (map)
		return map;

	size = PAGE_ALIGN(sizeof(struct page) * PAGES_PER_SECTION);
	map = memblock_virt_alloc_try_nid(size,
					  PAGE_SIZE, __pa(MAX_DMA_ADDRESS),
					  BOOTMEM_ALLOC_ACCESSIBLE, nid);
	return map;
}
void __init sparse_mem_maps_populate_node(struct page **map_map,
					  unsigned long pnum_begin,
					  unsigned long pnum_end,
					  unsigned long map_count, int nodeid)
{
	void *map;
	unsigned long pnum;
	unsigned long size = sizeof(struct page) * PAGES_PER_SECTION;

	map = alloc_remap(nodeid, size * map_count);
	if (map) {
		for (pnum = pnum_begin; pnum < pnum_end; pnum++) {
			if (!present_section_nr(pnum))
				continue;
			map_map[pnum] = map;
			map += size;
		}
		return;
	}

	size = PAGE_ALIGN(size);
	map = memblock_virt_alloc_try_nid(size * map_count,
					  PAGE_SIZE, __pa(MAX_DMA_ADDRESS),
					  BOOTMEM_ALLOC_ACCESSIBLE, nodeid);
	if (map) {
		for (pnum = pnum_begin; pnum < pnum_end; pnum++) {
			if (!present_section_nr(pnum))
				continue;
			map_map[pnum] = map;
			map += size;
		}
		return;
	}

	/* fallback */
	for (pnum = pnum_begin; pnum < pnum_end; pnum++) {
		struct mem_section *ms;
		unsigned long pfn = section_nr_to_pfn(pnum);

		if (!present_section_nr(pnum))
			continue;
		map_map[pnum] = __populate_section_memmap(pfn,
				PAGES_PER_SECTION, nodeid);
		if (map_map[pnum])
			continue;
		ms = __nr_to_section(pnum);
		pr_err("%s: sparsemem memory map backing failed some memory will not be available\n",
		       __func__);
		ms->section_mem_map = 0;
	}
}
#endif /* !CONFIG_SPARSEMEM_VMEMMAP */

#ifdef CONFIG_SPARSEMEM_ALLOC_MEM_MAP_TOGETHER
static void __init sparse_early_mem_maps_alloc_node(void *data,
				 unsigned long pnum_begin,
				 unsigned long pnum_end,
				 unsigned long map_count, int nodeid)
{
	struct page **map_map = (struct page **)data;
	sparse_mem_maps_populate_node(map_map, pnum_begin, pnum_end,
					 map_count, nodeid);
}
#else
static struct page __init *sparse_early_mem_map_alloc(unsigned long pnum)
{
	struct page *map;
	struct mem_section *ms = __nr_to_section(pnum);
	int nid = sparse_early_nid(ms);

	map = __populate_section_memmap(section_nr_to_pfn(pnum),
			PAGES_PER_SECTION, nid);
	if (map)
		return map;

	pr_err("%s: sparsemem memory map backing failed some memory will not be available\n",
	       __func__);
	ms->section_mem_map = 0;
	return NULL;
}
#endif

void __weak __meminit vmemmap_populate_print_last(void)
{
}

/**
 *  alloc_usemap_and_memmap - memory alloction for pageblock flags and vmemmap
 *  @map: usage_map for mem_section_usage or mmap_map for vmemmap
 */
static void __init alloc_usemap_and_memmap(void (*alloc_func)
					(void *, unsigned long, unsigned long,
					unsigned long, int), void *data)
{
	unsigned long pnum;
	unsigned long map_count;
	int nodeid_begin = 0;
	unsigned long pnum_begin = 0;

	for (pnum = 0; pnum < NR_MEM_SECTIONS; pnum++) {
		struct mem_section *ms;

		if (!present_section_nr(pnum))
			continue;
		ms = __nr_to_section(pnum);
		nodeid_begin = sparse_early_nid(ms);
		pnum_begin = pnum;
		break;
	}
	map_count = 1;
	for (pnum = pnum_begin + 1; pnum < NR_MEM_SECTIONS; pnum++) {
		struct mem_section *ms;
		int nodeid;

		if (!present_section_nr(pnum))
			continue;
		ms = __nr_to_section(pnum);
		nodeid = sparse_early_nid(ms);
		if (nodeid == nodeid_begin) {
			map_count++;
			continue;
		}
		/* ok, we need to take cake of from pnum_begin to pnum - 1*/
		alloc_func(data, pnum_begin, pnum,
						map_count, nodeid_begin);
		/* new start, update count etc*/
		nodeid_begin = nodeid;
		pnum_begin = pnum;
		map_count = 1;
	}
	/* ok, last chunk */
	alloc_func(data, pnum_begin, NR_MEM_SECTIONS,
						map_count, nodeid_begin);
}

/*
 * Allocate the accumulated non-linear sections, allocate a mem_map
 * for each and record the physical to section mapping.
 */
void __init sparse_init(void)
{
	struct mem_section_usage *usage, **usage_map;
	unsigned long pnum;
	struct page *map;
	int size;
#ifdef CONFIG_SPARSEMEM_ALLOC_MEM_MAP_TOGETHER
	int size2;
	struct page **map_map;
#endif

	/* see include/linux/mmzone.h 'struct mem_section' definition */
	BUILD_BUG_ON(!is_power_of_2(sizeof(struct mem_section)));

	/* Setup pageblock_order for HUGETLB_PAGE_SIZE_VARIABLE */
	set_pageblock_order();

	/*
	 * map is using big page (aka 2M in x86 64 bit)
	 * usage is less one page (aka 24 bytes)
	 * so alloc 2M (with 2M align) and 24 bytes in turn will
	 * make next 2M slip to one more 2M later.
	 * then in big system, the memory will have a lot of holes...
	 * here try to allocate 2M pages continuously.
	 *
	 * powerpc need to call sparse_init_one_section right after each
	 * sparse_early_mem_map_alloc, so allocate usage_map at first.
	 */
	size = sizeof(struct mem_section_usage *) * NR_MEM_SECTIONS;
	usage_map = memblock_virt_alloc(size, 0);
	if (!usage_map)
		panic("can not allocate usage_map\n");
	alloc_usemap_and_memmap(sparse_early_usemaps_alloc_node,
							(void *)usage_map);

#ifdef CONFIG_SPARSEMEM_ALLOC_MEM_MAP_TOGETHER
	size2 = sizeof(struct page *) * NR_MEM_SECTIONS;
	map_map = memblock_virt_alloc(size2, 0);
	if (!map_map)
		panic("can not allocate map_map\n");
	alloc_usemap_and_memmap(sparse_early_mem_maps_alloc_node,
							(void *)map_map);
#endif

	for (pnum = 0; pnum < NR_MEM_SECTIONS; pnum++) {
		if (!present_section_nr(pnum))
			continue;

		usage = usage_map[pnum];
		if (!usage)
			continue;

#ifdef CONFIG_SPARSEMEM_ALLOC_MEM_MAP_TOGETHER
		map = map_map[pnum];
#else
		map = sparse_early_mem_map_alloc(pnum);
#endif
		if (!map)
			continue;

		sparse_init_one_section(__nr_to_section(pnum), pnum, map,
								usage);
	}

	vmemmap_populate_print_last();

#ifdef CONFIG_SPARSEMEM_ALLOC_MEM_MAP_TOGETHER
	memblock_free_early(__pa(map_map), size2);
#endif
	memblock_free_early(__pa(usage_map), size);
}

#ifdef CONFIG_MEMORY_HOTPLUG
#ifdef CONFIG_SPARSEMEM_VMEMMAP
static struct page *populate_section_memmap(unsigned long pfn,
		unsigned long nr_pages, int nid)
{
	return __populate_section_memmap(pfn, nr_pages, nid);
}

static void depopulate_section_memmap(unsigned long pfn, unsigned long nr_pages)
{
	unsigned long start = (unsigned long) pfn_to_page(pfn);
	unsigned long end = start + nr_pages * sizeof(struct page);

	vmemmap_free(start, end);
}
#ifdef CONFIG_MEMORY_HOTREMOVE
static void free_map_bootmem(struct page *memmap)
{
	unsigned long start = (unsigned long)memmap;
	unsigned long end = (unsigned long)(memmap + PAGES_PER_SECTION);

	vmemmap_free(start, end);
}
#endif /* CONFIG_MEMORY_HOTREMOVE */
#else
struct page *populate_section_memmap(unsigned long pfn,
		unsigned long nr_pages, int nid)
{
	struct page *page, *ret;
	unsigned long memmap_size = sizeof(struct page) * PAGES_PER_SECTION;

	if ((pfn & ~PAGE_SECTION_MASK) || nr_pages != PAGES_PER_SECTION) {
		WARN(1, "%s: called with section unaligned parameters\n",
				__func__);
		return NULL;
	}

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

static void depopulate_section_memmap(unsigned long pfn, unsigned long nr_pages)
{
	struct page *memmap = pfn_to_page(pfn);

	if ((pfn & ~PAGE_SECTION_MASK) || nr_pages != PAGES_PER_SECTION) {
		WARN(1, "%s: called with section unaligned parameters\n",
				__func__);
		return;
	}

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

static bool is_early_section(struct mem_section *ms)
{
	struct page *usemap_page;

	usemap_page = virt_to_page(ms->usage->pageblock_flags);
	if (PageSlab(usemap_page) || PageCompound(usemap_page))
		return false;
	else
		return true;

}

#ifndef CONFIG_MEMORY_HOTREMOVE
static void free_map_bootmem(struct page *memmap)
{
}
#endif

static void section_deactivate(struct pglist_data *pgdat, unsigned long pfn,
                unsigned long nr_pages)
{
	bool early_section;
	struct page *memmap = NULL;
	struct mem_section_usage *usage = NULL;
	int section_nr = pfn_to_section_nr(pfn);
	struct mem_section *ms = __nr_to_section(section_nr);
	unsigned long mask = section_active_mask(pfn, nr_pages), flags;

	pgdat_resize_lock(pgdat, &flags);
	if (!ms->usage ||
	    WARN((ms->usage->map_active & mask) != mask,
		 "section already deactivated active: %#lx mask: %#lx\n",
			ms->usage->map_active, mask)) {
		pgdat_resize_unlock(pgdat, &flags);
		return;
	}

	early_section = is_early_section(ms);
	ms->usage->map_active ^= mask;
	if (ms->usage->map_active == 0) {
		usage = ms->usage;
		ms->usage = NULL;
		memmap = sparse_decode_mem_map(ms->section_mem_map,
				section_nr);
		ms->section_mem_map = 0;
	}

	pgdat_resize_unlock(pgdat, &flags);

	/*
	 * There are 3 cases to handle across two configurations
	 * (SPARSEMEM_VMEMMAP={y,n}):
	 *
	 * 1/ deactivation of a partial hot-added section (only possible
	 * in the SPARSEMEM_VMEMMAP=y case).
	 *    a/ section was present at memory init
	 *    b/ section was hot-added post memory init
	 * 2/ deactivation of a complete hot-added section
	 * 3/ deactivation of a complete section from memory init
	 *
	 * For 1/, when map_active does not go to zero we will not be
	 * freeing the usage map, but still need to free the vmemmap
	 * range.
	 *
	 * For 2/ and 3/ the SPARSEMEM_VMEMMAP={y,n} cases are unified
	 */
	if (!mask)
		return;
	if (nr_pages < PAGES_PER_SECTION) {
		if (!IS_ENABLED(CONFIG_SPARSEMEM_VMEMMAP)) {
			WARN(1, "partial memory section removal not supported\n");
			return;
		}
		if (!early_section)
			depopulate_section_memmap(pfn, nr_pages);
		memmap = 0;
	}

	if (usage) {
		if (!early_section) {
			/*
			 * 'memmap' may be zero in the SPARSEMEM_VMEMMAP=y case
			 * (see sparse_init_one_section()), so we can't rely on
			 * it to determine if we need to depopulate the memmap.
			 * Instead, we uncoditionally depopulate due to 'usage'
			 * being valid.
			 */
			if (memmap || (nr_pages >= PAGES_PER_SECTION
					&& IS_ENABLED(CONFIG_SPARSEMEM_VMEMMAP)))
				depopulate_section_memmap(pfn, nr_pages);
			kfree(usage);
			return;
		}
	}

	/*
	 * The usemap came from bootmem. This is packed with other usemaps
	 * on the section which has pgdat at boot time. Just keep it as is now.
	 */
	if (memmap)
		free_map_bootmem(memmap);
}

static struct page * __meminit section_activate(struct pglist_data *pgdat,
		unsigned long pfn, unsigned nr_pages)
{
	struct mem_section *ms = __nr_to_section(pfn_to_section_nr(pfn));
	unsigned long mask = section_active_mask(pfn, nr_pages), flags;
	struct mem_section_usage *usage;
	bool early_section = false;
	struct page *memmap;
	int rc = 0;

	usage = __alloc_section_usage();
	if (!usage)
		return ERR_PTR(-ENOMEM);

	pgdat_resize_lock(pgdat, &flags);
	if (!ms->usage) {
		ms->usage = usage;
		usage = NULL;
	} else
		early_section = is_early_section(ms);

	if (!mask)
		rc = -EINVAL;
	else if (mask & ms->usage->map_active)
		rc = -EBUSY;
	else
		ms->usage->map_active |= mask;
	pgdat_resize_unlock(pgdat, &flags);

	kfree(usage);

	if (rc)
		return ERR_PTR(rc);


	/*
	 * The early init code does not consider partially populated
	 * initial sections, it simply assumes that memory will never be
	 * referenced.  If we hot-add memory into such a section then we
	 * do not need to populate the memmap and can simply reuse what
	 * is already there.
	 */
	if (nr_pages < PAGES_PER_SECTION && early_section)
		return pfn_to_page(pfn);

	memmap = populate_section_memmap(pfn, nr_pages, pgdat->node_id);
	if (!memmap) {
		section_deactivate(pgdat, pfn, nr_pages);
		return ERR_PTR(-ENOMEM);
	}

	return memmap;
}

/**
 * sparse_add_section() - create a new memmap section, or populate an
 * existing one
 * @zone: host zone for the new memory mapping
 * @start_pfn: first pfn to add (section aligned if zone != ZONE_DEVICE)
 * @nr_pages: number of new pages to add
 *
 * Returns 0 on success.
 */
int __meminit sparse_add_section(struct zone *zone, unsigned long start_pfn,
		unsigned long nr_pages)
{
	unsigned long section_nr = pfn_to_section_nr(start_pfn);
	struct pglist_data *pgdat = zone->zone_pgdat;
	struct mem_section *ms;
	struct page *memmap;
	unsigned long flags;
	int ret;

	/*
	 * no locking for this, because it does its own
	 * plus, it does a kmalloc
	 */
	ret = sparse_index_init(section_nr, pgdat->node_id);
	if (ret < 0 && ret != -EEXIST)
		return ret;

	memmap = section_activate(pgdat, start_pfn, nr_pages);
	if (IS_ERR(memmap))
		return PTR_ERR(memmap);

	pgdat_resize_lock(pgdat, &flags);
	ms = __pfn_to_section(start_pfn);
	if (nr_pages == PAGES_PER_SECTION && (ms->section_mem_map
				& SECTION_MARKED_PRESENT)) {
		ret = -EBUSY;
		goto out;
	}
	ms->section_mem_map |= SECTION_MARKED_PRESENT;
	sparse_init_one_section(ms, section_nr, memmap, ms->usage);
out:
	pgdat_resize_unlock(pgdat, &flags);
	if (nr_pages == PAGES_PER_SECTION && ret < 0 && ret != -EEXIST) {
		section_deactivate(pgdat, start_pfn, nr_pages);
		return ret;
	}
	memset(memmap, 0, sizeof(struct page) * nr_pages);
	return 0;
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

void sparse_remove_section(struct zone *zone, struct mem_section *ms,
		unsigned long pfn, unsigned long nr_pages,
		unsigned long map_offset)
{
	struct pglist_data *pgdat = zone->zone_pgdat;

	clear_hwpoisoned_pages(pfn_to_page(pfn) + map_offset,
			nr_pages - map_offset);
	section_deactivate(pgdat, pfn, nr_pages);
}
#endif /* CONFIG_MEMORY_HOTREMOVE */
#endif /* CONFIG_MEMORY_HOTPLUG */
