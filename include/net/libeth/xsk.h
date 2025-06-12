/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef __LIBETH_XSK_H
#define __LIBETH_XSK_H

#include <net/libeth/xdp.h>
#include <net/xdp_sock_drv.h>

/* ``XDP_TXMD_FLAGS_VALID`` is defined only under ``CONFIG_XDP_SOCKETS`` */
#ifdef XDP_TXMD_FLAGS_VALID
static_assert(XDP_TXMD_FLAGS_VALID <= LIBETH_XDP_TX_XSKMD);
#endif

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

/* XSk TMO */

/**
 * libeth_xsktmo_req_csum - XSk Tx metadata op to request checksum offload
 * @csum_start: unused
 * @csum_offset: unused
 * @priv: &libeth_xdp_tx_desc from the filling helper
 *
 * Generic implementation of ::tmo_request_checksum. Works only when HW doesn't
 * require filling checksum offsets and other parameters beside the checksum
 * request bit.
 * Consider using within @libeth_xsktmo unless the driver requires HW-specific
 * callbacks.
 */
static inline void libeth_xsktmo_req_csum(u16 csum_start, u16 csum_offset,
					  void *priv)
{
	((struct libeth_xdp_tx_desc *)priv)->flags |= LIBETH_XDP_TX_CSUM;
}

/* Only to inline the callbacks below, use @libeth_xsktmo in drivers instead */
static const struct xsk_tx_metadata_ops __libeth_xsktmo = {
	.tmo_request_checksum	= libeth_xsktmo_req_csum,
};

/**
 * __libeth_xsk_xmit_fill_buf_md - internal helper to prepare XSk xmit w/meta
 * @xdesc: &xdp_desc from the XSk buffer pool
 * @sq: XDPSQ abstraction for the queue
 * @priv: XSk Tx metadata ops
 *
 * Same as __libeth_xsk_xmit_fill_buf(), but requests metadata pointer and
 * fills additional fields in &libeth_xdp_tx_desc to ask for metadata offload.
 *
 * Return: XDP Tx descriptor with the DMA, metadata request bits, and other
 * info to pass to the driver callback.
 */
static __always_inline struct libeth_xdp_tx_desc
__libeth_xsk_xmit_fill_buf_md(const struct xdp_desc *xdesc,
			      const struct libeth_xdpsq *sq,
			      u64 priv)
{
	const struct xsk_tx_metadata_ops *tmo = libeth_xdp_priv_to_ptr(priv);
	struct libeth_xdp_tx_desc desc;
	struct xdp_desc_ctx ctx;

	ctx = xsk_buff_raw_get_ctx(sq->pool, xdesc->addr);
	desc = (typeof(desc)){
		.addr	= ctx.dma,
		.len	= xdesc->len,
	};

	BUILD_BUG_ON(!__builtin_constant_p(tmo == libeth_xsktmo));
	tmo = tmo == libeth_xsktmo ? &__libeth_xsktmo : tmo;

	xsk_tx_metadata_request(ctx.meta, tmo, &desc);

	return desc;
}

/* XSk xmit implementation */

/**
 * __libeth_xsk_xmit_fill_buf - internal helper to prepare XSk xmit w/o meta
 * @xdesc: &xdp_desc from the XSk buffer pool
 * @sq: XDPSQ abstraction for the queue
 *
 * Return: XDP Tx descriptor with the DMA and other info to pass to
 * the driver callback.
 */
static inline struct libeth_xdp_tx_desc
__libeth_xsk_xmit_fill_buf(const struct xdp_desc *xdesc,
			   const struct libeth_xdpsq *sq)
{
	return (struct libeth_xdp_tx_desc){
		.addr	= xsk_buff_raw_get_dma(sq->pool, xdesc->addr),
		.len	= xdesc->len,
	};
}

/**
 * libeth_xsk_xmit_fill_buf - internal helper to prepare an XSk xmit
 * @frm: &xdp_desc from the XSk buffer pool
 * @i: index on the HW queue
 * @sq: XDPSQ abstraction for the queue
 * @priv: XSk Tx metadata ops
 *
 * Depending on the metadata ops presence (determined at compile time), calls
 * the quickest helper to build a libeth XDP Tx descriptor.
 *
 * Return: XDP Tx descriptor with the synced DMA, metadata request bits,
 * and other info to pass to the driver callback.
 */
static __always_inline struct libeth_xdp_tx_desc
libeth_xsk_xmit_fill_buf(struct libeth_xdp_tx_frame frm, u32 i,
			 const struct libeth_xdpsq *sq, u64 priv)
{
	struct libeth_xdp_tx_desc desc;

	if (priv)
		desc = __libeth_xsk_xmit_fill_buf_md(&frm.desc, sq, priv);
	else
		desc = __libeth_xsk_xmit_fill_buf(&frm.desc, sq);

	desc.flags |= xsk_is_eop_desc(&frm.desc) ? LIBETH_XDP_TX_LAST : 0;

	xsk_buff_raw_dma_sync_for_device(sq->pool, desc.addr, desc.len);

	return desc;
}

/**
 * libeth_xsk_xmit_do_bulk - send XSk xmit frames
 * @pool: XSk buffer pool containing the frames to send
 * @xdpsq: opaque pointer to driver's XDPSQ struct
 * @budget: maximum number of frames can be sent
 * @tmo: optional XSk Tx metadata ops
 * @prep: driver callback to build a &libeth_xdpsq
 * @xmit: driver callback to put frames to a HW queue
 * @finalize: driver callback to start a transmission
 *
 * Implements generic XSk xmit. Always turns on XSk Tx wakeup as it's assumed
 * lazy cleaning is used and interrupts are disabled for the queue.
 * HW descriptor filling is unrolled by ``LIBETH_XDP_TX_BATCH`` to optimize
 * writes.
 * Note that unlike other XDP Tx ops, the queue must be locked and cleaned
 * prior to calling this function to already know available @budget.
 * @prepare must only build a &libeth_xdpsq and return ``U32_MAX``.
 *
 * Return: false if @budget was exhausted, true otherwise.
 */
static __always_inline bool
libeth_xsk_xmit_do_bulk(struct xsk_buff_pool *pool, void *xdpsq, u32 budget,
			const struct xsk_tx_metadata_ops *tmo,
			u32 (*prep)(void *xdpsq, struct libeth_xdpsq *sq),
			void (*xmit)(struct libeth_xdp_tx_desc desc, u32 i,
				     const struct libeth_xdpsq *sq, u64 priv),
			void (*finalize)(void *xdpsq, bool sent, bool flush))
{
	const struct libeth_xdp_tx_frame *bulk;
	bool wake;
	u32 n;

	wake = xsk_uses_need_wakeup(pool);
	if (wake)
		xsk_clear_tx_need_wakeup(pool);

	n = xsk_tx_peek_release_desc_batch(pool, budget);
	bulk = container_of(&pool->tx_descs[0], typeof(*bulk), desc);

	libeth_xdp_tx_xmit_bulk(bulk, xdpsq, n, true,
				libeth_xdp_ptr_to_priv(tmo), prep,
				libeth_xsk_xmit_fill_buf, xmit);
	finalize(xdpsq, n, true);

	if (wake)
		xsk_set_tx_need_wakeup(pool);

	return n < budget;
}

#endif /* __LIBETH_XSK_H */
