/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/pagevec.h
 *
 * In many places it is efficient to batch an operation up against multiple
 * pages.  A pagevec is a multipage container which is used for that.
 */

#ifndef _LINUX_PAGEVEC_H
#define _LINUX_PAGEVEC_H

#include <linux/xarray.h>

/* 15 pointers + header align the pagevec structure to a power of two */
#define PAGEVEC_SIZE	15

struct page;
struct folio;
struct address_space;

/* Layout must match folio_batch */
struct pagevec {
	unsigned char nr;
	bool percpu_pvec_drained;
	struct page *pages[PAGEVEC_SIZE];
};

void __pagevec_release(struct pagevec *pvec);

static inline void pagevec_init(struct pagevec *pvec)
{
	pvec->nr = 0;
	pvec->percpu_pvec_drained = false;
}

static inline void pagevec_reinit(struct pagevec *pvec)
{
	pvec->nr = 0;
}

static inline unsigned pagevec_count(struct pagevec *pvec)
{
	return pvec->nr;
}

static inline unsigned pagevec_space(struct pagevec *pvec)
{
	return PAGEVEC_SIZE - pvec->nr;
}

/*
 * Add a page to a pagevec.  Returns the number of slots still available.
 */
static inline unsigned pagevec_add(struct pagevec *pvec, struct page *page)
{
	pvec->pages[pvec->nr++] = page;
	return pagevec_space(pvec);
}

static inline void pagevec_release(struct pagevec *pvec)
{
	if (pagevec_count(pvec))
		__pagevec_release(pvec);
}

/**
 * struct folio_batch - A collection of folios.
 *
 * The folio_batch is used to amortise the cost of retrieving and
 * operating on a set of folios.  The order of folios in the batch may be
 * significant (eg delete_from_page_cache_batch()).  Some users of the
 * folio_batch store "exceptional" entries in it which can be removed
 * by calling folio_batch_remove_exceptionals().
 */
struct folio_batch {
	unsigned char nr;
	bool percpu_pvec_drained;
	struct folio *folios[PAGEVEC_SIZE];
};

/* Layout must match pagevec */
static_assert(sizeof(struct pagevec) == sizeof(struct folio_batch));
static_assert(offsetof(struct pagevec, pages) ==
		offsetof(struct folio_batch, folios));

/**
 * folio_batch_init() - Initialise a batch of folios
 * @fbatch: The folio batch.
 *
 * A freshly initialised folio_batch contains zero folios.
 */
static inline void folio_batch_init(struct folio_batch *fbatch)
{
	fbatch->nr = 0;
	fbatch->percpu_pvec_drained = false;
}

static inline void folio_batch_reinit(struct folio_batch *fbatch)
{
	fbatch->nr = 0;
}

static inline unsigned int folio_batch_count(struct folio_batch *fbatch)
{
	return fbatch->nr;
}

static inline unsigned int fbatch_space(struct folio_batch *fbatch)
{
	return PAGEVEC_SIZE - fbatch->nr;
}

/**
 * folio_batch_add() - Add a folio to a batch.
 * @fbatch: The folio batch.
 * @folio: The folio to add.
 *
 * The folio is added to the end of the batch.
 * The batch must have previously been initialised using folio_batch_init().
 *
 * Return: The number of slots still available.
 */
static inline unsigned folio_batch_add(struct folio_batch *fbatch,
		struct folio *folio)
{
	fbatch->folios[fbatch->nr++] = folio;
	return fbatch_space(fbatch);
}

static inline void __folio_batch_release(struct folio_batch *fbatch)
{
	__pagevec_release((struct pagevec *)fbatch);
}

static inline void folio_batch_release(struct folio_batch *fbatch)
{
	if (folio_batch_count(fbatch))
		__folio_batch_release(fbatch);
}

void folio_batch_remove_exceptionals(struct folio_batch *fbatch);
#endif /* _LINUX_PAGEVEC_H */
