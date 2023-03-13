/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MM_PAGE_IDLE_H
#define _LINUX_MM_PAGE_IDLE_H

#include <linux/bitops.h>
#include <linux/page-flags.h>
#include <linux/page_ext.h>

#ifdef CONFIG_PAGE_IDLE_FLAG

#ifdef CONFIG_64BIT
static inline bool page_is_young(struct page *page)
{
	return PageYoung(page);
}

static inline void set_page_young(struct page *page)
{
	SetPageYoung(page);
}

static inline bool test_and_clear_page_young(struct page *page)
{
	return TestClearPageYoung(page);
}

static inline bool page_is_idle(struct page *page)
{
	return PageIdle(page);
}

static inline void set_page_idle(struct page *page)
{
	SetPageIdle(page);
}

static inline void clear_page_idle(struct page *page)
{
	ClearPageIdle(page);
}
#else /* !CONFIG_64BIT */
/*
 * If there is not enough space to store Idle and Young bits in page flags, use
 * page ext flags instead.
 */
extern struct page_ext_operations page_idle_ops;

static inline bool page_is_young(struct page *page)
{
	struct page_ext *page_ext = page_ext_get(page);
	bool page_young;

	if (unlikely(!page_ext))
		return false;

	page_young = test_bit(PAGE_EXT_YOUNG, &page_ext->flags);
	page_ext_put(page_ext);

	return page_young;
}

static inline void set_page_young(struct page *page)
{
	struct page_ext *page_ext = page_ext_get(page);

	if (unlikely(!page_ext))
		return;

	set_bit(PAGE_EXT_YOUNG, &page_ext->flags);
	page_ext_put(page_ext);
}

static inline bool test_and_clear_page_young(struct page *page)
{
	struct page_ext *page_ext = page_ext_get(page);
	bool page_young;

	if (unlikely(!page_ext))
		return false;

	page_young = test_and_clear_bit(PAGE_EXT_YOUNG, &page_ext->flags);
	page_ext_put(page_ext);

	return page_young;
}

static inline bool page_is_idle(struct page *page)
{
	struct page_ext *page_ext = page_ext_get(page);
	bool page_idle;

	if (unlikely(!page_ext))
		return false;

	page_idle =  test_bit(PAGE_EXT_IDLE, &page_ext->flags);
	page_ext_put(page_ext);

	return page_idle;
}

static inline void set_page_idle(struct page *page)
{
	struct page_ext *page_ext = page_ext_get(page);

	if (unlikely(!page_ext))
		return;

	set_bit(PAGE_EXT_IDLE, &page_ext->flags);
	page_ext_put(page_ext);
}

static inline void clear_page_idle(struct page *page)
{
	struct page_ext *page_ext = page_ext_get(page);

	if (unlikely(!page_ext))
		return;

	clear_bit(PAGE_EXT_IDLE, &page_ext->flags);
	page_ext_put(page_ext);
}
#endif /* CONFIG_64BIT */

#else /* !CONFIG_PAGE_IDLE_FLAG */

static inline bool page_is_young(struct page *page)
{
	return false;
}

static inline void set_page_young(struct page *page)
{
}

static inline bool test_and_clear_page_young(struct page *page)
{
	return false;
}

static inline bool page_is_idle(struct page *page)
{
	return false;
}

static inline void set_page_idle(struct page *page)
{
}

static inline void clear_page_idle(struct page *page)
{
}

#endif /* CONFIG_PAGE_IDLE_FLAG */

#endif /* _LINUX_MM_PAGE_IDLE_H */
