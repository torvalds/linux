/*
 * Virtual Memory Map support
 *
 * (C) 2007 sgi. Christoph Lameter <clameter@sgi.com>.
 *
 * Virtual memory maps allow VM primitives pfn_to_page, page_to_pfn,
 * virt_to_page, page_address() to be implemented as a base offset
 * calculation without memory access.
 *
 * However, virtual mappings need a page table and TLBs. Many Linux
 * architectures already map their physical space using 1-1 mappings
 * via TLBs. For those arches the virtual memmory map is essentially
 * for free if we use the same page size as the 1-1 mappings. In that
 * case the overhead consists of a few additional pages that are
 * allocated to create a view of memory for vmemmap.
 *
 * Special Kconfig settings:
 *
 * CONFIG_ARCH_POPULATES_SPARSEMEM_VMEMMAP
 *
 * 	The architecture has its own functions to populate the memory
 * 	map and provides a vmemmap_populate function.
 *
 * CONFIG_ARCH_POPULATES_SPARSEMEM_VMEMMAP_PMD
 *
 * 	The architecture provides functions to populate the pmd level
 * 	of the vmemmap mappings.  Allowing mappings using large pages
 * 	where available.
 *
 * 	If neither are set then PAGE_SIZE mappings are generated which
 * 	require one PTE/TLB per PAGE_SIZE chunk of the virtual memory map.
 */
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <asm/dma.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>

/*
 * Allocate a block of memory to be used to back the virtual memory map
 * or to back the page tables that are used to create the mapping.
 * Uses the main allocators if they are available, else bootmem.
 */
void * __meminit vmemmap_alloc_block(unsigned long size, int node)
{
	/* If the main allocator is up use that, fallback to bootmem. */
	if (slab_is_available()) {
		struct page *page = alloc_pages_node(node,
				GFP_KERNEL | __GFP_ZERO, get_order(size));
		if (page)
			return page_address(page);
		return NULL;
	} else
		return __alloc_bootmem_node(NODE_DATA(node), size, size,
				__pa(MAX_DMA_ADDRESS));
}

#ifndef CONFIG_ARCH_POPULATES_SPARSEMEM_VMEMMAP
void __meminit vmemmap_verify(pte_t *pte, int node,
				unsigned long start, unsigned long end)
{
	unsigned long pfn = pte_pfn(*pte);
	int actual_node = early_pfn_to_nid(pfn);

	if (actual_node != node)
		printk(KERN_WARNING "[%lx-%lx] potential offnode "
			"page_structs\n", start, end - 1);
}

#ifndef CONFIG_ARCH_POPULATES_SPARSEMEM_VMEMMAP_PMD
static int __meminit vmemmap_populate_pte(pmd_t *pmd, unsigned long addr,
					unsigned long end, int node)
{
	pte_t *pte;

	for (pte = pte_offset_kernel(pmd, addr); addr < end;
						pte++, addr += PAGE_SIZE)
		if (pte_none(*pte)) {
			pte_t entry;
			void *p = vmemmap_alloc_block(PAGE_SIZE, node);
			if (!p)
				return -ENOMEM;

			entry = pfn_pte(__pa(p) >> PAGE_SHIFT, PAGE_KERNEL);
			set_pte(pte, entry);

		} else
			vmemmap_verify(pte, node, addr + PAGE_SIZE, end);

	return 0;
}

int __meminit vmemmap_populate_pmd(pud_t *pud, unsigned long addr,
						unsigned long end, int node)
{
	pmd_t *pmd;
	int error = 0;
	unsigned long next;

	for (pmd = pmd_offset(pud, addr); addr < end && !error;
						pmd++, addr = next) {
		if (pmd_none(*pmd)) {
			void *p = vmemmap_alloc_block(PAGE_SIZE, node);
			if (!p)
				return -ENOMEM;

			pmd_populate_kernel(&init_mm, pmd, p);
		} else
			vmemmap_verify((pte_t *)pmd, node,
					pmd_addr_end(addr, end), end);
		next = pmd_addr_end(addr, end);
		error = vmemmap_populate_pte(pmd, addr, next, node);
	}
	return error;
}
#endif /* CONFIG_ARCH_POPULATES_SPARSEMEM_VMEMMAP_PMD */

static int __meminit vmemmap_populate_pud(pgd_t *pgd, unsigned long addr,
						unsigned long end, int node)
{
	pud_t *pud;
	int error = 0;
	unsigned long next;

	for (pud = pud_offset(pgd, addr); addr < end && !error;
						pud++, addr = next) {
		if (pud_none(*pud)) {
			void *p = vmemmap_alloc_block(PAGE_SIZE, node);
			if (!p)
				return -ENOMEM;

			pud_populate(&init_mm, pud, p);
		}
		next = pud_addr_end(addr, end);
		error = vmemmap_populate_pmd(pud, addr, next, node);
	}
	return error;
}

int __meminit vmemmap_populate(struct page *start_page,
						unsigned long nr, int node)
{
	pgd_t *pgd;
	unsigned long addr = (unsigned long)start_page;
	unsigned long end = (unsigned long)(start_page + nr);
	unsigned long next;
	int error = 0;

	printk(KERN_DEBUG "[%lx-%lx] Virtual memory section"
		" (%ld pages) node %d\n", addr, end - 1, nr, node);

	for (pgd = pgd_offset_k(addr); addr < end && !error;
					pgd++, addr = next) {
		if (pgd_none(*pgd)) {
			void *p = vmemmap_alloc_block(PAGE_SIZE, node);
			if (!p)
				return -ENOMEM;

			pgd_populate(&init_mm, pgd, p);
		}
		next = pgd_addr_end(addr,end);
		error = vmemmap_populate_pud(pgd, addr, next, node);
	}
	return error;
}
#endif /* !CONFIG_ARCH_POPULATES_SPARSEMEM_VMEMMAP */

struct page __init *sparse_early_mem_map_populate(unsigned long pnum, int nid)
{
	struct page *map = pfn_to_page(pnum * PAGES_PER_SECTION);
	int error = vmemmap_populate(map, PAGES_PER_SECTION, nid);
	if (error)
		return NULL;

	return map;
}
