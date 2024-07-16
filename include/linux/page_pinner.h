/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PAGE_PINNER_H
#define __LINUX_PAGE_PINNER_H

#include <linux/jump_label.h>

#ifdef CONFIG_PAGE_PINNER
extern struct static_key_false page_pinner_inited;
extern struct static_key_true failure_tracking;
extern struct page_ext_operations page_pinner_ops;

extern void __free_page_pinner(struct page *page, unsigned int order);
void __page_pinner_failure_detect(struct page *page);
void __page_pinner_put_page(struct page *page);

static inline void free_page_pinner(struct page *page, unsigned int order)
{
	if (static_branch_unlikely(&page_pinner_inited))
		__free_page_pinner(page, order);
}

static inline void page_pinner_put_page(struct page *page)
{
	if (!static_branch_unlikely(&page_pinner_inited))
		return;

	__page_pinner_put_page(page);
}

static inline void page_pinner_failure_detect(struct page *page)
{
	if (!static_branch_unlikely(&page_pinner_inited))
		return;

	__page_pinner_failure_detect(page);
}
#else
static inline void free_page_pinner(struct page *page, unsigned int order)
{
}
static inline void page_pinner_put_page(struct page *page)
{
}
static inline void page_pinner_failure_detect(struct page *page)
{
}
#endif /* CONFIG_PAGE_PINNER */
#endif /* __LINUX_PAGE_PINNER_H */
