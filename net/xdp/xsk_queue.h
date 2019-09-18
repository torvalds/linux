/* SPDX-License-Identifier: GPL-2.0 */
/* XDP user-space ring structure
 * Copyright(c) 2018 Intel Corporation.
 */

#ifndef _LINUX_XSK_QUEUE_H
#define _LINUX_XSK_QUEUE_H

#include <linux/types.h>
#include <linux/if_xdp.h>
#include <net/xdp_sock.h>

#define RX_BATCH_SIZE 16
#define LAZY_UPDATE_THRESHOLD 128

struct xdp_ring {
	u32 producer ____cacheline_aligned_in_smp;
	u32 consumer ____cacheline_aligned_in_smp;
	u32 flags;
};

/* Used for the RX and TX queues for packets */
struct xdp_rxtx_ring {
	struct xdp_ring ptrs;
	struct xdp_desc desc[0] ____cacheline_aligned_in_smp;
};

/* Used for the fill and completion queues for buffers */
struct xdp_umem_ring {
	struct xdp_ring ptrs;
	u64 desc[0] ____cacheline_aligned_in_smp;
};

struct xsk_queue {
	u64 chunk_mask;
	u64 size;
	u32 ring_mask;
	u32 nentries;
	u32 prod_head;
	u32 prod_tail;
	u32 cons_head;
	u32 cons_tail;
	struct xdp_ring *ring;
	u64 invalid_descs;
};

/* The structure of the shared state of the rings are the same as the
 * ring buffer in kernel/events/ring_buffer.c. For the Rx and completion
 * ring, the kernel is the producer and user space is the consumer. For
 * the Tx and fill rings, the kernel is the consumer and user space is
 * the producer.
 *
 * producer                         consumer
 *
 * if (LOAD ->consumer) {           LOAD ->producer
 *                    (A)           smp_rmb()       (C)
 *    STORE $data                   LOAD $data
 *    smp_wmb()       (B)           smp_mb()        (D)
 *    STORE ->producer              STORE ->consumer
 * }
 *
 * (A) pairs with (D), and (B) pairs with (C).
 *
 * Starting with (B), it protects the data from being written after
 * the producer pointer. If this barrier was missing, the consumer
 * could observe the producer pointer being set and thus load the data
 * before the producer has written the new data. The consumer would in
 * this case load the old data.
 *
 * (C) protects the consumer from speculatively loading the data before
 * the producer pointer actually has been read. If we do not have this
 * barrier, some architectures could load old data as speculative loads
 * are not discarded as the CPU does not know there is a dependency
 * between ->producer and data.
 *
 * (A) is a control dependency that separates the load of ->consumer
 * from the stores of $data. In case ->consumer indicates there is no
 * room in the buffer to store $data we do not. So no barrier is needed.
 *
 * (D) protects the load of the data to be observed to happen after the
 * store of the consumer pointer. If we did not have this memory
 * barrier, the producer could observe the consumer pointer being set
 * and overwrite the data with a new value before the consumer got the
 * chance to read the old value. The consumer would thus miss reading
 * the old entry and very likely read the new entry twice, once right
 * now and again after circling through the ring.
 */

/* Common functions operating for both RXTX and umem queues */

static inline u64 xskq_nb_invalid_descs(struct xsk_queue *q)
{
	return q ? q->invalid_descs : 0;
}

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

static inline bool xskq_has_addrs(struct xsk_queue *q, u32 cnt)
{
	u32 entries = q->prod_tail - q->cons_tail;

	if (entries >= cnt)
		return true;

	/* Refresh the local pointer. */
	q->prod_tail = READ_ONCE(q->ring->producer);
	entries = q->prod_tail - q->cons_tail;

	return entries >= cnt;
}

/* UMEM queue */

static inline bool xskq_crosses_non_contig_pg(struct xdp_umem *umem, u64 addr,
					      u64 length)
{
	bool cross_pg = (addr & (PAGE_SIZE - 1)) + length > PAGE_SIZE;
	bool next_pg_contig =
		(unsigned long)umem->pages[(addr >> PAGE_SHIFT)].addr &
			XSK_NEXT_PG_CONTIG_MASK;

	return cross_pg && !next_pg_contig;
}

static inline bool xskq_is_valid_addr(struct xsk_queue *q, u64 addr)
{
	if (addr >= q->size) {
		q->invalid_descs++;
		return false;
	}

	return true;
}

static inline bool xskq_is_valid_addr_unaligned(struct xsk_queue *q, u64 addr,
						u64 length,
						struct xdp_umem *umem)
{
	u64 base_addr = xsk_umem_extract_addr(addr);

	addr = xsk_umem_add_offset_to_addr(addr);
	if (base_addr >= q->size || addr >= q->size ||
	    xskq_crosses_non_contig_pg(umem, addr, length)) {
		q->invalid_descs++;
		return false;
	}

	return true;
}

static inline u64 *xskq_validate_addr(struct xsk_queue *q, u64 *addr,
				      struct xdp_umem *umem)
{
	while (q->cons_tail != q->cons_head) {
		struct xdp_umem_ring *ring = (struct xdp_umem_ring *)q->ring;
		unsigned int idx = q->cons_tail & q->ring_mask;

		*addr = READ_ONCE(ring->desc[idx]) & q->chunk_mask;

		if (umem->flags & XDP_UMEM_UNALIGNED_CHUNK_FLAG) {
			if (xskq_is_valid_addr_unaligned(q, *addr,
							 umem->chunk_size_nohr,
							 umem))
				return addr;
			goto out;
		}

		if (xskq_is_valid_addr(q, *addr))
			return addr;

out:
		q->cons_tail++;
	}

	return NULL;
}

