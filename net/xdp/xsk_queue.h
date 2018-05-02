/* SPDX-License-Identifier: GPL-2.0
 * XDP user-space ring structure
 * Copyright(c) 2018 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _LINUX_XSK_QUEUE_H
#define _LINUX_XSK_QUEUE_H

#include <linux/types.h>
#include <linux/if_xdp.h>

#include "xdp_umem_props.h"

#define RX_BATCH_SIZE 16

struct xsk_queue {
	struct xdp_umem_props umem_props;
	u32 ring_mask;
	u32 nentries;
	u32 prod_head;
	u32 prod_tail;
	u32 cons_head;
	u32 cons_tail;
	struct xdp_ring *ring;
	u64 invalid_descs;
};

/* Common functions operating for both RXTX and umem queues */

static inline u32 xskq_nb_avail(struct xsk_queue *q, u32 dcnt)
{
	u32 entries = q->prod_tail - q->cons_tail;

	if (entries == 0) {
		/* Refresh the local pointer */
		q->prod_tail = READ_ONCE(q->ring->producer);
		entries = q->prod_tail - q->cons_tail;
	}

	return (entries > dcnt) ? dcnt : entries;
}

static inline u32 xskq_nb_free(struct xsk_queue *q, u32 producer, u32 dcnt)
{
	u32 free_entries = q->nentries - (producer - q->cons_tail);

	if (free_entries >= dcnt)
		return free_entries;

	/* Refresh the local tail pointer */
	q->cons_tail = READ_ONCE(q->ring->consumer);
	return q->nentries - (producer - q->cons_tail);
}

/* UMEM queue */

static inline bool xskq_is_valid_id(struct xsk_queue *q, u32 idx)
{
	if (unlikely(idx >= q->umem_props.nframes)) {
		q->invalid_descs++;
		return false;
	}
	return true;
}

static inline u32 *xskq_validate_id(struct xsk_queue *q)
{
	while (q->cons_tail != q->cons_head) {
		struct xdp_umem_ring *ring = (struct xdp_umem_ring *)q->ring;
		unsigned int idx = q->cons_tail & q->ring_mask;

		if (xskq_is_valid_id(q, ring->desc[idx]))
			return &ring->desc[idx];

		q->cons_tail++;
	}

	return NULL;
}

static inline u32 *xskq_peek_id(struct xsk_queue *q)
{
	struct xdp_umem_ring *ring;

	if (q->cons_tail == q->cons_head) {
		WRITE_ONCE(q->ring->consumer, q->cons_tail);
		q->cons_head = q->cons_tail + xskq_nb_avail(q, RX_BATCH_SIZE);

		/* Order consumer and data */
		smp_rmb();

		return xskq_validate_id(q);
	}

	ring = (struct xdp_umem_ring *)q->ring;
	return &ring->desc[q->cons_tail & q->ring_mask];
}

static inline void xskq_discard_id(struct xsk_queue *q)
{
	q->cons_tail++;
	(void)xskq_validate_id(q);
}

static inline int xskq_produce_id(struct xsk_queue *q, u32 id)
{
	struct xdp_umem_ring *ring = (struct xdp_umem_ring *)q->ring;

	ring->desc[q->prod_tail++ & q->ring_mask] = id;

	/* Order producer and data */
	smp_wmb();

	WRITE_ONCE(q->ring->producer, q->prod_tail);
	return 0;
}

static inline int xskq_reserve_id(struct xsk_queue *q)
{
	if (xskq_nb_free(q, q->prod_head, 1) == 0)
		return -ENOSPC;

	q->prod_head++;
	return 0;
}

/* Rx/Tx queue */

static inline bool xskq_is_valid_desc(struct xsk_queue *q, struct xdp_desc *d)
{
	u32 buff_len;

	if (unlikely(d->idx >= q->umem_props.nframes)) {
		q->invalid_descs++;
		return false;
	}

	buff_len = q->umem_props.frame_size;
	if (unlikely(d->len > buff_len || d->len == 0 ||
		     d->offset > buff_len || d->offset + d->len > buff_len)) {
		q->invalid_descs++;
		return false;
	}

	return true;
}

static inline struct xdp_desc *xskq_validate_desc(struct xsk_queue *q,
						  struct xdp_desc *desc)
{
	while (q->cons_tail != q->cons_head) {
		struct xdp_rxtx_ring *ring = (struct xdp_rxtx_ring *)q->ring;
		unsigned int idx = q->cons_tail & q->ring_mask;

		if (xskq_is_valid_desc(q, &ring->desc[idx])) {
			if (desc)
				*desc = ring->desc[idx];
			return desc;
		}

		q->cons_tail++;
	}

	return NULL;
}

static inline struct xdp_desc *xskq_peek_desc(struct xsk_queue *q,
					      struct xdp_desc *desc)
{
	struct xdp_rxtx_ring *ring;

	if (q->cons_tail == q->cons_head) {
		WRITE_ONCE(q->ring->consumer, q->cons_tail);
		q->cons_head = q->cons_tail + xskq_nb_avail(q, RX_BATCH_SIZE);

		/* Order consumer and data */
		smp_rmb();

		return xskq_validate_desc(q, desc);
	}

	ring = (struct xdp_rxtx_ring *)q->ring;
	*desc = ring->desc[q->cons_tail & q->ring_mask];
	return desc;
}

static inline void xskq_discard_desc(struct xsk_queue *q)
{
	q->cons_tail++;
	(void)xskq_validate_desc(q, NULL);
}

static inline int xskq_produce_batch_desc(struct xsk_queue *q,
					  u32 id, u32 len, u16 offset)
{
	struct xdp_rxtx_ring *ring = (struct xdp_rxtx_ring *)q->ring;
	unsigned int idx;

	if (xskq_nb_free(q, q->prod_head, 1) == 0)
		return -ENOSPC;

	idx = (q->prod_head++) & q->ring_mask;
	ring->desc[idx].idx = id;
	ring->desc[idx].len = len;
	ring->desc[idx].offset = offset;

	return 0;
}

static inline void xskq_produce_flush_desc(struct xsk_queue *q)
{
	/* Order producer and data */
	smp_wmb();

	q->prod_tail = q->prod_head,
	WRITE_ONCE(q->ring->producer, q->prod_tail);
}

static inline bool xskq_full_desc(struct xsk_queue *q)
{
	return (xskq_nb_avail(q, q->nentries) == q->nentries);
}

static inline bool xskq_empty_desc(struct xsk_queue *q)
{
	return (xskq_nb_free(q, q->prod_tail, 1) == q->nentries);
}

void xskq_set_umem(struct xsk_queue *q, struct xdp_umem_props *umem_props);
struct xsk_queue *xskq_create(u32 nentries, bool umem_queue);
void xskq_destroy(struct xsk_queue *q_ops);

#endif /* _LINUX_XSK_QUEUE_H */
