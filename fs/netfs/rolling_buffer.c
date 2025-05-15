// SPDX-License-Identifier: GPL-2.0-or-later
/* Rolling buffer helpers
 *
 * Copyright (C) 2024 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/bitops.h>
#include <linux/pagemap.h>
#include <linux/rolling_buffer.h>
#include <linux/slab.h>
#include "internal.h"

static atomic_t debug_ids;

/**
 * netfs_folioq_alloc - Allocate a folio_queue struct
 * @rreq_id: Associated debugging ID for tracing purposes
 * @gfp: Allocation constraints
 * @trace: Trace tag to indicate the purpose of the allocation
 *
 * Allocate, initialise and account the folio_queue struct and log a trace line
 * to mark the allocation.
 */
struct folio_queue *netfs_folioq_alloc(unsigned int rreq_id, gfp_t gfp,
				       unsigned int /*enum netfs_folioq_trace*/ trace)
{
	struct folio_queue *fq;

	fq = kmalloc(sizeof(*fq), gfp);
	if (fq) {
		netfs_stat(&netfs_n_folioq);
		folioq_init(fq, rreq_id);
		fq->debug_id = atomic_inc_return(&debug_ids);
		trace_netfs_folioq(fq, trace);
	}
	return fq;
}
EXPORT_SYMBOL(netfs_folioq_alloc);

/**
 * netfs_folioq_free - Free a folio_queue struct
 * @folioq: The object to free
 * @trace: Trace tag to indicate which free
 *
 * Free and unaccount the folio_queue struct.
 */
void netfs_folioq_free(struct folio_queue *folioq,
		       unsigned int /*enum netfs_trace_folioq*/ trace)
{
	trace_netfs_folioq(folioq, trace);
	netfs_stat_d(&netfs_n_folioq);
	kfree(folioq);
}
EXPORT_SYMBOL(netfs_folioq_free);

/*
 * Initialise a rolling buffer.  We allocate an empty folio queue struct to so
 * that the pointers can be independently driven by the producer and the
 * consumer.
 */
int rolling_buffer_init(struct rolling_buffer *roll, unsigned int rreq_id,
			unsigned int direction)
{
	struct folio_queue *fq;

	fq = netfs_folioq_alloc(rreq_id, GFP_NOFS, netfs_trace_folioq_rollbuf_init);
	if (!fq)
		return -ENOMEM;

	roll->head = fq;
	roll->tail = fq;
	iov_iter_folio_queue(&roll->iter, direction, fq, 0, 0, 0);
	return 0;
}

/*
 * Add another folio_queue to a rolling buffer if there's no space left.
 */
int rolling_buffer_make_space(struct rolling_buffer *roll)
{
	struct folio_queue *fq, *head = roll->head;

	if (!folioq_full(head))
		return 0;

	fq = netfs_folioq_alloc(head->rreq_id, GFP_NOFS, netfs_trace_folioq_make_space);
	if (!fq)
		return -ENOMEM;
	fq->prev = head;

	roll->head = fq;
	if (folioq_full(head)) {
		/* Make sure we don't leave the master iterator pointing to a
		 * block that might get immediately consumed.
		 */
		if (roll->iter.folioq == head &&
		    roll->iter.folioq_slot == folioq_nr_slots(head)) {
			roll->iter.folioq = fq;
			roll->iter.folioq_slot = 0;
		}
	}

	/* Make sure the initialisation is stored before the next pointer.
	 *
	 * [!] NOTE: After we set head->next, the consumer is at liberty to
	 * immediately delete the old head.
	 */
	smp_store_release(&head->next, fq);
	return 0;
}

/*
 * Decant the list of folios to read into a rolling buffer.
 */
ssize_t rolling_buffer_load_from_ra(struct rolling_buffer *roll,
				    struct readahead_control *ractl,
				    struct folio_batch *put_batch)
{
	struct folio_queue *fq;
	struct page **vec;
	int nr, ix, to;
	ssize_t size = 0;

	if (rolling_buffer_make_space(roll) < 0)
		return -ENOMEM;

	fq = roll->head;
	vec = (struct page **)fq->vec.folios;
	nr = __readahead_batch(ractl, vec + folio_batch_count(&fq->vec),
			       folio_batch_space(&fq->vec));
	ix = fq->vec.nr;
	to = ix + nr;
	fq->vec.nr = to;
	for (; ix < to; ix++) {
		struct folio *folio = folioq_folio(fq, ix);
		unsigned int order = folio_order(folio);

		fq->orders[ix] = order;
		size += PAGE_SIZE << order;
		trace_netfs_folio(folio, netfs_folio_trace_read);
		if (!folio_batch_add(put_batch, folio))
			folio_batch_release(put_batch);
	}
	WRITE_ONCE(roll->iter.count, roll->iter.count + size);

	/* Store the counter after setting the slot. */
	smp_store_release(&roll->next_head_slot, to);
	return size;
}

/*
 * Append a folio to the rolling buffer.
 */
ssize_t rolling_buffer_append(struct rolling_buffer *roll, struct folio *folio,
			      unsigned int flags)
{
	ssize_t size = folio_size(folio);
	int slot;

	if (rolling_buffer_make_space(roll) < 0)
		return -ENOMEM;

	slot = folioq_append(roll->head, folio);
	if (flags & ROLLBUF_MARK_1)
		folioq_mark(roll->head, slot);
	if (flags & ROLLBUF_MARK_2)
		folioq_mark2(roll->head, slot);

	WRITE_ONCE(roll->iter.count, roll->iter.count + size);

	/* Store the counter after setting the slot. */
	smp_store_release(&roll->next_head_slot, slot);
	return size;
}

/*
 * Delete a spent buffer from a rolling queue and return the next in line.  We
 * don't return the last buffer to keep the pointers independent, but return
 * NULL instead.
 */
struct folio_queue *rolling_buffer_delete_spent(struct rolling_buffer *roll)
{
	struct folio_queue *spent = roll->tail, *next = READ_ONCE(spent->next);

	if (!next)
		return NULL;
	next->prev = NULL;
	netfs_folioq_free(spent, netfs_trace_folioq_delete);
	roll->tail = next;
	return next;
}

/*
 * Clear out a rolling queue.  Folios that have mark 1 set are put.
 */
void rolling_buffer_clear(struct rolling_buffer *roll)
{
	struct folio_batch fbatch;
	struct folio_queue *p;

	folio_batch_init(&fbatch);

	while ((p = roll->tail)) {
		roll->tail = p->next;
		for (int slot = 0; slot < folioq_count(p); slot++) {
			struct folio *folio = folioq_folio(p, slot);

			if (!folio)
				continue;
			if (folioq_is_marked(p, slot)) {
				trace_netfs_folio(folio, netfs_folio_trace_put);
				if (!folio_batch_add(&fbatch, folio))
					folio_batch_release(&fbatch);
			}
		}

		netfs_folioq_free(p, netfs_trace_folioq_clear);
	}

	folio_batch_release(&fbatch);
}
