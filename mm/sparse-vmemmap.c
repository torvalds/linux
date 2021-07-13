// SPDX-License-Identifier: GPL-2.0
/*
 * Virtual Memory Map support
 *
 * (C) 2007 sgi. Christoph Lameter.
 *
 * Virtual memory maps allow VM primitives pfn_to_page, page_to_pfn,
 * virt_to_page, page_address() to be implemented as a base offset
 * calculation without memory access.
 *
 * However, virtual mappings need a page table and TLBs. Many Linux
 * architectures already map their physical space using 1-1 mappings
 * via TLBs. For those arches the virtual memory map is essentially
 * for free if we use the same page size as the 1-1 mappings. In that
 * case the overhead consists of a few additional pages that are
 * allocated to create a view of memory for vmemmap.
 *
 * The architecture is expected to provide a vmemmap_populate() function
 * to instantiate the mapping.
 */
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/memblock.h>
#include <linux/memremap.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/pgtable.h>
#include <linux/bootmem_info.h>

#include <asm/dma.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>

/**
 * struct vmemmap_remap_walk - walk vmemmap page table
 *
 * @remap_pte:		called for each lowest-level entry (PTE).
 * @nr_walked:		the number of walked pte.
 * @reuse_page:		the page which is reused for the tail vmemmap pages.
 * @reuse_addr:		the virtual address of the @reuse_page page.
 * @vmemmap_pages:	the list head of the vmemmap pages that can be freed
 *			or is mapped from.
 */
struct vmemmap_remap_walk {
	void (*remap_pte)(pte_t *pte, unsigned long addr,
			  struct vmemmap_remap_walk *walk);
	unsigned long nr_walked;
	struct page *reuse_page;
	unsigned long reuse_addr;
	struct list_head *vmemmap_pages;
};

static int split_vmemmap_huge_pmd(pmd_t *pmd, unsigned long start,
				  struct vmemmap_remap_walk *walk)
{
	pmd_t __pmd;
	int i;
	unsigned long addr = start;
	struct page *page = pmd_page(*pmd);
	pte_t *pgtable = pte_alloc_one_kernel(&init_mm);

	if (!pgtable)
		return -ENOMEM;

	pmd_populate_kernel(&init_mm, &__pmd, pgtable);

	for (i = 0; i < PMD_SIZE / PAGE_SIZE; i++, addr += PAGE_SIZE) {
		pte_t entry, *pte;
		pgprot_t pgprot = PAGE_KERNEL;

		entry = mk_pte(page + i, pgprot);
		pte = pte_offset_kernel(&__pmd, addr);
		set_pte_at(&init_mm, addr, pte, entry);
	}

	/* Make pte visible before pmd. See comment in __pte_alloc(). */
	smp_wmb();
	pmd_populate_kernel(&init_mm, pmd, pgtable);

	flush_tlb_kernel_range(start, start + PMD_SIZE);

	return 0;
}

static void vmemmap_pte_range(pmd_t *pmd, unsigned long addr,
			      unsigned long end,
			      struct vmemmap_remap_walk *walk)
{
	pte_t *pte = pte_offset_kernel(pmd, addr);

	/*
	 * The reuse_page is found 'first' in table walk before we start
	 * remapping (which is calling @walk->remap_pte).
	 */
	if (!walk->reuse_page) {
		walk->reuse_page = pte_page(*pte);
		/*
		 * Because the reuse address is part of the range that we are
		 * walking, skip the reuse address range.
		 */
		addr += PAGE_SIZE;
		pte++;
		walk->nr_walked++;
	}

	for (; addr != end; addr += PAGE_SIZE, pte++) {
		walk->remap_pte(pte, addr, walk);
		walk->nr_walked++;
	}
}

static int vmemmap_pmd_range(pud_t *pud, unsigned long addr,
			     unsigned long end,
			     struct vmemmap_remap_walk *walk)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_offset(pud, addr);
	do {
		if (pmd_leaf(*pmd)) {
			int ret;

			ret = split_vmemmap_huge_pmd(pmd, addr & PMD_MASK, walk);
			if (ret)
				return ret;
		}
		next = pmd_addr_end(addr, end);
		vmemmap_pte_range(pmd, addr, next, walk);
	} while (pmd++, addr = next, addr != end);

	return 0;
}

