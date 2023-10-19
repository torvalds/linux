// SPDX-License-Identifier: GPL-2.0
/*
 * HugeTLB Vmemmap Optimization (HVO)
 *
 * Copyright (c) 2020, ByteDance. All rights reserved.
 *
 *     Author: Muchun Song <songmuchun@bytedance.com>
 *
 * See Documentation/mm/vmemmap_dedup.rst
 */
#define pr_fmt(fmt)	"HugeTLB: " fmt

#include <linux/pgtable.h>
#include <linux/moduleparam.h>
#include <linux/bootmem_info.h>
#include <linux/mmdebug.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include "hugetlb_vmemmap.h"

/**
 * struct vmemmap_remap_walk - walk vmemmap page table
 *
 * @remap_pte:		called for each lowest-level entry (PTE).
 * @nr_walked:		the number of walked pte.
 * @reuse_page:		the page which is reused for the tail vmemmap pages.
 * @reuse_addr:		the virtual address of the @reuse_page page.
 * @vmemmap_pages:	the list head of the vmemmap pages that can be freed
 *			or is mapped from.
 * @flags:		used to modify behavior in vmemmap page table walking
 *			operations.
 */
struct vmemmap_remap_walk {
	void			(*remap_pte)(pte_t *pte, unsigned long addr,
					     struct vmemmap_remap_walk *walk);
	unsigned long		nr_walked;
	struct page		*reuse_page;
	unsigned long		reuse_addr;
	struct list_head	*vmemmap_pages;

/* Skip the TLB flush when we split the PMD */
#define VMEMMAP_SPLIT_NO_TLB_FLUSH	BIT(0)
	unsigned long		flags;
};

static int split_vmemmap_huge_pmd(pmd_t *pmd, unsigned long start, bool flush)
{
	pmd_t __pmd;
	int i;
	unsigned long addr = start;
	struct page *head;
	pte_t *pgtable;

	spin_lock(&init_mm.page_table_lock);
	head = pmd_leaf(*pmd) ? pmd_page(*pmd) : NULL;
	spin_unlock(&init_mm.page_table_lock);

	if (!head)
		return 0;

	pgtable = pte_alloc_one_kernel(&init_mm);
	if (!pgtable)
		return -ENOMEM;

	pmd_populate_kernel(&init_mm, &__pmd, pgtable);

	for (i = 0; i < PTRS_PER_PTE; i++, addr += PAGE_SIZE) {
		pte_t entry, *pte;
		pgprot_t pgprot = PAGE_KERNEL;

		entry = mk_pte(head + i, pgprot);
		pte = pte_offset_kernel(&__pmd, addr);
		set_pte_at(&init_mm, addr, pte, entry);
	}

	spin_lock(&init_mm.page_table_lock);
	if (likely(pmd_leaf(*pmd))) {
		/*
		 * Higher order allocations from buddy allocator must be able to
		 * be treated as indepdenent small pages (as they can be freed
		 * individually).
		 */
		if (!PageReserved(head))
			split_page(head, get_order(PMD_SIZE));

		/* Make pte visible before pmd. See comment in pmd_install(). */
		smp_wmb();
		pmd_populate_kernel(&init_mm, pmd, pgtable);
		if (flush)
			flush_tlb_kernel_range(start, start + PMD_SIZE);
	} else {
		pte_free_kernel(&init_mm, pgtable);
	}
	spin_unlock(&init_mm.page_table_lock);

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
		walk->reuse_page = pte_page(ptep_get(pte));
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
		int ret;

		ret = split_vmemmap_huge_pmd(pmd, addr & PMD_MASK,
				!(walk->flags & VMEMMAP_SPLIT_NO_TLB_FLUSH));
		if (ret)
			return ret;

		next = pmd_addr_end(addr, end);

		/*
		 * We are only splitting, not remapping the hugetlb vmemmap
		 * pages.
		 */
		if (!walk->remap_pte)
			continue;

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

	VM_BUG_ON(!PAGE_ALIGNED(start));
	VM_BUG_ON(!PAGE_ALIGNED(end));

	pgd = pgd_offset_k(addr);
	do {
		int ret;

		next = pgd_addr_end(addr, end);
		ret = vmemmap_p4d_range(pgd, addr, next, walk);
		if (ret)
			return ret;
	} while (pgd++, addr = next, addr != end);

	if (walk->remap_pte)
		flush_tlb_kernel_range(start, end);

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

	list_for_each_entry_safe(page, next, list, lru)
		free_vmemmap_page(page);
}

