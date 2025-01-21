/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2024 Intel Corporation */

#ifndef __LIBETH_TX_H
#define __LIBETH_TX_H

#include <linux/skbuff.h>

#include <net/libeth/types.h>

/* Tx buffer completion */

/**
 * enum libeth_sqe_type - type of &libeth_sqe to act on Tx completion
 * @LIBETH_SQE_EMPTY: unused/empty, no action required
 * @LIBETH_SQE_CTX: context descriptor with empty SQE, no action required
 * @LIBETH_SQE_SLAB: kmalloc-allocated buffer, unmap and kfree()
 * @LIBETH_SQE_FRAG: mapped skb frag, only unmap DMA
 * @LIBETH_SQE_SKB: &sk_buff, unmap and napi_consume_skb(), update stats
 */
enum libeth_sqe_type {
	LIBETH_SQE_EMPTY		= 0U,
	LIBETH_SQE_CTX,
	LIBETH_SQE_SLAB,
	LIBETH_SQE_FRAG,
	LIBETH_SQE_SKB,
};

/**
 * struct libeth_sqe - represents a Send Queue Element / Tx buffer
 * @type: type of the buffer, see the enum above
 * @rs_idx: index of the last buffer from the batch this one was sent in
 * @raw: slab buffer to free via kfree()
 * @skb: &sk_buff to consume
 * @dma: DMA address to unmap
 * @len: length of the mapped region to unmap
 * @nr_frags: number of frags in the frame this buffer belongs to
 * @packets: number of physical packets sent for this frame
 * @bytes: number of physical bytes sent for this frame
 * @priv: driver-private scratchpad
 */
struct libeth_sqe {
	enum libeth_sqe_type		type:32;
	u32				rs_idx;

	union {
		void				*raw;
		struct sk_buff			*skb;
	};

	DEFINE_DMA_UNMAP_ADDR(dma);
	DEFINE_DMA_UNMAP_LEN(len);

	u32				nr_frags;
	u32				packets;
	u32				bytes;

	unsigned long			priv;
} __aligned_largest;

/**
 * LIBETH_SQE_CHECK_PRIV - check the driver's private SQE data
 * @p: type or name of the object the driver wants to fit into &libeth_sqe
 *
 * Make sure the driver's private data fits into libeth_sqe::priv. To be used
 * right after its declaration.
 */
#define LIBETH_SQE_CHECK_PRIV(p)					  \
	static_assert(sizeof(p) <= sizeof_field(struct libeth_sqe, priv))

/**
 * struct libeth_cq_pp - completion queue poll params
 * @dev: &device to perform DMA unmapping
 * @ss: onstack NAPI stats to fill
 * @napi: whether it's called from the NAPI context
 *
 * libeth uses this structure to access objects needed for performing full
 * Tx complete operation without passing lots of arguments and change the
 * prototypes each time a new one is added.
 */
struct libeth_cq_pp {
	struct device			*dev;
	struct libeth_sq_napi_stats	*ss;

	bool				napi;
};

/**
 * libeth_tx_complete - perform Tx completion for one SQE
 * @sqe: SQE to complete
 * @cp: poll params
 *
 * Do Tx complete for all the types of buffers, incl. freeing, unmapping,
 * updating the stats etc.
 */
static inline void libeth_tx_complete(struct libeth_sqe *sqe,
				      const struct libeth_cq_pp *cp)
{
	switch (sqe->type) {
	case LIBETH_SQE_EMPTY:
		return;
	case LIBETH_SQE_SKB:
	case LIBETH_SQE_FRAG:
	case LIBETH_SQE_SLAB:
		dma_unmap_page(cp->dev, dma_unmap_addr(sqe, dma),
			       dma_unmap_len(sqe, len), DMA_TO_DEVICE);
		break;
	default:
		break;
	}

	switch (sqe->type) {
	case LIBETH_SQE_SKB:
		cp->ss->packets += sqe->packets;
		cp->ss->bytes += sqe->bytes;

		napi_consume_skb(sqe->skb, cp->napi);
		break;
	case LIBETH_SQE_SLAB:
		kfree(sqe->raw);
		break;
	default:
		break;
	}

	sqe->type = LIBETH_SQE_EMPTY;
}

#endif /* __LIBETH_TX_H */