static int vmemmap_pud_range(p4d_t *p4d, unsigned long addr,
			     unsigned long end,
			     struct vmemmap_remap_walk *walk)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_offset(p4d, addr);
	do {
		int ret;

		next = pud_addr_end(addr, end);
		ret = vmemmap_pmd_range(pud, addr, next, walk);
		if (ret)
			return ret;
	} while (pud++, addr = next, addr != end);

	return 0;
}

static int vmemmap_p4d_range(pgd_t *pgd, unsigned long addr,
			     unsigned long end,
			     struct vmemmap_remap_walk *walk)
{
	p4d_t *p4d;
	unsigned long next;

	p4d = p4d_offset(pgd, addr);
	do {
		int ret;

		next = p4d_addr_end(addr, end);
		ret = vmemmap_pud_range(p4d, addr, next, walk);
		if (ret)
			return ret;
	} while (p4d++, addr = next, addr != end);

	return 0;
}

static int vmemmap_remap_range(unsigned long start, unsigned long end,
			       struct vmemmap_remap_walk *walk)
{
	unsigned long addr = start;
	unsigned long next;
	pgd_t *pgd;

	VM_BUG_ON(!IS_ALIGNED(start, PAGE_SIZE));
	VM_BUG_ON(!IS_ALIGNED(end, PAGE_SIZE));

	pgd = pgd_offset_k(addr);
	do {
		int ret;

		next = pgd_addr_end(addr, end);
		ret = vmemmap_p4d_range(pgd, addr, next, walk);
		if (ret)
			return ret;
	} while (pgd++, addr = next, addr != end);

	/*
	 * We only change the mapping of the vmemmap virtual address range
	 * [@start + PAGE_SIZE, end), so we only need to flush the TLB which
	 * belongs to the range.
	 */
	flush_tlb_kernel_range(start + PAGE_SIZE, end);

	return 0;
}

/*
 * Free a vmemmap page. A vmemmap page can be allocated from the memblock
 * allocator or buddy allocator. If the PG_reserved flag is set, it means
 * that it allocated from the memblock allocator, just free it via the
 * free_bootmem_page(). Otherwise, use __free_page().
 */
static inline void free_vmemmap_page(struct page *page)
{
	if (PageReserved(page))
		free_bootmem_page(page);
	else
		__free_page(page);
}

/* Free a list of the vmemmap pages */
static void free_vmemmap_page_list(struct list_head *list)
{
	struct page *page, *next;

	list_for_each_entry_safe(page, next, list, lru) {
		list_del(&page->lru);
		free_vmemmap_page(page);
	}
}

static void vmemmap_remap_pte(pte_t *pte, unsigned long addr,
			      struct vmemmap_remap_walk *walk)
{
	/*
	 * Remap the tail pages as read-only to catch illegal write operation
	 * to the tail pages.
	 */
	pgprot_t pgprot = PAGE_KERNEL_RO;
	pte_t entry = mk_pte(walk->reuse_page, pgprot);
	struct page *page = pte_page(*pte);

	list_add_tail(&page->lru, walk->vmemmap_pages);
	set_pte_at(&init_mm, addr, pte, entry);
}

static void vmemmap_restore_pte(pte_t *pte, unsigned long addr,
				struct vmemmap_remap_walk *walk)
{
	pgprot_t pgprot = PAGE_KERNEL;
	struct page *page;
	void *to;

	BUG_ON(pte_page(*pte) != walk->reuse_page);

	page = list_first_entry(walk->vmemmap_pages, struct page, lru);
	list_del(&page->lru);
	to = page_to_virt(page);
	copy_page(to, (void *)walk->reuse_addr);

	set_pte_at(&init_mm, addr, pte, mk_pte(page, pgprot));
}