static void vmemmap_remap_pte(pte_t *pte, unsigned long addr,
			      struct vmemmap_remap_walk *walk)
{
	/*
	 * Remap the tail pages as read-only to catch illegal write operation
	 * to the tail pages.
	 */
	pgprot_t pgprot = PAGE_KERNEL_RO;
	struct page *page = pte_page(ptep_get(pte));
	pte_t entry;

	/* Remapping the head page requires r/w */
	if (unlikely(addr == walk->reuse_addr)) {
		pgprot = PAGE_KERNEL;
		list_del(&walk->reuse_page->lru);

		/*
		 * Makes sure that preceding stores to the page contents from
		 * vmemmap_remap_free() become visible before the set_pte_at()
		 * write.
		 */
		smp_wmb();
	}

	entry = mk_pte(walk->reuse_page, pgprot);
	list_add(&page->lru, walk->vmemmap_pages);
	set_pte_at(&init_mm, addr, pte, entry);
}

/*
 * How many struct page structs need to be reset. When we reuse the head
 * struct page, the special metadata (e.g. page->flags or page->mapping)
 * cannot copy to the tail struct page structs. The invalid value will be
 * checked in the free_tail_page_prepare(). In order to avoid the message
 * of "corrupted mapping in tail page". We need to reset at least 3 (one
 * head struct page struct and two tail struct page structs) struct page
 * structs.
 */
#define NR_RESET_STRUCT_PAGE		3

static inline void reset_struct_pages(struct page *start)
{
	struct page *from = start + NR_RESET_STRUCT_PAGE;

	BUILD_BUG_ON(NR_RESET_STRUCT_PAGE * 2 > PAGE_SIZE / sizeof(struct page));
	memcpy(start, from, sizeof(*from) * NR_RESET_STRUCT_PAGE);
}

static void vmemmap_restore_pte(pte_t *pte, unsigned long addr,
				struct vmemmap_remap_walk *walk)
{
	pgprot_t pgprot = PAGE_KERNEL;
	struct page *page;
	void *to;

	BUG_ON(pte_page(ptep_get(pte)) != walk->reuse_page);

	page = list_first_entry(walk->vmemmap_pages, struct page, lru);
	list_del(&page->lru);
	to = page_to_virt(page);
	copy_page(to, (void *)walk->reuse_addr);
	reset_struct_pages(to);

	/*
	 * Makes sure that preceding stores to the page contents become visible
	 * before the set_pte_at() write.
	 */
	smp_wmb();
	set_pte_at(&init_mm, addr, pte, mk_pte(page, pgprot));
}

/**
 * vmemmap_remap_split - split the vmemmap virtual address range [@start, @end)
 *                      backing PMDs of the directmap into PTEs
 * @start:     start address of the vmemmap virtual address range that we want
 *             to remap.
 * @end:       end address of the vmemmap virtual address range that we want to
 *             remap.
 * @reuse:     reuse address.
 *
 * Return: %0 on success, negative error code otherwise.
 */
static int vmemmap_remap_split(unsigned long start, unsigned long end,
				unsigned long reuse)
{
	int ret;
	struct vmemmap_remap_walk walk = {
		.remap_pte	= NULL,
		.flags		= VMEMMAP_SPLIT_NO_TLB_FLUSH,
	};

	/* See the comment in the vmemmap_remap_free(). */
	BUG_ON(start - reuse != PAGE_SIZE);

	mmap_read_lock(&init_mm);
	ret = vmemmap_remap_range(reuse, end, &walk);
	mmap_read_unlock(&init_mm);

	return ret;
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
 * @vmemmap_pages: list to deposit vmemmap pages to be freed.  It is callers
 *		responsibility to free pages.
 *
 * Return: %0 on success, negative error code otherwise.
 */
static int vmemmap_remap_free(unsigned long start, unsigned long end,
			      unsigned long reuse,
			      struct list_head *vmemmap_pages)
{
	int ret;
	struct vmemmap_remap_walk walk = {
		.remap_pte	= vmemmap_remap_pte,
		.reuse_addr	= reuse,
		.vmemmap_pages	= vmemmap_pages,
		.flags		= 0,
	};
	int nid = page_to_nid((struct page *)reuse);
	gfp_t gfp_mask = GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN;

	/*
	 * Allocate a new head vmemmap page to avoid breaking a contiguous
	 * block of struct page memory when freeing it back to page allocator
	 * in free_vmemmap_page_list(). This will allow the likely contiguous
	 * struct page backing memory to be kept contiguous and allowing for
	 * more allocations of hugepages. Fallback to the currently
	 * mapped head page in case should it fail to allocate.
	 */
	walk.reuse_page = alloc_pages_node(nid, gfp_mask, 0);
	if (walk.reuse_page) {
		copy_page(page_to_virt(walk.reuse_page),
			  (void *)walk.reuse_addr);
		list_add(&walk.reuse_page->lru, vmemmap_pages);
	}

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

	mmap_read_lock(&init_mm);
	ret = vmemmap_remap_range(reuse, end, &walk);
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
			.vmemmap_pages	= vmemmap_pages,
			.flags		= 0,
		};

		vmemmap_remap_range(reuse, end, &walk);
	}
	mmap_read_unlock(&init_mm);

	return ret;
}

