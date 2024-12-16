/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Queue of folios definitions
 *
 * Copyright (C) 2024 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * See:
 *
 *	Documentation/core-api/folio_queue.rst
 *
 * for a description of the API.
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
	unsigned int		rreq_id;
	unsigned int		debug_id;
};

/**
 * folioq_init - Initialise a folio queue segment
 * @folioq: The segment to initialise
 * @rreq_id: The request identifier to use in tracelines.
 *
 * Initialise a folio queue segment and set an identifier to be used in traces.
 *
 * Note that the folio pointers are left uninitialised.
 */
static inline void folioq_init(struct folio_queue *folioq, unsigned int rreq_id)
{
	folio_batch_init(&folioq->vec);
	folioq->next = NULL;
	folioq->prev = NULL;
	folioq->marks = 0;
	folioq->marks2 = 0;
	folioq->marks3 = 0;
	folioq->rreq_id = rreq_id;
	folioq->debug_id = 0;
}

/**
 * folioq_nr_slots: Query the capacity of a folio queue segment
 * @folioq: The segment to query
 *
 * Query the number of folios that a particular folio queue segment might hold.
 * [!] NOTE: This must not be assumed to be the same for every segment!
 */
static inline unsigned int folioq_nr_slots(const struct folio_queue *folioq)
{
	return PAGEVEC_SIZE;
}

/**
 * folioq_count: Query the occupancy of a folio queue segment
 * @folioq: The segment to query
 *
 * Query the number of folios that have been added to a folio queue segment.
 * Note that this is not decreased as folios are removed from a segment.
 */
static inline unsigned int folioq_count(struct folio_queue *folioq)
{
	return folio_batch_count(&folioq->vec);
}

/**
 * folioq_full: Query if a folio queue segment is full
 * @folioq: The segment to query
 *
 * Query if a folio queue segment is fully occupied.  Note that this does not
 * change if folios are removed from a segment.
 */
static inline bool folioq_full(struct folio_queue *folioq)
{
	//return !folio_batch_space(&folioq->vec);
	return folioq_count(folioq) >= folioq_nr_slots(folioq);
}

/**
 * folioq_is_marked: Check first folio mark in a folio queue segment
 * @folioq: The segment to query
 * @slot: The slot number of the folio to query
 *
 * Determine if the first mark is set for the folio in the specified slot in a
 * folio queue segment.
 */
static inline bool folioq_is_marked(const struct folio_queue *folioq, unsigned int slot)
{
	return test_bit(slot, &folioq->marks);
}

/**
 * folioq_mark: Set the first mark on a folio in a folio queue segment
 * @folioq: The segment to modify
 * @slot: The slot number of the folio to modify
 *
 * Set the first mark for the folio in the specified slot in a folio queue
 * segment.
 */
static inline void folioq_mark(struct folio_queue *folioq, unsigned int slot)
{
	set_bit(slot, &folioq->marks);
}

/**
 * folioq_unmark: Clear the first mark on a folio in a folio queue segment
 * @folioq: The segment to modify
 * @slot: The slot number of the folio to modify
 *
 * Clear the first mark for the folio in the specified slot in a folio queue
 * segment.
 */
static inline void folioq_unmark(struct folio_queue *folioq, unsigned int slot)
{
	clear_bit(slot, &folioq->marks);
}

/**
 * folioq_is_marked2: Check second folio mark in a folio queue segment
 * @folioq: The segment to query
 * @slot: The slot number of the folio to query
 *
 * Determine if the second mark is set for the folio in the specified slot in a
 * folio queue segment.
 */
static inline bool folioq_is_marked2(const struct folio_queue *folioq, unsigned int slot)
{
	return test_bit(slot, &folioq->marks2);
}

/**
 * folioq_mark2: Set the second mark on a folio in a folio queue segment
 * @folioq: The segment to modify
 * @slot: The slot number of the folio to modify
 *
 * Set the second mark for the folio in the specified slot in a folio queue
 * segment.
 */
static inline void folioq_mark2(struct folio_queue *folioq, unsigned int slot)
{
	set_bit(slot, &folioq->marks2);
}

/**
 * folioq_unmark2: Clear the second mark on a folio in a folio queue segment
 * @folioq: The segment to modify
 * @slot: The slot number of the folio to modify
 *
 * Clear the second mark for the folio in the specified slot in a folio queue
 * segment.
 */
static inline void folioq_unmark2(struct folio_queue *folioq, unsigned int slot)
{
	clear_bit(slot, &folioq->marks2);
}

