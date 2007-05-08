/*
 * sparse memory mappings.
 */
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <asm/dma.h>

/*
 * Permanent SPARSEMEM data:
 *
 * 1) mem_section	- memory sections, mem_map's for valid memory
 */
#ifdef CONFIG_SPARSEMEM_EXTREME
struct mem_section *mem_section[NR_SECTION_ROOTS]
	____cacheline_internodealigned_in_smp;
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

int page_to_nid(struct page *page)
{
	return section_to_node_table[page_to_section(page)];
}
EXPORT_SYMBOL(page_to_nid);
#endif

#ifdef CONFIG_SPARSEMEM_EXTREME
static struct mem_section *sparse_index_alloc(int nid)
{
	struct mem_section *section = NULL;
	unsigned long array_size = SECTIONS_PER_ROOT *
				   sizeof(struct mem_section);

	if (slab_is_available())
		section = kmalloc_node(array_size, GFP_KERNEL, nid);
	else
		section = alloc_bootmem_node(NODE_DATA(nid), array_size);

	if (section)
		memset(section, 0, array_size);

	return section;
}

static int __meminit sparse_index_init(unsigned long section_nr, int nid)
{
	static DEFINE_SPINLOCK(index_init_lock);
	unsigned long root = SECTION_NR_TO_ROOT(section_nr);
	struct mem_section *section;
	int ret = 0;

#ifdef NODE_NOT_IN_PAGE_FLAGS
	section_to_node_table[section_nr] = nid;
#endif

	if (mem_section[root])
		return -EEXIST;

	section = sparse_index_alloc(nid);
	/*
	 * This lock keeps two different sections from
	 * reallocating for the same index
	 */
	spin_lock(&index_init_lock);

	if (mem_section[root]) {
		ret = -EEXIST;
		goto out;
	}

	mem_section[root] = section;
out:
	spin_unlock(&index_init_lock);
	return ret;
}
#else /* !SPARSEMEM_EXTREME */
static inline int sparse_index_init(unsigned long section_nr, int nid)
{
	return 0;
}
#endif

/*
 * Although written for the SPARSEMEM_EXTREME case, this happens
 * to also work for the flat array case becase
 * NR_SECTION_ROOTS==NR_MEM_SECTIONS.
 */
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

	return (root_nr * SECTIONS_PER_ROOT) + (ms - root);
}

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

/* Record a memory area against a node. */
void __init memory_present(int nid, unsigned long start, unsigned long end)
{
	unsigned long pfn;

	start &= PAGE_SECTION_MASK;
	for (pfn = start; pfn < end; pfn += PAGES_PER_SECTION) {
		unsigned long section = pfn_to_section_nr(pfn);
		struct mem_section *ms;

		sparse_index_init(section, nid);

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

	for (pfn = start_pfn; pfn < end_pfn; pfn += PAGES_PER_SECTION) {
		if (nid != early_pfn_to_nid(pfn))
			continue;

		if (pfn_valid(pfn))
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
 * We need this if we ever free the mem_maps.  While not implemented yet,
 * this function is included for parity with its sibling.
 */
static __attribute((unused))
struct page *sparse_decode_mem_map(unsigned long coded_mem_map, unsigned long pnum)
{
	return ((struct page *)coded_mem_map) + section_nr_to_pfn(pnum);
}

static int __meminit sparse_init_one_section(struct mem_section *ms,
		unsigned long pnum, struct page *mem_map)
{
	if (!valid_section(ms))
		return -EINVAL;

	ms->section_mem_map &= ~SECTION_MAP_MASK;
	ms->section_mem_map |= sparse_encode_mem_map(mem_map, pnum);

	return 1;
}

static struct page __init *sparse_early_mem_map_alloc(unsigned long pnum)
{
	struct page *map;
	struct mem_section *ms = __nr_to_section(pnum);
	int nid = sparse_early_nid(ms);

	map = alloc_remap(nid, sizeof(struct page) * PAGES_PER_SECTION);
	if (map)
		return map;

	map = alloc_bootmem_node(NODE_DATA(nid),
			sizeof(struct page) * PAGES_PER_SECTION);
	if (map)
		return map;

	printk(KERN_WARNING "%s: allocation failed\n", __FUNCTION__);
	ms->section_mem_map = 0;
	return NULL;
}

static struct page *__kmalloc_section_memmap(unsigned long nr_pages)
{
	struct page *page, *ret;
	unsigned long memmap_size = sizeof(struct page) * nr_pages;

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
	memset(ret, 0, memmap_size);

	return ret;
}

static int vaddr_in_vmalloc_area(void *addr)
{
	if (addr >= (void *)VMALLOC_START &&
	    addr < (void *)VMALLOC_END)
		return 1;
	return 0;
}

static void __kfree_section_memmap(struct page *memmap, unsigned long nr_pages)
{
	if (vaddr_in_vmalloc_area(memmap))
		vfree(memmap);
	else
		free_pages((unsigned long)memmap,
			   get_order(sizeof(struct page) * nr_pages));
}

/*
 * Allocate the accumulated non-linear sections, allocate a mem_map
 * for each and record the physical to section mapping.
 */
void __init sparse_init(void)
{
	unsigned long pnum;
	struct page *map;

	for (pnum = 0; pnum < NR_MEM_SECTIONS; pnum++) {
		if (!valid_section_nr(pnum))
			continue;

		map = sparse_early_mem_map_alloc(pnum);
		if (!map)
			continue;
		sparse_init_one_section(__nr_to_section(pnum), pnum, map);
	}
}

#ifdef CONFIG_MEMORY_HOTPLUG
/*
 * returns the number of sections whose mem_maps were properly
 * set.  If this is <=0, then that means that the passed-in
 * map was not consumed and must be freed.
 */
int sparse_add_one_section(struct zone *zone, unsigned long start_pfn,
			   int nr_pages)
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
	sparse_index_init(section_nr, pgdat->node_id);
	memmap = __kmalloc_section_memmap(nr_pages);

	pgdat_resize_lock(pgdat, &flags);

	ms = __pfn_to_section(start_pfn);
	if (ms->section_mem_map & SECTION_MARKED_PRESENT) {
		ret = -EEXIST;
		goto out;
	}
	ms->section_mem_map |= SECTION_MARKED_PRESENT;

	ret = sparse_init_one_section(ms, section_nr, memmap);

out:
	pgdat_resize_unlock(pgdat, &flags);
	if (ret <= 0)
		__kfree_section_memmap(memmap, nr_pages);
	return ret;
}
#endif
