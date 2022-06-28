// SPDX-License-Identifier: GPL-2.0
/*
 * HugeTLB Vmemmap Optimization (HVO)
 *
 * Copyright (c) 2020, ByteDance. All rights reserved.
 *
 *     Author: Muchun Song <songmuchun@bytedance.com>
 */
#ifndef _LINUX_HUGETLB_VMEMMAP_H
#define _LINUX_HUGETLB_VMEMMAP_H
#include <linux/hugetlb.h>

#ifdef CONFIG_HUGETLB_PAGE_OPTIMIZE_VMEMMAP
int hugetlb_vmemmap_alloc(struct hstate *h, struct page *head);
void hugetlb_vmemmap_free(struct hstate *h, struct page *head);
void hugetlb_vmemmap_init(struct hstate *h);

/*
 * How many vmemmap pages associated with a HugeTLB page that can be
 * optimized and freed to the buddy allocator.
 */
static inline unsigned int hugetlb_optimize_vmemmap_pages(struct hstate *h)
{
	return h->optimize_vmemmap_pages;
}
#else
static inline int hugetlb_vmemmap_alloc(struct hstate *h, struct page *head)
{
	return 0;
}

static inline void hugetlb_vmemmap_free(struct hstate *h, struct page *head)
{
}

static inline void hugetlb_vmemmap_init(struct hstate *h)
{
}

static inline unsigned int hugetlb_optimize_vmemmap_pages(struct hstate *h)
{
	return 0;
}
#endif /* CONFIG_HUGETLB_PAGE_OPTIMIZE_VMEMMAP */
#endif /* _LINUX_HUGETLB_VMEMMAP_H */
