/*
 * sparse memory mappings.
 */
#include <linux/config.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/dma.h>

/*
 * Permanent SPARSEMEM data:
 *
 * 1) mem_section	- memory sections, mem_map's for valid memory
 */
#ifdef CONFIG_SPARSEMEM_EXTREME
struct mem_section *mem_section[NR_SECTION_ROOTS]
	____cacheline_maxaligned_in_smp;
#else
struct mem_section mem_section[NR_SECTION_ROOTS][SECTIONS_PER_ROOT]
	____cacheline_maxaligned_in_smp;
#endif
EXPORT_SYMBOL(mem_section);

#ifdef CONFIG_SPARSEMEM_EXTREME
static struct mem_section *sparse_index_alloc(int nid)
{
	struct mem_section *section = NULL;
	unsigned long array_size = SECTIONS_PER_ROOT *
				   sizeof(struct mem_section);

	section = alloc_bootmem_node(NODE_DATA(nid), array_size);

	if (section)
		memset(section, 0, array_size);

	return section;
}

static int sparse_index_init(unsigned long section_nr, int nid)
{
	static spinlock_t index_init_lock = SPIN_LOCK_UNLOCKED;
	unsigned long root = SECTION_NR_TO_ROOT(section_nr);
	struct mem_section *section;
	int ret = 0;

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

/* Record a memory area against a node. */
void memory_present(int nid, unsigned long start, unsigned long end)
{
	unsigned long pfn;

	start &= PAGE_SECTION_MASK;
	for (pfn = start; pfn < end; pfn += PAGES_PER_SECTION) {
		unsigned long section = pfn_to_section_nr(pfn);
		struct mem_section *ms;

		sparse_index_init(section, nid);

		ms = __nr_to_section(section);
		if (!ms->section_mem_map)
			ms->section_mem_map = SECTION_MARKED_PRESENT;
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

static int sparse_init_one_section(struct mem_section *ms,
		unsigned long pnum, struct page *mem_map)
{
	if (!valid_section(ms))
		return -EINVAL;

	ms->section_mem_map |= sparse_encode_mem_map(mem_map, pnum);

	return 1;
}

static struct page *sparse_early_mem_map_alloc(unsigned long pnum)
{
	struct page *map;
	int nid = early_pfn_to_nid(section_nr_to_pfn(pnum));
	struct mem_section *ms = __nr_to_section(pnum);

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

/*
 * Allocate the accumulated non-linear sections, allocate a mem_map
 * for each and record the physical to section mapping.
 */
void sparse_init(void)
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

/*
 * returns the number of sections whose mem_maps were properly
 * set.  If this is <=0, then that means that the passed-in
 * map was not consumed and must be freed.
 */
int sparse_add_one_section(unsigned long start_pfn, int nr_pages, struct page *map)
{
	struct mem_section *ms = __pfn_to_section(start_pfn);

	if (ms->section_mem_map & SECTION_MARKED_PRESENT)
		return -EEXIST;

	ms->section_mem_map |= SECTION_MARKED_PRESENT;

	return sparse_init_one_section(ms, pfn_to_section_nr(start_pfn), map);
}