/**
 * vmemmap_remap_free - remap the vmemmap virtual address range [@start, @end)
 *			to the page which @reuse is mapped to, then free vmemmap
 *			which the range are mapped to.
 * @start:	start address of the vmemmap virtual address range that we want
 *		to remap.
 * @end:	end address of the vmemmap virtual address range that we want to
 *		remap.
 * @reuse:	reuse address.
 *
 * Return: %0 on success, negative error code otherwise.
 */
int vmemmap_remap_free(unsigned long start, unsigned long end,
		       unsigned long reuse)
{
	int ret;
	LIST_HEAD(vmemmap_pages);
	struct vmemmap_remap_walk walk = {
		.remap_pte	= vmemmap_remap_pte,
		.reuse_addr	= reuse,
		.vmemmap_pages	= &vmemmap_pages,
	};

	/*
	 * In order to make remapping routine most efficient for the huge pages,
	 * the routine of vmemmap page table walking has the following rules
	 * (see more details from the vmemmap_pte_range()):
	 *
	 * - The range [@start, @end) and the range [@reuse, @reuse + PAGE_SIZE)
	 *   should be continuous.
	 * - The @reuse address is part of the range [@reuse, @end) that we are
	 *   walking which is passed to vmemmap_remap_range().
	 * - The @reuse address is the first in the complete range.
	 *
	 * So we need to make sure that @start and @reuse meet the above rules.
	 */
	BUG_ON(start - reuse != PAGE_SIZE);

	mmap_write_lock(&init_mm);
	ret = vmemmap_remap_range(reuse, end, &walk);
	mmap_write_downgrade(&init_mm);

	if (ret && walk.nr_walked) {
		end = reuse + walk.nr_walked * PAGE_SIZE;
		/*
		 * vmemmap_pages contains pages from the previous
		 * vmemmap_remap_range call which failed.  These
		 * are pages which were removed from the vmemmap.
		 * They will be restored in the following call.
		 */
		walk = (struct vmemmap_remap_walk) {
			.remap_pte	= vmemmap_restore_pte,
			.reuse_addr	= reuse,
			.vmemmap_pages	= &vmemmap_pages,
		};

		vmemmap_remap_range(reuse, end, &walk);
	}
	mmap_read_unlock(&init_mm);

	free_vmemmap_page_list(&vmemmap_pages);

	return ret;
}

static int alloc_vmemmap_page_list(unsigned long start, unsigned long end,
				   gfp_t gfp_mask, struct list_head *list)
{
	unsigned long nr_pages = (end - start) >> PAGE_SHIFT;
	int nid = page_to_nid((struct page *)start);
	struct page *page, *next;

	while (nr_pages--) {
		page = alloc_pages_node(nid, gfp_mask, 0);
		if (!page)
			goto out;
		list_add_tail(&page->lru, list);
	}

	return 0;
out:
	list_for_each_entry_safe(page, next, list, lru)
		__free_pages(page, 0);
	return -ENOMEM;
}

/**
 * vmemmap_remap_alloc - remap the vmemmap virtual address range [@start, end)
 *			 to the page which is from the @vmemmap_pages
 *			 respectively.
 * @start:	start address of the vmemmap virtual address range that we want
 *		to remap.
 * @end:	end address of the vmemmap virtual address range that we want to
 *		remap.
 * @reuse:	reuse address.
 * @gfp_mask:	GFP flag for allocating vmemmap pages.
 *
 * Return: %0 on success, negative error code otherwise.
 */
int vmemmap_remap_alloc(unsigned long start, unsigned long end,
			unsigned long reuse, gfp_t gfp_mask)
{
	LIST_HEAD(vmemmap_pages);
	struct vmemmap_remap_walk walk = {
		.remap_pte	= vmemmap_restore_pte,
		.reuse_addr	= reuse,
		.vmemmap_pages	= &vmemmap_pages,
	};

	/* See the comment in the vmemmap_remap_free(). */
	BUG_ON(start - reuse != PAGE_SIZE);

	if (alloc_vmemmap_page_list(start, end, gfp_mask, &vmemmap_pages))
		return -ENOMEM;

	mmap_read_lock(&init_mm);
	vmemmap_remap_range(reuse, end, &walk);
	mmap_read_unlock(&init_mm);

	return 0;
}

