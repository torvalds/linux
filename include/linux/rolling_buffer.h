/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Rolling buffer of folios
 *
 * Copyright (C) 2024 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _ROLLING_BUFFER_H
#define _ROLLING_BUFFER_H

#include <linux/folio_queue.h>
#include <linux/uio.h>

/*
 * Rolling buffer.  Whilst the buffer is live and in use, folios and folio
 * queue segments can be added to one end by one thread and removed from the
 * other end by another thread.  The buffer isn't allowed to be empty; it must
 * always have at least one folio_queue in it so that neither side has to
 * modify both queue pointers.
 *
 * The iterator in the buffer is extended as buffers are inserted.  It can be
 * snapshotted to use a segment of the buffer.
 */
struct rolling_buffer {
	struct folio_queue	*head;		/* Producer's insertion point */
	struct folio_queue	*tail;		/* Consumer's removal point */
	struct iov_iter		iter;		/* Iterator tracking what's left in the buffer */
	u8			next_head_slot;	/* Next slot in ->head */
	u8			first_tail_slot; /* First slot in ->tail */
};

/*
 * Snapshot of a rolling buffer.
 */
struct rolling_buffer_snapshot {
	struct folio_queue	*curr_folioq;	/* Queue segment in which current folio resides */
	unsigned char		curr_slot;	/* Folio currently being read */
	unsigned char		curr_order;	/* Order of folio */
};

/* Marks to store per-folio in the internal folio_queue structs. */
#define ROLLBUF_MARK_1	BIT(0)
#define ROLLBUF_MARK_2	BIT(1)

int rolling_buffer_init(struct rolling_buffer *roll, unsigned int rreq_id,
			unsigned int direction);
int rolling_buffer_make_space(struct rolling_buffer *roll);
ssize_t rolling_buffer_load_from_ra(struct rolling_buffer *roll,
				    struct readahead_control *ractl,
				    struct folio_batch *put_batch);
ssize_t rolling_buffer_append(struct rolling_buffer *roll, struct folio *folio,
			      unsigned int flags);
struct folio_queue *rolling_buffer_delete_spent(struct rolling_buffer *roll);
void rolling_buffer_clear(struct rolling_buffer *roll);

static inline void rolling_buffer_advance(struct rolling_buffer *roll, size_t amount)
{
	iov_iter_advance(&roll->iter, amount);
}

#endif /* _ROLLING_BUFFER_H */
