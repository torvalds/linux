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

#include <linux/memory_hotplug.h>
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

enum vmemmap_optimize_mode {
	VMEMMAP_OPTIMIZE_OFF,
	VMEMMAP_OPTIMIZE_ON,
};

DEFINE_STATIC_KEY_MAYBE(CONFIG_HUGETLB_PAGE_OPTIMIZE_VMEMMAP_DEFAULT_ON,
			hugetlb_optimize_vmemmap_key);
EXPORT_SYMBOL(hugetlb_optimize_vmemmap_key);

static enum vmemmap_optimize_mode vmemmap_optimize_mode =
	IS_ENABLED(CONFIG_HUGETLB_PAGE_OPTIMIZE_VMEMMAP_DEFAULT_ON);

static void vmemmap_optimize_mode_switch(enum vmemmap_optimize_mode to)
{
	if (vmemmap_optimize_mode == to)
		return;

	if (to == VMEMMAP_OPTIMIZE_OFF)
		static_branch_dec(&hugetlb_optimize_vmemmap_key);
	else
		static_branch_inc(&hugetlb_optimize_vmemmap_key);
	WRITE_ONCE(vmemmap_optimize_mode, to);
}

static int __init hugetlb_vmemmap_early_param(char *buf)
{
	bool enable;
	enum vmemmap_optimize_mode mode;

	if (kstrtobool(buf, &enable))
		return -EINVAL;

	mode = enable ? VMEMMAP_OPTIMIZE_ON : VMEMMAP_OPTIMIZE_OFF;
	vmemmap_optimize_mode_switch(mode);

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
	if (!ret) {
		ClearHPageVmemmapOptimized(head);
		static_branch_dec(&hugetlb_optimize_vmemmap_key);
	}

	return ret;
}

void hugetlb_vmemmap_free(struct hstate *h, struct page *head)
{
	unsigned long vmemmap_addr = (unsigned long)head;
	unsigned long vmemmap_end, vmemmap_reuse, vmemmap_pages;

	vmemmap_pages = hugetlb_optimize_vmemmap_pages(h);
	if (!vmemmap_pages)
		return;

	if (READ_ONCE(vmemmap_optimize_mode) == VMEMMAP_OPTIMIZE_OFF)
		return;

	static_branch_inc(&hugetlb_optimize_vmemmap_key);

	vmemmap_addr	+= RESERVE_VMEMMAP_SIZE;
	vmemmap_end	= vmemmap_addr + (vmemmap_pages << PAGE_SHIFT);
	vmemmap_reuse	= vmemmap_addr - PAGE_SIZE;

	/*
	 * Remap the vmemmap virtual address range [@vmemmap_addr, @vmemmap_end)
	 * to the page which @vmemmap_reuse is mapped to, then free the pages
	 * which the range [@vmemmap_addr, @vmemmap_end] is mapped to.
	 */
	if (vmemmap_remap_free(vmemmap_addr, vmemmap_end, vmemmap_reuse))
		static_branch_dec(&hugetlb_optimize_vmemmap_key);
	else
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

#ifdef CONFIG_PROC_SYSCTL
static int hugetlb_optimize_vmemmap_handler(struct ctl_table *table, int write,
					    void *buffer, size_t *length,
					    loff_t *ppos)
{
	int ret;
	enum vmemmap_optimize_mode mode;
	static DEFINE_MUTEX(sysctl_mutex);

	if (write && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	mutex_lock(&sysctl_mutex);
	mode = vmemmap_optimize_mode;
	table->data = &mode;
	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (write && !ret)
		vmemmap_optimize_mode_switch(mode);
	mutex_unlock(&sysctl_mutex);

	return ret;
}

static struct ctl_table hugetlb_vmemmap_sysctls[] = {
	{
		.procname	= "hugetlb_optimize_vmemmap",
		.maxlen		= sizeof(enum vmemmap_optimize_mode),
		.mode		= 0644,
		.proc_handler	= hugetlb_optimize_vmemmap_handler,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{ }
};

static __init int hugetlb_vmemmap_sysctls_init(void)
{
	/*
	 * If "memory_hotplug.memmap_on_memory" is enabled or "struct page"
	 * crosses page boundaries, the vmemmap pages cannot be optimized.
	 */
	if (!mhp_memmap_on_memory() && is_power_of_2(sizeof(struct page)))
		register_sysctl_init("vm", hugetlb_vmemmap_sysctls);

	return 0;
}
late_initcall(hugetlb_vmemmap_sysctls_init);
#endif /* CONFIG_PROC_SYSCTL */
