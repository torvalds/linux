/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Queue of folios definitions
 *
 * Copyright (C) 2024 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _LINUX_FOLIO_QUEUE_H
#define _LINUX_FOLIO_QUEUE_H

#include <linux/pagevec.h>

/*
 * Segment in a queue of running buffers.  Each segment can hold a number of
 * folios and a portion of the queue can be referenced with the ITER_FOLIOQ
 * iterator.  The possibility exists of inserting non-folio elements into the
 * queue (such as gaps).
 *
 * Explicit prev and next pointers are used instead of a list_head to make it
 * easier to add segments to tail and remove them from the head without the
 * need for a lock.
 */
struct folio_queue {
	struct folio_batch	vec;		/* Folios in the queue segment */
	u8			orders[PAGEVEC_SIZE]; /* Order of each folio */
	struct folio_queue	*next;		/* Next queue segment or NULL */
	struct folio_queue	*prev;		/* Previous queue segment of NULL */
	unsigned long		marks;		/* 1-bit mark per folio */
	unsigned long		marks2;		/* Second 1-bit mark per folio */
	unsigned long		marks3;		/* Third 1-bit mark per folio */
#if PAGEVEC_SIZE > BITS_PER_LONG
#error marks is not big enough
#endif
};

static inline void folioq_init(struct folio_queue *folioq)
{
	folio_batch_init(&folioq->vec);
	folioq->next = NULL;
	folioq->prev = NULL;
	folioq->marks = 0;
	folioq->marks2 = 0;
	folioq->marks3 = 0;
}

static inline unsigned int folioq_nr_slots(const struct folio_queue *folioq)
{
	return PAGEVEC_SIZE;
}

static inline unsigned int folioq_count(struct folio_queue *folioq)
{
	return folio_batch_count(&folioq->vec);
}

static inline bool folioq_full(struct folio_queue *folioq)
{
	//return !folio_batch_space(&folioq->vec);
	return folioq_count(folioq) >= folioq_nr_slots(folioq);
}

static inline bool folioq_is_marked(const struct folio_queue *folioq, unsigned int slot)
{
	return test_bit(slot, &folioq->marks);
}

static inline void folioq_mark(struct folio_queue *folioq, unsigned int slot)
{
	set_bit(slot, &folioq->marks);
}

static inline void folioq_unmark(struct folio_queue *folioq, unsigned int slot)
{
	clear_bit(slot, &folioq->marks);
}

static inline bool folioq_is_marked2(const struct folio_queue *folioq, unsigned int slot)
{
	return test_bit(slot, &folioq->marks2);
}

static inline void folioq_mark2(struct folio_queue *folioq, unsigned int slot)
{
	set_bit(slot, &folioq->marks2);
}

static inline void folioq_unmark2(struct folio_queue *folioq, unsigned int slot)
{
	clear_bit(slot, &folioq->marks2);
}

static inline bool folioq_is_marked3(const struct folio_queue *folioq, unsigned int slot)
{
	return test_bit(slot, &folioq->marks3);
}

static inline void folioq_mark3(struct folio_queue *folioq, unsigned int slot)
{
	set_bit(slot, &folioq->marks3);
}

static inline void folioq_unmark3(struct folio_queue *folioq, unsigned int slot)
{
	clear_bit(slot, &folioq->marks3);
}

static inline unsigned int __folio_order(struct folio *folio)
{
	if (!folio_test_large(folio))
		return 0;
	return folio->_flags_1 & 0xff;
}

static inline unsigned int folioq_append(struct folio_queue *folioq, struct folio *folio)
{
	unsigned int slot = folioq->vec.nr++;

	folioq->vec.folios[slot] = folio;
	folioq->orders[slot] = __folio_order(folio);
	return slot;
}

static inline unsigned int folioq_append_mark(struct folio_queue *folioq, struct folio *folio)
{
	unsigned int slot = folioq->vec.nr++;

	folioq->vec.folios[slot] = folio;
	folioq->orders[slot] = __folio_order(folio);
	folioq_mark(folioq, slot);
	return slot;
}

static inline struct folio *folioq_folio(const struct folio_queue *folioq, unsigned int slot)
{
	return folioq->vec.folios[slot];
}

static inline unsigned int folioq_folio_order(const struct folio_queue *folioq, unsigned int slot)
{
	return folioq->orders[slot];
}

static inline size_t folioq_folio_size(const struct folio_queue *folioq, unsigned int slot)
{
	return PAGE_SIZE << folioq_folio_order(folioq, slot);
}

static inline void folioq_clear(struct folio_queue *folioq, unsigned int slot)
{
	folioq->vec.folios[slot] = NULL;
	folioq_unmark(folioq, slot);
	folioq_unmark2(folioq, slot);
	folioq_unmark3(folioq, slot);
}

#endif /* _LINUX_FOLIO_QUEUE_H */