/*
 * Allocate a block of memory to be used to back the virtual memory map
 * or to back the page tables that are used to create the mapping.
 * Uses the main allocators if they are available, else bootmem.
 */

static void * __ref __earlyonly_bootmem_alloc(int node,
				unsigned long size,
				unsigned long align,
				unsigned long goal)
{
	return memblock_alloc_try_nid_raw(size, align, goal,
					       MEMBLOCK_ALLOC_ACCESSIBLE, node);
}

void * __meminit vmemmap_alloc_block(unsigned long size, int node)
{
	/* If the main allocator is up use that, fallback to bootmem. */
	if (slab_is_available()) {
		gfp_t gfp_mask = GFP_KERNEL|__GFP_RETRY_MAYFAIL|__GFP_NOWARN;
		int order = get_order(size);
		static bool warned;
		struct page *page;

		page = alloc_pages_node(node, gfp_mask, order);
		if (page)
			return page_address(page);

		if (!warned) {
			warn_alloc(gfp_mask & ~__GFP_NOWARN, NULL,
				   "vmemmap alloc failure: order:%u", order);
			warned = true;
		}
		return NULL;
	} else
		return __earlyonly_bootmem_alloc(node, size, size,
				__pa(MAX_DMA_ADDRESS));
}

static void * __meminit altmap_alloc_block_buf(unsigned long size,
					       struct vmem_altmap *altmap);

/* need to make sure size is all the same during early stage */
void * __meminit vmemmap_alloc_block_buf(unsigned long size, int node,
					 struct vmem_altmap *altmap)
{
	void *ptr;

	if (altmap)
		return altmap_alloc_block_buf(size, altmap);

	ptr = sparse_buffer_alloc(size);
	if (!ptr)
		ptr = vmemmap_alloc_block(size, node);
	return ptr;
}

static unsigned long __meminit vmem_altmap_next_pfn(struct vmem_altmap *altmap)
{
	return altmap->base_pfn + altmap->reserve + altmap->alloc
		+ altmap->align;
}

static unsigned long __meminit vmem_altmap_nr_free(struct vmem_altmap *altmap)
{
	unsigned long allocated = altmap->alloc + altmap->align;

	if (altmap->free > allocated)
		return altmap->free - allocated;
	return 0;
}

static void * __meminit altmap_alloc_block_buf(unsigned long size,
					       struct vmem_altmap *altmap)
{
	unsigned long pfn, nr_pfns, nr_align;

	if (size & ~PAGE_MASK) {
		pr_warn_once("%s: allocations must be multiple of PAGE_SIZE (%ld)\n",
				__func__, size);
		return NULL;
	}

	pfn = vmem_altmap_next_pfn(altmap);
	nr_pfns = size >> PAGE_SHIFT;
	nr_align = 1UL << find_first_bit(&nr_pfns, BITS_PER_LONG);
	nr_align = ALIGN(pfn, nr_align) - pfn;
	if (nr_pfns + nr_align > vmem_altmap_nr_free(altmap))
		return NULL;

	altmap->alloc += nr_pfns;
	altmap->align += nr_align;
	pfn += nr_align;

	pr_debug("%s: pfn: %#lx alloc: %ld align: %ld nr: %#lx\n",
			__func__, pfn, altmap->alloc, altmap->align, nr_pfns);
	return __va(__pfn_to_phys(pfn));
}

void __meminit vmemmap_verify(pte_t *pte, int node,
				unsigned long start, unsigned long end)
{
	unsigned long pfn = pte_pfn(*pte);
	int actual_node = early_pfn_to_nid(pfn);

	if (node_distance(actual_node, node) > LOCAL_DISTANCE)
		pr_warn("[%lx-%lx] potential offnode page_structs\n",
			start, end - 1);
}