static int alloc_vmemmap_page_list(unsigned long start, unsigned long end,
				   struct list_head *list)
{
	gfp_t gfp_mask = GFP_KERNEL | __GFP_RETRY_MAYFAIL;
	unsigned long nr_pages = (end - start) >> PAGE_SHIFT;
	int nid = page_to_nid((struct page *)start);
	struct page *page, *next;

	while (nr_pages--) {
		page = alloc_pages_node(nid, gfp_mask, 0);
		if (!page)
			goto out;
		list_add(&page->lru, list);
	}

	return 0;
out:
	list_for_each_entry_safe(page, next, list, lru)
		__free_page(page);
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
 *
 * Return: %0 on success, negative error code otherwise.
 */
static int vmemmap_remap_alloc(unsigned long start, unsigned long end,
			       unsigned long reuse)
{
	LIST_HEAD(vmemmap_pages);
	struct vmemmap_remap_walk walk = {
		.remap_pte	= vmemmap_restore_pte,
		.reuse_addr	= reuse,
		.vmemmap_pages	= &vmemmap_pages,
		.flags		= 0,
	};

	/* See the comment in the vmemmap_remap_free(). */
	BUG_ON(start - reuse != PAGE_SIZE);

	if (alloc_vmemmap_page_list(start, end, &vmemmap_pages))
		return -ENOMEM;

	mmap_read_lock(&init_mm);
	vmemmap_remap_range(reuse, end, &walk);
	mmap_read_unlock(&init_mm);

	return 0;
}

DEFINE_STATIC_KEY_FALSE(hugetlb_optimize_vmemmap_key);
EXPORT_SYMBOL(hugetlb_optimize_vmemmap_key);

static bool vmemmap_optimize_enabled = IS_ENABLED(CONFIG_HUGETLB_PAGE_OPTIMIZE_VMEMMAP_DEFAULT_ON);
core_param(hugetlb_free_vmemmap, vmemmap_optimize_enabled, bool, 0);

/**
 * hugetlb_vmemmap_restore - restore previously optimized (by
 *			     hugetlb_vmemmap_optimize()) vmemmap pages which
 *			     will be reallocated and remapped.
 * @h:		struct hstate.
 * @head:	the head page whose vmemmap pages will be restored.
 *
 * Return: %0 if @head's vmemmap pages have been reallocated and remapped,
 * negative error code otherwise.
 */
int hugetlb_vmemmap_restore(const struct hstate *h, struct page *head)
{
	int ret;
	unsigned long vmemmap_start = (unsigned long)head, vmemmap_end;
	unsigned long vmemmap_reuse;

	VM_WARN_ON_ONCE(!PageHuge(head));
	if (!HPageVmemmapOptimized(head))
		return 0;

	vmemmap_end	= vmemmap_start + hugetlb_vmemmap_size(h);
	vmemmap_reuse	= vmemmap_start;
	vmemmap_start	+= HUGETLB_VMEMMAP_RESERVE_SIZE;

	/*
	 * The pages which the vmemmap virtual address range [@vmemmap_start,
	 * @vmemmap_end) are mapped to are freed to the buddy allocator, and
	 * the range is mapped to the page which @vmemmap_reuse is mapped to.
	 * When a HugeTLB page is freed to the buddy allocator, previously
	 * discarded vmemmap pages must be allocated and remapping.
	 */
	ret = vmemmap_remap_alloc(vmemmap_start, vmemmap_end, vmemmap_reuse);
	if (!ret) {
		ClearHPageVmemmapOptimized(head);
		static_branch_dec(&hugetlb_optimize_vmemmap_key);
	}

	return ret;
}

/**
 * hugetlb_vmemmap_restore_folios - restore vmemmap for every folio on the list.
 * @h:			hstate.
 * @folio_list:		list of folios.
 * @non_hvo_folios:	Output list of folios for which vmemmap exists.
 *
 * Return: number of folios for which vmemmap was restored, or an error code
 *		if an error was encountered restoring vmemmap for a folio.
 *		Folios that have vmemmap are moved to the non_hvo_folios
 *		list.  Processing of entries stops when the first error is
 *		encountered. The folio that experienced the error and all
 *		non-processed folios will remain on folio_list.
 */
long hugetlb_vmemmap_restore_folios(const struct hstate *h,
					struct list_head *folio_list,
					struct list_head *non_hvo_folios)
{
	struct folio *folio, *t_folio;
	long restored = 0;
	long ret = 0;

