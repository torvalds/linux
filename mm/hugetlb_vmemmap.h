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
#else
static inline void free_huge_page_vmemmap(struct hstate *h, struct page *head)
{
}
#endif /* CONFIG_HUGETLB_PAGE_FREE_VMEMMAP */
#endif /* _LINUX_HUGETLB_VMEMMAP_H */