pte_t * __meminit vmemmap_pte_populate(pmd_t *pmd, unsigned long addr, int node,
				       struct vmem_altmap *altmap)
{
	pte_t *pte = pte_offset_kernel(pmd, addr);
	if (pte_none(*pte)) {
		pte_t entry;
		void *p;

		p = vmemmap_alloc_block_buf(PAGE_SIZE, node, altmap);
		if (!p)
			return NULL;
		entry = pfn_pte(__pa(p) >> PAGE_SHIFT, PAGE_KERNEL);
		set_pte_at(&init_mm, addr, pte, entry);
	}
	return pte;
}

static void * __meminit vmemmap_alloc_block_zero(unsigned long size, int node)
{
	void *p = vmemmap_alloc_block(size, node);

	if (!p)
		return NULL;
	memset(p, 0, size);

	return p;
}

pmd_t * __meminit vmemmap_pmd_populate(pud_t *pud, unsigned long addr, int node)
{
	pmd_t *pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd)) {
		void *p = vmemmap_alloc_block_zero(PAGE_SIZE, node);
		if (!p)
			return NULL;
		pmd_populate_kernel(&init_mm, pmd, p);
	}
	return pmd;
}

pud_t * __meminit vmemmap_pud_populate(p4d_t *p4d, unsigned long addr, int node)
{
	pud_t *pud = pud_offset(p4d, addr);
	if (pud_none(*pud)) {
		void *p = vmemmap_alloc_block_zero(PAGE_SIZE, node);
		if (!p)
			return NULL;
		pud_populate(&init_mm, pud, p);
	}
	return pud;
}

p4d_t * __meminit vmemmap_p4d_populate(pgd_t *pgd, unsigned long addr, int node)
{
	p4d_t *p4d = p4d_offset(pgd, addr);
	if (p4d_none(*p4d)) {
		void *p = vmemmap_alloc_block_zero(PAGE_SIZE, node);
		if (!p)
			return NULL;
		p4d_populate(&init_mm, p4d, p);
	}
	return p4d;
}

pgd_t * __meminit vmemmap_pgd_populate(unsigned long addr, int node)
{
	pgd_t *pgd = pgd_offset_k(addr);
	if (pgd_none(*pgd)) {
		void *p = vmemmap_alloc_block_zero(PAGE_SIZE, node);
		if (!p)
			return NULL;
		pgd_populate(&init_mm, pgd, p);
	}
	return pgd;
}

int __meminit vmemmap_populate_basepages(unsigned long start, unsigned long end,
					 int node, struct vmem_altmap *altmap)
{
	unsigned long addr = start;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	for (; addr < end; addr += PAGE_SIZE) {
		pgd = vmemmap_pgd_populate(addr, node);
		if (!pgd)
			return -ENOMEM;
		p4d = vmemmap_p4d_populate(pgd, addr, node);
		if (!p4d)
			return -ENOMEM;
		pud = vmemmap_pud_populate(p4d, addr, node);
		if (!pud)
			return -ENOMEM;
		pmd = vmemmap_pmd_populate(pud, addr, node);
		if (!pmd)
			return -ENOMEM;
		pte = vmemmap_pte_populate(pmd, addr, node, altmap);
		if (!pte)
			return -ENOMEM;
		vmemmap_verify(pte, node, addr, addr + PAGE_SIZE);
	}

	return 0;
}

struct page * __meminit __populate_section_memmap(unsigned long pfn,
		unsigned long nr_pages, int nid, struct vmem_altmap *altmap)
{
	unsigned long start = (unsigned long) pfn_to_page(pfn);
	unsigned long end = start + nr_pages * sizeof(struct page);

	if (WARN_ON_ONCE(!IS_ALIGNED(pfn, PAGES_PER_SUBSECTION) ||
		!IS_ALIGNED(nr_pages, PAGES_PER_SUBSECTION)))
		return NULL;

	if (vmemmap_populate(start, end, nid, altmap))
		return NULL;

	return pfn_to_page(pfn);
}