	list_for_each_entry_safe(folio, t_folio, folio_list, lru) {
		if (folio_test_hugetlb_vmemmap_optimized(folio)) {
			ret = hugetlb_vmemmap_restore(h, &folio->page);
			if (ret)
				break;
			restored++;
		}

		/* Add non-optimized folios to output list */
		list_move(&folio->lru, non_hvo_folios);
	}

	if (!ret)
		ret = restored;
	return ret;
}

/* Return true iff a HugeTLB whose vmemmap should and can be optimized. */
static bool vmemmap_should_optimize(const struct hstate *h, const struct page *head)
{
	if (HPageVmemmapOptimized((struct page *)head))
		return false;

	if (!READ_ONCE(vmemmap_optimize_enabled))
		return false;

	if (!hugetlb_vmemmap_optimizable(h))
		return false;

	if (IS_ENABLED(CONFIG_MEMORY_HOTPLUG)) {
		pmd_t *pmdp, pmd;
		struct page *vmemmap_page;
		unsigned long vaddr = (unsigned long)head;

		/*
		 * Only the vmemmap page's vmemmap page can be self-hosted.
		 * Walking the page tables to find the backing page of the
		 * vmemmap page.
		 */
		pmdp = pmd_off_k(vaddr);
		/*
		 * The READ_ONCE() is used to stabilize *pmdp in a register or
		 * on the stack so that it will stop changing under the code.
		 * The only concurrent operation where it can be changed is
		 * split_vmemmap_huge_pmd() (*pmdp will be stable after this
		 * operation).
		 */
		pmd = READ_ONCE(*pmdp);
		if (pmd_leaf(pmd))
			vmemmap_page = pmd_page(pmd) + pte_index(vaddr);
		else
			vmemmap_page = pte_page(*pte_offset_kernel(pmdp, vaddr));
		/*
		 * Due to HugeTLB alignment requirements and the vmemmap pages
		 * being at the start of the hotplugged memory region in
		 * memory_hotplug.memmap_on_memory case. Checking any vmemmap
		 * page's vmemmap page if it is marked as VmemmapSelfHosted is
		 * sufficient.
		 *
		 * [                  hotplugged memory                  ]
		 * [        section        ][...][        section        ]
		 * [ vmemmap ][              usable memory               ]
		 *   ^   |     |                                        |
		 *   +---+     |                                        |
		 *     ^       |                                        |
		 *     +-------+                                        |
		 *          ^                                           |
		 *          +-------------------------------------------+
		 */
		if (PageVmemmapSelfHosted(vmemmap_page))
			return false;
	}

	return true;
}

static int __hugetlb_vmemmap_optimize(const struct hstate *h,
					struct page *head,
					struct list_head *vmemmap_pages)
{
	int ret = 0;
	unsigned long vmemmap_start = (unsigned long)head, vmemmap_end;
	unsigned long vmemmap_reuse;

	VM_WARN_ON_ONCE(!PageHuge(head));
	if (!vmemmap_should_optimize(h, head))
		return ret;

	static_branch_inc(&hugetlb_optimize_vmemmap_key);

	vmemmap_end	= vmemmap_start + hugetlb_vmemmap_size(h);
	vmemmap_reuse	= vmemmap_start;
	vmemmap_start	+= HUGETLB_VMEMMAP_RESERVE_SIZE;

