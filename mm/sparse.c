/*
 * sparse memory mappings.
 */
#include <linux/config.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <asm/dma.h>

/*
 * Permanent SPARSEMEM data:
 *
 * 1) mem_section	- memory sections, mem_map's for valid memory
 */
struct mem_section mem_section[NR_MEM_SECTIONS];
EXPORT_SYMBOL(mem_section);

/* Record a memory area against a node. */
void memory_present(int nid, unsigned long start, unsigned long end)
{
	unsigned long pfn;

	start &= PAGE_SECTION_MASK;
	for (pfn = start; pfn < end; pfn += PAGES_PER_SECTION) {
		unsigned long section = pfn_to_section_nr(pfn);
		if (!mem_section[section].section_mem_map)
			mem_section[section].section_mem_map = SECTION_MARKED_PRESENT;
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

	map = alloc_remap(nid, sizeof(struct page) * PAGES_PER_SECTION);
	if (map)
		return map;

	map = alloc_bootmem_node(NODE_DATA(nid),
			sizeof(struct page) * PAGES_PER_SECTION);
	if (map)
		return map;

	printk(KERN_WARNING "%s: allocation failed\n", __FUNCTION__);
	mem_section[pnum].section_mem_map = 0;
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
		if (map)
			sparse_init_one_section(&mem_section[pnum], pnum, map);
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
