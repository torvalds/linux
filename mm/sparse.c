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
			mem_section[section].section_mem_map = (void *) -1;
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
 * Allocate the accumulated non-linear sections, allocate a mem_map
 * for each and record the physical to section mapping.
 */
void sparse_init(void)
{
	unsigned long pnum;
	struct page *map;
	int nid;

	for (pnum = 0; pnum < NR_MEM_SECTIONS; pnum++) {
		if (!mem_section[pnum].section_mem_map)
			continue;

		nid = early_pfn_to_nid(section_nr_to_pfn(pnum));
		map = alloc_remap(nid, sizeof(struct page) * PAGES_PER_SECTION);
		if (!map)
			map = alloc_bootmem_node(NODE_DATA(nid),
				sizeof(struct page) * PAGES_PER_SECTION);
		if (!map) {
			mem_section[pnum].section_mem_map = 0;
			continue;
		}

		/*
		 * Subtle, we encode the real pfn into the mem_map such that
		 * the identity pfn - section_mem_map will return the actual
		 * physical page frame number.
		 */
		mem_section[pnum].section_mem_map = map -
						section_nr_to_pfn(pnum);
	}
}
