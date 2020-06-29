/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MM_PAGE_REPORTING_H
#define _MM_PAGE_REPORTING_H

#include <linux/mmzone.h>
#include <linux/pageblock-flags.h>
#include <linux/page-isolation.h>
#include <linux/jump_label.h>
#include <linux/slab.h>
#include <linux/pgtable.h>
#include <linux/scatterlist.h>

#define PAGE_REPORTING_MIN_ORDER	pageblock_order

#ifdef CONFIG_PAGE_REPORTING
DECLARE_STATIC_KEY_FALSE(page_reporting_enabled);
void __page_reporting_notify(void);

static inline bool page_reported(struct page *page)
{
	return static_branch_unlikely(&page_reporting_enabled) &&
	       PageReported(page);
}

/**
 * page_reporting_notify_free - Free page notification to start page processing
 *
 * This function is meant to act as a screener for __page_reporting_notify
 * which will determine if a give zone has crossed over the high-water mark
 * that will justify us beginning page treatment. If we have crossed that
 * threshold then it will start the process of pulling some pages and
 * placing them in the batch list for treatment.
 */
static inline void page_reporting_notify_free(unsigned int order)
{
	/* Called from hot path in __free_one_page() */
	if (!static_branch_unlikely(&page_reporting_enabled))
		return;

	/* Determine if we have crossed reporting threshold */
	if (order < PAGE_REPORTING_MIN_ORDER)
		return;

	/* This will add a few cycles, but should be called infrequently */
	__page_reporting_notify();
}
#else /* CONFIG_PAGE_REPORTING */
#define page_reported(_page)	false

static inline void page_reporting_notify_free(unsigned int order)
{
}
#endif /* CONFIG_PAGE_REPORTING */
#endif /*_MM_PAGE_REPORTING_H */