static inline u64 *xskq_peek_addr(struct xsk_queue *q, u64 *addr,
				  struct xdp_umem *umem)
{
	if (q->cons_tail == q->cons_head) {
		smp_mb(); /* D, matches A */
		WRITE_ONCE(q->ring->consumer, q->cons_tail);
		q->cons_head = q->cons_tail + xskq_nb_avail(q, RX_BATCH_SIZE);

		/* Order consumer and data */
		smp_rmb();
	}

	return xskq_validate_addr(q, addr, umem);
}

static inline void xskq_discard_addr(struct xsk_queue *q)
{
	q->cons_tail++;
}

static inline int xskq_produce_addr(struct xsk_queue *q, u64 addr)
{
	struct xdp_umem_ring *ring = (struct xdp_umem_ring *)q->ring;

	if (xskq_nb_free(q, q->prod_tail, 1) == 0)
		return -ENOSPC;

	/* A, matches D */
	ring->desc[q->prod_tail++ & q->ring_mask] = addr;

	/* Order producer and data */
	smp_wmb(); /* B, matches C */

	WRITE_ONCE(q->ring->producer, q->prod_tail);
	return 0;
}

static inline int xskq_produce_addr_lazy(struct xsk_queue *q, u64 addr)
{
	struct xdp_umem_ring *ring = (struct xdp_umem_ring *)q->ring;

	if (xskq_nb_free(q, q->prod_head, LAZY_UPDATE_THRESHOLD) == 0)
		return -ENOSPC;

	/* A, matches D */
	ring->desc[q->prod_head++ & q->ring_mask] = addr;
	return 0;
}

static inline void xskq_produce_flush_addr_n(struct xsk_queue *q,
					     u32 nb_entries)
{
	/* Order producer and data */
	smp_wmb(); /* B, matches C */

	q->prod_tail += nb_entries;
	WRITE_ONCE(q->ring->producer, q->prod_tail);
}

static inline int xskq_reserve_addr(struct xsk_queue *q)
{
	if (xskq_nb_free(q, q->prod_head, 1) == 0)
		return -ENOSPC;

	/* A, matches D */
	q->prod_head++;
	return 0;
}

/* Rx/Tx queue */

static inline bool xskq_is_valid_desc(struct xsk_queue *q, struct xdp_desc *d,
				      struct xdp_umem *umem)
{
	if (umem->flags & XDP_UMEM_UNALIGNED_CHUNK_FLAG) {
		if (!xskq_is_valid_addr_unaligned(q, d->addr, d->len, umem))
			return false;

		if (d->len > umem->chunk_size_nohr || d->options) {
			q->invalid_descs++;
			return false;
		}

		return true;
	}

	if (!xskq_is_valid_addr(q, d->addr))
		return false;

	if (((d->addr + d->len) & q->chunk_mask) != (d->addr & q->chunk_mask) ||
	    d->options) {
		q->invalid_descs++;
		return false;
	}

	return true;
}

static inline struct xdp_desc *xskq_validate_desc(struct xsk_queue *q,
						  struct xdp_desc *desc,
						  struct xdp_umem *umem)
{
	while (q->cons_tail != q->cons_head) {
		struct xdp_rxtx_ring *ring = (struct xdp_rxtx_ring *)q->ring;
		unsigned int idx = q->cons_tail & q->ring_mask;

		*desc = READ_ONCE(ring->desc[idx]);
		if (xskq_is_valid_desc(q, desc, umem))
			return desc;

		q->cons_tail++;
	}

	return NULL;
}

static inline struct xdp_desc *xskq_peek_desc(struct xsk_queue *q,
					      struct xdp_desc *desc,
					      struct xdp_umem *umem)
{
	if (q->cons_tail == q->cons_head) {
		smp_mb(); /* D, matches A */
		WRITE_ONCE(q->ring->consumer, q->cons_tail);
		q->cons_head = q->cons_tail + xskq_nb_avail(q, RX_BATCH_SIZE);

		/* Order consumer and data */
		smp_rmb(); /* C, matches B */
	}

	return xskq_validate_desc(q, desc, umem);
}

static inline void xskq_discard_desc(struct xsk_queue *q)
{
	q->cons_tail++;
}

static inline int xskq_produce_batch_desc(struct xsk_queue *q,
					  u64 addr, u32 len)
{
	struct xdp_rxtx_ring *ring = (struct xdp_rxtx_ring *)q->ring;
	unsigned int idx;

	if (xskq_nb_free(q, q->prod_head, 1) == 0)
		return -ENOSPC;

	/* A, matches D */
	idx = (q->prod_head++) & q->ring_mask;
	ring->desc[idx].addr = addr;
	ring->desc[idx].len = len;

	return 0;
}

static inline void xskq_produce_flush_desc(struct xsk_queue *q)
{
	/* Order producer and data */
	smp_wmb(); /* B, matches C */

	q->prod_tail = q->prod_head;
	WRITE_ONCE(q->ring->producer, q->prod_tail);
}

static inline bool xskq_full_desc(struct xsk_queue *q)
{
	return xskq_nb_avail(q, q->nentries) == q->nentries;
}

static inline bool xskq_empty_desc(struct xsk_queue *q)
{
	return xskq_nb_free(q, q->prod_tail, q->nentries) == q->nentries;
}

void xskq_set_umem(struct xsk_queue *q, u64 size, u64 chunk_mask);
struct xsk_queue *xskq_create(u32 nentries, bool umem_queue);
void xskq_destroy(struct xsk_queue *q_ops);

/* Executed by the core when the entire UMEM gets freed */
void xsk_reuseq_destroy(struct xdp_umem *umem);

#endif /* _LINUX_XSK_QUEUE_H */
