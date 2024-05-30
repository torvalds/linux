/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MM_PAGE_IDLE_H
#define _LINUX_MM_PAGE_IDLE_H

#include <linux/bitops.h>
#include <linux/page-flags.h>
#include <linux/page_ext.h>

#if defined(CONFIG_PAGE_IDLE_FLAG) && !defined(CONFIG_64BIT)
/*
 * If there is not enough space to store Idle and Young bits in page flags, use
 * page ext flags instead.
 */
static inline bool folio_test_young(const struct folio *folio)
{
	struct page_ext *page_ext = page_ext_get(&folio->page);
	bool page_young;

	if (unlikely(!page_ext))
		return false;

	page_young = test_bit(PAGE_EXT_YOUNG, &page_ext->flags);
	page_ext_put(page_ext);

	return page_young;
}

static inline void folio_set_young(struct folio *folio)
{
	struct page_ext *page_ext = page_ext_get(&folio->page);

	if (unlikely(!page_ext))
		return;

	set_bit(PAGE_EXT_YOUNG, &page_ext->flags);
	page_ext_put(page_ext);
}

static inline bool folio_test_clear_young(struct folio *folio)
{
	struct page_ext *page_ext = page_ext_get(&folio->page);
	bool page_young;

	if (unlikely(!page_ext))
		return false;

	page_young = test_and_clear_bit(PAGE_EXT_YOUNG, &page_ext->flags);
	page_ext_put(page_ext);

	return page_young;
}

static inline bool folio_test_idle(const struct folio *folio)
{
	struct page_ext *page_ext = page_ext_get(&folio->page);
	bool page_idle;

	if (unlikely(!page_ext))
		return false;

	page_idle = test_bit(PAGE_EXT_IDLE, &page_ext->flags);
	page_ext_put(page_ext);

	return page_idle;
}

static inline void folio_set_idle(struct folio *folio)
{
	struct page_ext *page_ext = page_ext_get(&folio->page);

	if (unlikely(!page_ext))
		return;

	set_bit(PAGE_EXT_IDLE, &page_ext->flags);
	page_ext_put(page_ext);
}

static inline void folio_clear_idle(struct folio *folio)
{
	struct page_ext *page_ext = page_ext_get(&folio->page);

	if (unlikely(!page_ext))
		return;

	clear_bit(PAGE_EXT_IDLE, &page_ext->flags);
	page_ext_put(page_ext);
}
#endif /* CONFIG_PAGE_IDLE_FLAG && !64BIT */
#endif /* _LINUX_MM_PAGE_IDLE_H */