	/*
	 * Remap the vmemmap virtual address range [@vmemmap_start, @vmemmap_end)
	 * to the page which @vmemmap_reuse is mapped to.  Add pages previously
	 * mapping the range to vmemmap_pages list so that they can be freed by
	 * the caller.
	 */
	ret = vmemmap_remap_free(vmemmap_start, vmemmap_end, vmemmap_reuse, vmemmap_pages);
	if (ret)
		static_branch_dec(&hugetlb_optimize_vmemmap_key);
	else
		SetHPageVmemmapOptimized(head);

	return ret;
}

/**
 * hugetlb_vmemmap_optimize - optimize @head page's vmemmap pages.
 * @h:		struct hstate.
 * @head:	the head page whose vmemmap pages will be optimized.
 *
 * This function only tries to optimize @head's vmemmap pages and does not
 * guarantee that the optimization will succeed after it returns. The caller
 * can use HPageVmemmapOptimized(@head) to detect if @head's vmemmap pages
 * have been optimized.
 */
void hugetlb_vmemmap_optimize(const struct hstate *h, struct page *head)
{
	LIST_HEAD(vmemmap_pages);

	__hugetlb_vmemmap_optimize(h, head, &vmemmap_pages);
	free_vmemmap_page_list(&vmemmap_pages);
}

static int hugetlb_vmemmap_split(const struct hstate *h, struct page *head)
{
	unsigned long vmemmap_start = (unsigned long)head, vmemmap_end;
	unsigned long vmemmap_reuse;

	if (!vmemmap_should_optimize(h, head))
		return 0;

	vmemmap_end	= vmemmap_start + hugetlb_vmemmap_size(h);
	vmemmap_reuse	= vmemmap_start;
	vmemmap_start	+= HUGETLB_VMEMMAP_RESERVE_SIZE;

	/*
	 * Split PMDs on the vmemmap virtual address range [@vmemmap_start,
	 * @vmemmap_end]
	 */
	return vmemmap_remap_split(vmemmap_start, vmemmap_end, vmemmap_reuse);
}

void hugetlb_vmemmap_optimize_folios(struct hstate *h, struct list_head *folio_list)
{
	struct folio *folio;
	LIST_HEAD(vmemmap_pages);

	list_for_each_entry(folio, folio_list, lru) {
		int ret = hugetlb_vmemmap_split(h, &folio->page);

		/*
		 * Spliting the PMD requires allocating a page, thus lets fail
		 * early once we encounter the first OOM. No point in retrying
		 * as it can be dynamically done on remap with the memory
		 * we get back from the vmemmap deduplication.
		 */
		if (ret == -ENOMEM)
			break;
	}

	flush_tlb_all();

	list_for_each_entry(folio, folio_list, lru) {
		int ret = __hugetlb_vmemmap_optimize(h, &folio->page,
								&vmemmap_pages);

		/*
		 * Pages to be freed may have been accumulated.  If we
		 * encounter an ENOMEM,  free what we have and try again.
		 */
		if (ret == -ENOMEM && !list_empty(&vmemmap_pages)) {
			free_vmemmap_page_list(&vmemmap_pages);
			INIT_LIST_HEAD(&vmemmap_pages);
			__hugetlb_vmemmap_optimize(h, &folio->page, &vmemmap_pages);
		}
	}

	free_vmemmap_page_list(&vmemmap_pages);
}

static struct ctl_table hugetlb_vmemmap_sysctls[] = {
	{
		.procname	= "hugetlb_optimize_vmemmap",
		.data		= &vmemmap_optimize_enabled,
		.maxlen		= sizeof(vmemmap_optimize_enabled),
		.mode		= 0644,
		.proc_handler	= proc_dobool,
	},
	{ }
};

static int __init hugetlb_vmemmap_init(void)
{
	const struct hstate *h;

	/* HUGETLB_VMEMMAP_RESERVE_SIZE should cover all used struct pages */
	BUILD_BUG_ON(__NR_USED_SUBPAGE > HUGETLB_VMEMMAP_RESERVE_PAGES);

	for_each_hstate(h) {
		if (hugetlb_vmemmap_optimizable(h)) {
			register_sysctl_init("vm", hugetlb_vmemmap_sysctls);
			break;
		}
	}
	return 0;
}
late_initcall(hugetlb_vmemmap_init);
