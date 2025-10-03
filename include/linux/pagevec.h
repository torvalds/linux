/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/pagevec.h
 *
 * In many places it is efficient to batch an operation up against multiple
 * folios.  A folio_batch is a container which is used for that.
 */

#ifndef _LINUX_PAGEVEC_H
#define _LINUX_PAGEVEC_H

#include <linux/types.h>

/* 31 pointers + header align the folio_batch structure to a power of two */
#define PAGEVEC_SIZE	31

struct folio;

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
	unsigned char i;
	bool percpu_pvec_drained;
	struct folio *folios[PAGEVEC_SIZE];
};

/**
 * folio_batch_init() - Initialise a batch of folios
 * @fbatch: The folio batch.
 *
 * A freshly initialised folio_batch contains zero folios.
 */
static inline void folio_batch_init(struct folio_batch *fbatch)
{
	fbatch->nr = 0;
	fbatch->i = 0;
	fbatch->percpu_pvec_drained = false;
}

static inline void folio_batch_reinit(struct folio_batch *fbatch)
{
	fbatch->nr = 0;
	fbatch->i = 0;
}

static inline unsigned int folio_batch_count(const struct folio_batch *fbatch)
{
	return fbatch->nr;
}

static inline unsigned int folio_batch_space(const struct folio_batch *fbatch)
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
	return folio_batch_space(fbatch);
}

/**
 * folio_batch_next - Return the next folio to process.
 * @fbatch: The folio batch being processed.
 *
 * Use this function to implement a queue of folios.
 *
 * Return: The next folio in the queue, or NULL if the queue is empty.
 */
static inline struct folio *folio_batch_next(struct folio_batch *fbatch)
{
	if (fbatch->i == fbatch->nr)
		return NULL;
	return fbatch->folios[fbatch->i++];
}

void __folio_batch_release(struct folio_batch *pvec);

static inline void folio_batch_release(struct folio_batch *fbatch)
{
	if (folio_batch_count(fbatch))
		__folio_batch_release(fbatch);
}

void folio_batch_remove_exceptionals(struct folio_batch *fbatch);
#endif /* _LINUX_PAGEVEC_H */
