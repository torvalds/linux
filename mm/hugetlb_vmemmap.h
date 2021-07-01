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
void free_huge_page_vmemmap(struct hstate *h, struct page *head);

/*
 * How many vmemmap pages associated with a HugeTLB page that can be freed
 * to the buddy allocator.
 *
 * Todo: Returns zero for now, which means the feature is disabled. We will
 * enable it once all the infrastructure is there.
 */
static inline unsigned int free_vmemmap_pages_per_hpage(struct hstate *h)
{
	return 0;
}
#else
static inline void free_huge_page_vmemmap(struct hstate *h, struct page *head)
{
}

static inline unsigned int free_vmemmap_pages_per_hpage(struct hstate *h)
{
	return 0;
}
#endif /* CONFIG_HUGETLB_PAGE_FREE_VMEMMAP */
#endif /* _LINUX_HUGETLB_VMEMMAP_H */
