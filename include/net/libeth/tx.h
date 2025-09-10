/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2024-2025 Intel Corporation */

#ifndef __LIBETH_TX_H
#define __LIBETH_TX_H

#include <linux/skbuff.h>

#include <net/libeth/types.h>

/* Tx buffer completion */

/**
 * enum libeth_sqe_type - type of &libeth_sqe to act on Tx completion
 * @LIBETH_SQE_EMPTY: unused/empty OR XDP_TX/XSk frame, no action required
 * @LIBETH_SQE_CTX: context descriptor with empty SQE, no action required
 * @LIBETH_SQE_SLAB: kmalloc-allocated buffer, unmap and kfree()
 * @LIBETH_SQE_FRAG: mapped skb frag, only unmap DMA
 * @LIBETH_SQE_SKB: &sk_buff, unmap and napi_consume_skb(), update stats
 * @__LIBETH_SQE_XDP_START: separator between skb and XDP types
 * @LIBETH_SQE_XDP_TX: &skb_shared_info, libeth_xdp_return_buff_bulk(), stats
 * @LIBETH_SQE_XDP_XMIT: &xdp_frame, unmap and xdp_return_frame_bulk(), stats
 * @LIBETH_SQE_XDP_XMIT_FRAG: &xdp_frame frag, only unmap DMA
 * @LIBETH_SQE_XSK_TX: &libeth_xdp_buff on XSk queue, xsk_buff_free(), stats
 * @LIBETH_SQE_XSK_TX_FRAG: &libeth_xdp_buff frag on XSk queue, xsk_buff_free()
 */
enum libeth_sqe_type {
	LIBETH_SQE_EMPTY		= 0U,
	LIBETH_SQE_CTX,
	LIBETH_SQE_SLAB,
	LIBETH_SQE_FRAG,
	LIBETH_SQE_SKB,

	__LIBETH_SQE_XDP_START,
	LIBETH_SQE_XDP_TX		= __LIBETH_SQE_XDP_START,
	LIBETH_SQE_XDP_XMIT,
	LIBETH_SQE_XDP_XMIT_FRAG,
	LIBETH_SQE_XSK_TX,
	LIBETH_SQE_XSK_TX_FRAG,
};

/**
 * struct libeth_sqe - represents a Send Queue Element / Tx buffer
 * @type: type of the buffer, see the enum above
 * @rs_idx: index of the last buffer from the batch this one was sent in
 * @raw: slab buffer to free via kfree()
 * @skb: &sk_buff to consume
 * @sinfo: skb shared info of an XDP_TX frame
 * @xdpf: XDP frame from ::ndo_xdp_xmit()
 * @xsk: XSk Rx frame from XDP_TX action
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
		struct skb_shared_info		*sinfo;
		struct xdp_frame		*xdpf;
		struct libeth_xdp_buff		*xsk;
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
 * @bq: XDP frame bulk to combine return operations
 * @ss: onstack NAPI stats to fill
 * @xss: onstack XDPSQ NAPI stats to fill
 * @xdp_tx: number of XDP-not-XSk frames processed
 * @napi: whether it's called from the NAPI context
 *
 * libeth uses this structure to access objects needed for performing full
 * Tx complete operation without passing lots of arguments and change the
 * prototypes each time a new one is added.
 */
struct libeth_cq_pp {
	struct device			*dev;
	struct xdp_frame_bulk		*bq;

	union {
		struct libeth_sq_napi_stats	*ss;
		struct libeth_xdpsq_napi_stats	*xss;
	};
	u32				xdp_tx;

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

void libeth_tx_complete_any(struct libeth_sqe *sqe, struct libeth_cq_pp *cp);

#endif /* __LIBETH_TX_H */
