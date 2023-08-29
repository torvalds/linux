/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MM_PAGE_IDLE_H
#define _LINUX_MM_PAGE_IDLE_H

#include <linux/bitops.h>
#include <linux/page-flags.h>
#include <linux/page_ext.h>

#ifdef CONFIG_PAGE_IDLE_FLAG

#ifndef CONFIG_64BIT
/*
 * If there is not enough space to store Idle and Young bits in page flags, use
 * page ext flags instead.
 */
static inline bool folio_test_young(struct folio *folio)
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

static inline bool folio_test_idle(struct folio *folio)
{
	struct page_ext *page_ext = page_ext_get(&folio->page);
	bool page_idle;

	if (unlikely(!page_ext))
		return false;

	page_idle =  test_bit(PAGE_EXT_IDLE, &page_ext->flags);
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
#endif /* !CONFIG_64BIT */

#else /* !CONFIG_PAGE_IDLE_FLAG */

static inline bool folio_test_young(struct folio *folio)
{
	return false;
}

static inline void folio_set_young(struct folio *folio)
{
}

static inline bool folio_test_clear_young(struct folio *folio)
{
	return false;
}

static inline bool folio_test_idle(struct folio *folio)
{
	return false;
}

static inline void folio_set_idle(struct folio *folio)
{
}

static inline void folio_clear_idle(struct folio *folio)
{
}

#endif /* CONFIG_PAGE_IDLE_FLAG */

static inline bool page_is_young(struct page *page)
{
	return folio_test_young(page_folio(page));
}

static inline void set_page_young(struct page *page)
{
	folio_set_young(page_folio(page));
}

static inline bool test_and_clear_page_young(struct page *page)
{
	return folio_test_clear_young(page_folio(page));
}

static inline bool page_is_idle(struct page *page)
{
	return folio_test_idle(page_folio(page));
}

static inline void set_page_idle(struct page *page)
{
	folio_set_idle(page_folio(page));
}
#endif /* _LINUX_MM_PAGE_IDLE_H */
