/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef __LIBETH_XSK_H
#define __LIBETH_XSK_H

#include <net/libeth/xdp.h>
#include <net/xdp_sock_drv.h>

/* ``XDP_TX`` bulking */

/**
 * libeth_xsk_tx_queue_head - internal helper for queueing XSk ``XDP_TX`` head
 * @bq: XDP Tx bulk to queue the head frag to
 * @xdp: XSk buffer with the head to queue
 *
 * Return: false if it's the only frag of the frame, true if it's an S/G frame.
 */
static inline bool libeth_xsk_tx_queue_head(struct libeth_xdp_tx_bulk *bq,
					    struct libeth_xdp_buff *xdp)
{
	bq->bulk[bq->count++] = (typeof(*bq->bulk)){
		.xsk	= xdp,
		.len	= xdp->base.data_end - xdp->data,
		.flags	= LIBETH_XDP_TX_FIRST,
	};

	if (likely(!xdp_buff_has_frags(&xdp->base)))
		return false;

	bq->bulk[bq->count - 1].flags |= LIBETH_XDP_TX_MULTI;

	return true;
}

/**
 * libeth_xsk_tx_queue_frag - internal helper for queueing XSk ``XDP_TX`` frag
 * @bq: XDP Tx bulk to queue the frag to
 * @frag: XSk frag to queue
 */
static inline void libeth_xsk_tx_queue_frag(struct libeth_xdp_tx_bulk *bq,
					    struct libeth_xdp_buff *frag)
{
	bq->bulk[bq->count++] = (typeof(*bq->bulk)){
		.xsk	= frag,
		.len	= frag->base.data_end - frag->data,
	};
}

/**
 * libeth_xsk_tx_queue_bulk - internal helper for queueing XSk ``XDP_TX`` frame
 * @bq: XDP Tx bulk to queue the frame to
 * @xdp: XSk buffer to queue
 * @flush_bulk: driver callback to flush the bulk to the HW queue
 *
 * Return: true on success, false on flush error.
 */
static __always_inline bool
libeth_xsk_tx_queue_bulk(struct libeth_xdp_tx_bulk *bq,
			 struct libeth_xdp_buff *xdp,
			 bool (*flush_bulk)(struct libeth_xdp_tx_bulk *bq,
					    u32 flags))
{
	bool ret = true;

	if (unlikely(bq->count == LIBETH_XDP_TX_BULK) &&
	    unlikely(!flush_bulk(bq, LIBETH_XDP_TX_XSK))) {
		libeth_xsk_buff_free_slow(xdp);
		return false;
	}

	if (!libeth_xsk_tx_queue_head(bq, xdp))
		goto out;

	for (const struct libeth_xdp_buff *head = xdp; ; ) {
		xdp = container_of(xsk_buff_get_frag(&head->base),
				   typeof(*xdp), base);
		if (!xdp)
			break;

		if (unlikely(bq->count == LIBETH_XDP_TX_BULK) &&
		    unlikely(!flush_bulk(bq, LIBETH_XDP_TX_XSK))) {
			ret = false;
			break;
		}

		libeth_xsk_tx_queue_frag(bq, xdp);
	}

out:
	bq->bulk[bq->count - 1].flags |= LIBETH_XDP_TX_LAST;

	return ret;
}

/**
 * libeth_xsk_tx_fill_buf - internal helper to fill XSk ``XDP_TX`` &libeth_sqe
 * @frm: XDP Tx frame from the bulk
 * @i: index on the HW queue
 * @sq: XDPSQ abstraction for the queue
 * @priv: private data
 *
 * Return: XDP Tx descriptor with the synced DMA and other info to pass to
 * the driver callback.
 */
static inline struct libeth_xdp_tx_desc
libeth_xsk_tx_fill_buf(struct libeth_xdp_tx_frame frm, u32 i,
		       const struct libeth_xdpsq *sq, u64 priv)
{
	struct libeth_xdp_buff *xdp = frm.xsk;
	struct libeth_xdp_tx_desc desc = {
		.addr	= xsk_buff_xdp_get_dma(&xdp->base),
		.opts	= frm.opts,
	};
	struct libeth_sqe *sqe;

	xsk_buff_raw_dma_sync_for_device(sq->pool, desc.addr, desc.len);

	sqe = &sq->sqes[i];
	sqe->xsk = xdp;

	if (!(desc.flags & LIBETH_XDP_TX_FIRST)) {
		sqe->type = LIBETH_SQE_XSK_TX_FRAG;
		return desc;
	}

	sqe->type = LIBETH_SQE_XSK_TX;
	libeth_xdp_tx_fill_stats(sqe, &desc,
				 xdp_get_shared_info_from_buff(&xdp->base));

	return desc;
}

/**
 * libeth_xsk_tx_flush_bulk - wrapper to define flush of XSk ``XDP_TX`` bulk
 * @bq: bulk to flush
 * @flags: Tx flags, see __libeth_xdp_tx_flush_bulk()
 * @prep: driver callback to prepare the queue
 * @xmit: driver callback to fill a HW descriptor
 *
 * Use via LIBETH_XSK_DEFINE_FLUSH_TX() to define an XSk ``XDP_TX`` driver
 * callback.
 */
#define libeth_xsk_tx_flush_bulk(bq, flags, prep, xmit)			     \
	__libeth_xdp_tx_flush_bulk(bq, (flags) | LIBETH_XDP_TX_XSK, prep,    \
				   libeth_xsk_tx_fill_buf, xmit)

#endif /* __LIBETH_XSK_H */