/**
 * folioq_is_marked3: Check third folio mark in a folio queue segment
 * @folioq: The segment to query
 * @slot: The slot number of the folio to query
 *
 * Determine if the third mark is set for the folio in the specified slot in a
 * folio queue segment.
 */
static inline bool folioq_is_marked3(const struct folio_queue *folioq, unsigned int slot)
{
	return test_bit(slot, &folioq->marks3);
}

/**
 * folioq_mark3: Set the third mark on a folio in a folio queue segment
 * @folioq: The segment to modify
 * @slot: The slot number of the folio to modify
 *
 * Set the third mark for the folio in the specified slot in a folio queue
 * segment.
 */
static inline void folioq_mark3(struct folio_queue *folioq, unsigned int slot)
{
	set_bit(slot, &folioq->marks3);
}

/**
 * folioq_unmark3: Clear the third mark on a folio in a folio queue segment
 * @folioq: The segment to modify
 * @slot: The slot number of the folio to modify
 *
 * Clear the third mark for the folio in the specified slot in a folio queue
 * segment.
 */
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

/**
 * folioq_append: Add a folio to a folio queue segment
 * @folioq: The segment to add to
 * @folio: The folio to add
 *
 * Add a folio to the tail of the sequence in a folio queue segment, increasing
 * the occupancy count and returning the slot number for the folio just added.
 * The folio size is extracted and stored in the queue and the marks are left
 * unmodified.
 *
 * Note that it's left up to the caller to check that the segment capacity will
 * not be exceeded and to extend the queue.
 */
static inline unsigned int folioq_append(struct folio_queue *folioq, struct folio *folio)
{
	unsigned int slot = folioq->vec.nr++;

	folioq->vec.folios[slot] = folio;
	folioq->orders[slot] = __folio_order(folio);
	return slot;
}

/**
 * folioq_append_mark: Add a folio to a folio queue segment
 * @folioq: The segment to add to
 * @folio: The folio to add
 *
 * Add a folio to the tail of the sequence in a folio queue segment, increasing
 * the occupancy count and returning the slot number for the folio just added.
 * The folio size is extracted and stored in the queue, the first mark is set
 * and and the second and third marks are left unmodified.
 *
 * Note that it's left up to the caller to check that the segment capacity will
 * not be exceeded and to extend the queue.
 */
static inline unsigned int folioq_append_mark(struct folio_queue *folioq, struct folio *folio)
{
	unsigned int slot = folioq->vec.nr++;

	folioq->vec.folios[slot] = folio;
	folioq->orders[slot] = __folio_order(folio);
	folioq_mark(folioq, slot);
	return slot;
}

/**
 * folioq_folio: Get a folio from a folio queue segment
 * @folioq: The segment to access
 * @slot: The folio slot to access
 *
 * Retrieve the folio in the specified slot from a folio queue segment.  Note
 * that no bounds check is made and if the slot hasn't been added into yet, the
 * pointer will be undefined.  If the slot has been cleared, NULL will be
 * returned.
 */
static inline struct folio *folioq_folio(const struct folio_queue *folioq, unsigned int slot)
{
	return folioq->vec.folios[slot];
}

/**
 * folioq_folio_order: Get the order of a folio from a folio queue segment
 * @folioq: The segment to access
 * @slot: The folio slot to access
 *
 * Retrieve the order of the folio in the specified slot from a folio queue
 * segment.  Note that no bounds check is made and if the slot hasn't been
 * added into yet, the order returned will be 0.
 */
static inline unsigned int folioq_folio_order(const struct folio_queue *folioq, unsigned int slot)
{
	return folioq->orders[slot];
}

/**
 * folioq_folio_size: Get the size of a folio from a folio queue segment
 * @folioq: The segment to access
 * @slot: The folio slot to access
 *
 * Retrieve the size of the folio in the specified slot from a folio queue
 * segment.  Note that no bounds check is made and if the slot hasn't been
 * added into yet, the size returned will be PAGE_SIZE.
 */
static inline size_t folioq_folio_size(const struct folio_queue *folioq, unsigned int slot)
{
	return PAGE_SIZE << folioq_folio_order(folioq, slot);
}

/**
 * folioq_clear: Clear a folio from a folio queue segment
 * @folioq: The segment to clear
 * @slot: The folio slot to clear
 *
 * Clear a folio from a sequence in a folio queue segment and clear its marks.
 * The occupancy count is left unchanged.
 */
static inline void folioq_clear(struct folio_queue *folioq, unsigned int slot)
{
	folioq->vec.folios[slot] = NULL;
	folioq_unmark(folioq, slot);
	folioq_unmark2(folioq, slot);
	folioq_unmark3(folioq, slot);
}

#endif /* _LINUX_FOLIO_QUEUE_H */
