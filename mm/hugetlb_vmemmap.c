// SPDX-License-Identifier: GPL-2.0
/*
 * Optimize vmemmap pages associated with HugeTLB
 *
 * Copyright (c) 2020, Bytedance. All rights reserved.
 *
 *     Author: Muchun Song <songmuchun@bytedance.com>
 *
 * See Documentation/vm/vmemmap_dedup.rst
 */
#define pr_fmt(fmt)	"HugeTLB: " fmt

#include "hugetlb_vmemmap.h"

/*
 * There are a lot of struct page structures associated with each HugeTLB page.
 * For tail pages, the value of compound_head is the same. So we can reuse first
 * page of head page structures. We map the virtual addresses of all the pages
 * of tail page structures to the head page struct, and then free these page
 * frames. Therefore, we need to reserve one pages as vmemmap areas.
 */
#define RESERVE_VMEMMAP_NR		1U
#define RESERVE_VMEMMAP_SIZE		(RESERVE_VMEMMAP_NR << PAGE_SHIFT)

DEFINE_STATIC_KEY_MAYBE(CONFIG_HUGETLB_PAGE_OPTIMIZE_VMEMMAP_DEFAULT_ON,
			hugetlb_optimize_vmemmap_key);
EXPORT_SYMBOL(hugetlb_optimize_vmemmap_key);

static int __init hugetlb_vmemmap_early_param(char *buf)
{
	if (!buf)
		return -EINVAL;

	if (!strcmp(buf, "on"))
		static_branch_enable(&hugetlb_optimize_vmemmap_key);
	else if (!strcmp(buf, "off"))
		static_branch_disable(&hugetlb_optimize_vmemmap_key);
	else
		return -EINVAL;

	return 0;
}
early_param("hugetlb_free_vmemmap", hugetlb_vmemmap_early_param);

/*
 * Previously discarded vmemmap pages will be allocated and remapping
 * after this function returns zero.
 */
int hugetlb_vmemmap_alloc(struct hstate *h, struct page *head)
{
	int ret;
	unsigned long vmemmap_addr = (unsigned long)head;
	unsigned long vmemmap_end, vmemmap_reuse, vmemmap_pages;

	if (!HPageVmemmapOptimized(head))
		return 0;

	vmemmap_addr	+= RESERVE_VMEMMAP_SIZE;
	vmemmap_pages	= hugetlb_optimize_vmemmap_pages(h);
	vmemmap_end	= vmemmap_addr + (vmemmap_pages << PAGE_SHIFT);
	vmemmap_reuse	= vmemmap_addr - PAGE_SIZE;

	/*
	 * The pages which the vmemmap virtual address range [@vmemmap_addr,
	 * @vmemmap_end) are mapped to are freed to the buddy allocator, and
	 * the range is mapped to the page which @vmemmap_reuse is mapped to.
	 * When a HugeTLB page is freed to the buddy allocator, previously
	 * discarded vmemmap pages must be allocated and remapping.
	 */
	ret = vmemmap_remap_alloc(vmemmap_addr, vmemmap_end, vmemmap_reuse,
				  GFP_KERNEL | __GFP_NORETRY | __GFP_THISNODE);
	if (!ret)
		ClearHPageVmemmapOptimized(head);

	return ret;
}

void hugetlb_vmemmap_free(struct hstate *h, struct page *head)
{
	unsigned long vmemmap_addr = (unsigned long)head;
	unsigned long vmemmap_end, vmemmap_reuse, vmemmap_pages;

	vmemmap_pages = hugetlb_optimize_vmemmap_pages(h);
	if (!vmemmap_pages)
		return;

	vmemmap_addr	+= RESERVE_VMEMMAP_SIZE;
	vmemmap_end	= vmemmap_addr + (vmemmap_pages << PAGE_SHIFT);
	vmemmap_reuse	= vmemmap_addr - PAGE_SIZE;

	/*
	 * Remap the vmemmap virtual address range [@vmemmap_addr, @vmemmap_end)
	 * to the page which @vmemmap_reuse is mapped to, then free the pages
	 * which the range [@vmemmap_addr, @vmemmap_end] is mapped to.
	 */
	if (!vmemmap_remap_free(vmemmap_addr, vmemmap_end, vmemmap_reuse))
		SetHPageVmemmapOptimized(head);
}

void __init hugetlb_vmemmap_init(struct hstate *h)
{
	unsigned int nr_pages = pages_per_huge_page(h);
	unsigned int vmemmap_pages;

	/*
	 * There are only (RESERVE_VMEMMAP_SIZE / sizeof(struct page)) struct
	 * page structs that can be used when CONFIG_HUGETLB_PAGE_OPTIMIZE_VMEMMAP,
	 * so add a BUILD_BUG_ON to catch invalid usage of the tail struct page.
	 */
	BUILD_BUG_ON(__NR_USED_SUBPAGE >=
		     RESERVE_VMEMMAP_SIZE / sizeof(struct page));

	if (!hugetlb_optimize_vmemmap_enabled())
		return;

	if (!is_power_of_2(sizeof(struct page))) {
		pr_warn_once("cannot optimize vmemmap pages because \"struct page\" crosses page boundaries\n");
		static_branch_disable(&hugetlb_optimize_vmemmap_key);
		return;
	}

	vmemmap_pages = (nr_pages * sizeof(struct page)) >> PAGE_SHIFT;
	/*
	 * The head page is not to be freed to buddy allocator, the other tail
	 * pages will map to the head page, so they can be freed.
	 *
	 * Could RESERVE_VMEMMAP_NR be greater than @vmemmap_pages? It is true
	 * on some architectures (e.g. aarch64). See Documentation/arm64/
	 * hugetlbpage.rst for more details.
	 */
	if (likely(vmemmap_pages > RESERVE_VMEMMAP_NR))
		h->optimize_vmemmap_pages = vmemmap_pages - RESERVE_VMEMMAP_NR;

	pr_info("can optimize %d vmemmap pages for %s\n",
		h->optimize_vmemmap_pages, h->name);
}
