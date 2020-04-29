// SPDX-License-Identifier: GPL-2.0
/* XDP user-space ring structure
 * Copyright(c) 2018 Intel Corporation.
 */

#include <linux/log2.h>
#include <linux/slab.h>
#include <linux/overflow.h>

#include "xsk_queue.h"

void xskq_set_umem(struct xsk_queue *q, u64 size, u64 chunk_mask)
{
	if (!q)
		return;

	q->size = size;
	q->chunk_mask = chunk_mask;
}

static size_t xskq_get_ring_size(struct xsk_queue *q, bool umem_queue)
{
	struct xdp_umem_ring *umem_ring;
	struct xdp_rxtx_ring *rxtx_ring;

	if (umem_queue)
		return struct_size(umem_ring, desc, q->nentries);
	return struct_size(rxtx_ring, desc, q->nentries);
}

struct xsk_queue *xskq_create(u32 nentries, bool umem_queue)
{
	struct xsk_queue *q;
	gfp_t gfp_flags;
	size_t size;

	q = kzalloc(sizeof(*q), GFP_KERNEL);
	if (!q)
		return NULL;

	q->nentries = nentries;
	q->ring_mask = nentries - 1;

	gfp_flags = GFP_KERNEL | __GFP_ZERO | __GFP_NOWARN |
		    __GFP_COMP  | __GFP_NORETRY;
	size = xskq_get_ring_size(q, umem_queue);

	q->ring = (struct xdp_ring *)__get_free_pages(gfp_flags,
						      get_order(size));
	if (!q->ring) {
		kfree(q);
		return NULL;
	}

	return q;
}

void xskq_destroy(struct xsk_queue *q)
{
	if (!q)
		return;

	page_frag_free(q->ring);
	kfree(q);
}

struct xdp_umem_fq_reuse *xsk_reuseq_prepare(u32 nentries)
{
	struct xdp_umem_fq_reuse *newq;

	/* Check for overflow */
	if (nentries > (u32)roundup_pow_of_two(nentries))
		return NULL;
	nentries = roundup_pow_of_two(nentries);

	newq = kvmalloc(struct_size(newq, handles, nentries), GFP_KERNEL);
	if (!newq)
		return NULL;
	memset(newq, 0, offsetof(typeof(*newq), handles));

	newq->nentries = nentries;
	return newq;
}
EXPORT_SYMBOL_GPL(xsk_reuseq_prepare);

struct xdp_umem_fq_reuse *xsk_reuseq_swap(struct xdp_umem *umem,
					  struct xdp_umem_fq_reuse *newq)
{
	struct xdp_umem_fq_reuse *oldq = umem->fq_reuse;

	if (!oldq) {
		umem->fq_reuse = newq;
		return NULL;
	}

	if (newq->nentries < oldq->length)
		return newq;

	memcpy(newq->handles, oldq->handles,
	       array_size(oldq->length, sizeof(u64)));
	newq->length = oldq->length;

	umem->fq_reuse = newq;
	return oldq;
}
EXPORT_SYMBOL_GPL(xsk_reuseq_swap);

void xsk_reuseq_free(struct xdp_umem_fq_reuse *rq)
{
	kvfree(rq);
}
EXPORT_SYMBOL_GPL(xsk_reuseq_free);

void xsk_reuseq_destroy(struct xdp_umem *umem)
{
	xsk_reuseq_free(umem->fq_reuse);
	umem->fq_reuse = NULL;
}
