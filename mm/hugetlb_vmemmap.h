// SPDX-License-Identifier: GPL-2.0
/*
 * Free some vmemmap pages of HugeTLB
 *
 * Copyright (c) 2020, Bytedance. All rights reserved.
 *
 *     Author: Muchun Song <songmuchun@bytedance.com>
 */
#ifndef _LINUX_HUGETLB_VMEMMAP_H
#define _LINUX_HUGETLB_VMEMMAP_H
#include <linux/hugetlb.h>

#ifdef CONFIG_HUGETLB_PAGE_FREE_VMEMMAP
int alloc_huge_page_vmemmap(struct hstate *h, struct page *head);
void free_huge_page_vmemmap(struct hstate *h, struct page *head);
void hugetlb_vmemmap_init(struct hstate *h);

/*
 * How many vmemmap pages associated with a HugeTLB page that can be freed
 * to the buddy allocator.
 */
static inline unsigned int free_vmemmap_pages_per_hpage(struct hstate *h)
{
	return h->nr_free_vmemmap_pages;
}
#else
static inline int alloc_huge_page_vmemmap(struct hstate *h, struct page *head)
{
	return 0;
}

static inline void free_huge_page_vmemmap(struct hstate *h, struct page *head)
{
}

static inline void hugetlb_vmemmap_init(struct hstate *h)
{
}

static inline unsigned int free_vmemmap_pages_per_hpage(struct hstate *h)
{
	return 0;
}
#endif /* CONFIG_HUGETLB_PAGE_FREE_VMEMMAP */
#endif /* _LINUX_HUGETLB_VMEMMAP_H */
