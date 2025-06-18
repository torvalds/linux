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
		__libeth_xdp_tx_len(xdp->base.data_end - xdp->data,
				    LIBETH_XDP_TX_FIRST),
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
		__libeth_xdp_tx_len(frag->base.data_end - frag->data),
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
		__libeth_xdp_tx_len(xdesc->len),
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
		__libeth_xdp_tx_len(xdesc->len),
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

/* Rx polling path */

/**
 * libeth_xsk_tx_init_bulk - initialize XDP Tx bulk for an XSk Rx NAPI poll
 * @bq: bulk to initialize
 * @prog: RCU pointer to the XDP program (never %NULL)
 * @dev: target &net_device
 * @xdpsqs: array of driver XDPSQ structs
 * @num: number of active XDPSQs, the above array length
 *
 * Should be called on an onstack XDP Tx bulk before the XSk NAPI polling loop.
 * Initializes all the needed fields to run libeth_xdp functions.
 * Never checks if @prog is %NULL or @num == 0 as XDP must always be enabled
 * when hitting this path.
 */
#define libeth_xsk_tx_init_bulk(bq, prog, dev, xdpsqs, num)		     \
	__libeth_xdp_tx_init_bulk(bq, prog, dev, xdpsqs, num, true,	     \
				  __UNIQUE_ID(bq_), __UNIQUE_ID(nqs_))

struct libeth_xdp_buff *libeth_xsk_buff_add_frag(struct libeth_xdp_buff *head,
						 struct libeth_xdp_buff *xdp);

/**
 * libeth_xsk_process_buff - attach XSk Rx buffer to &libeth_xdp_buff
 * @head: head XSk buffer to attach the XSk buffer to (or %NULL)
 * @xdp: XSk buffer to process
 * @len: received data length from the descriptor
 *
 * If @head == %NULL, treats the XSk buffer as head and initializes
 * the required fields. Otherwise, attaches the buffer as a frag.
 * Already performs DMA sync-for-CPU and frame start prefetch
 * (for head buffers only).
 *
 * Return: head XSk buffer on success or if the descriptor must be skipped
 * (empty), %NULL if there is no space for a new frag.
 */
static inline struct libeth_xdp_buff *
libeth_xsk_process_buff(struct libeth_xdp_buff *head,
			struct libeth_xdp_buff *xdp, u32 len)
{
	if (unlikely(!len)) {
		libeth_xsk_buff_free_slow(xdp);
		return head;
	}

	xsk_buff_set_size(&xdp->base, len);
	xsk_buff_dma_sync_for_cpu(&xdp->base);

	if (head)
		return libeth_xsk_buff_add_frag(head, xdp);

	prefetch(xdp->data);

	return xdp;
}

void libeth_xsk_buff_stats_frags(struct libeth_rq_napi_stats *rs,
				 const struct libeth_xdp_buff *xdp);

u32 __libeth_xsk_run_prog_slow(struct libeth_xdp_buff *xdp,
			       const struct libeth_xdp_tx_bulk *bq,
			       enum xdp_action act, int ret);

/**
 * __libeth_xsk_run_prog - run XDP program on XSk buffer
 * @xdp: XSk buffer to run the prog on
 * @bq: buffer bulk for ``XDP_TX`` queueing
 *
 * Internal inline abstraction to run XDP program on XSk Rx path. Handles
 * only the most common ``XDP_REDIRECT`` inline, the rest is processed
 * externally.
 * Reports an XDP prog exception on errors.
 *
 * Return: libeth_xdp prog verdict depending on the prog's verdict.
 */
static __always_inline u32
__libeth_xsk_run_prog(struct libeth_xdp_buff *xdp,
		      const struct libeth_xdp_tx_bulk *bq)
{
	enum xdp_action act;
	int ret = 0;

	act = bpf_prog_run_xdp(bq->prog, &xdp->base);
	if (unlikely(act != XDP_REDIRECT))
rest:
		return __libeth_xsk_run_prog_slow(xdp, bq, act, ret);

	ret = xdp_do_redirect(bq->dev, &xdp->base, bq->prog);
	if (unlikely(ret))
		goto rest;

	return LIBETH_XDP_REDIRECT;
}

/**
 * libeth_xsk_run_prog - run XDP program on XSk path and handle all verdicts
 * @xdp: XSk buffer to process
 * @bq: XDP Tx bulk to queue ``XDP_TX`` buffers
 * @fl: driver ``XDP_TX`` bulk flush callback
 *
 * Run the attached XDP program and handle all possible verdicts.
 * Prefer using it via LIBETH_XSK_DEFINE_RUN{,_PASS,_PROG}().
 *
 * Return: libeth_xdp prog verdict depending on the prog's verdict.
 */
#define libeth_xsk_run_prog(xdp, bq, fl)				     \
	__libeth_xdp_run_flush(xdp, bq, __libeth_xsk_run_prog,		     \
			       libeth_xsk_tx_queue_bulk, fl)

/**
 * __libeth_xsk_run_pass - helper to run XDP program and handle the result
 * @xdp: XSk buffer to process
 * @bq: XDP Tx bulk to queue ``XDP_TX`` frames
 * @napi: NAPI to build an skb and pass it up the stack
 * @rs: onstack libeth RQ stats
 * @md: metadata that should be filled to the XSk buffer
 * @prep: callback for filling the metadata
 * @run: driver wrapper to run XDP program
 * @populate: driver callback to populate an skb with the HW descriptor data
 *
 * Inline abstraction, XSk's counterpart of __libeth_xdp_run_pass(), see its
 * doc for details.
 *
 * Return: false if the polling loop must be exited due to lack of free
 * buffers, true otherwise.
 */
static __always_inline bool
__libeth_xsk_run_pass(struct libeth_xdp_buff *xdp,
		      struct libeth_xdp_tx_bulk *bq, struct napi_struct *napi,
		      struct libeth_rq_napi_stats *rs, const void *md,
		      void (*prep)(struct libeth_xdp_buff *xdp,
				   const void *md),
		      u32 (*run)(struct libeth_xdp_buff *xdp,
				 struct libeth_xdp_tx_bulk *bq),
		      bool (*populate)(struct sk_buff *skb,
				       const struct libeth_xdp_buff *xdp,
				       struct libeth_rq_napi_stats *rs))
{
	struct sk_buff *skb;
	u32 act;

	rs->bytes += xdp->base.data_end - xdp->data;
	rs->packets++;

	if (unlikely(xdp_buff_has_frags(&xdp->base)))
		libeth_xsk_buff_stats_frags(rs, xdp);

	if (prep && (!__builtin_constant_p(!!md) || md))
		prep(xdp, md);

	act = run(xdp, bq);
	if (likely(act == LIBETH_XDP_REDIRECT))
		return true;

	if (act != LIBETH_XDP_PASS)
		return act != LIBETH_XDP_ABORTED;

	skb = xdp_build_skb_from_zc(&xdp->base);
	if (unlikely(!skb)) {
		libeth_xsk_buff_free_slow(xdp);
		return true;
	}

	if (unlikely(!populate(skb, xdp, rs))) {
		napi_consume_skb(skb, true);
		return true;
	}

	napi_gro_receive(napi, skb);

	return true;
}

/**
 * libeth_xsk_run_pass - helper to run XDP program and handle the result
 * @xdp: XSk buffer to process
 * @bq: XDP Tx bulk to queue ``XDP_TX`` frames
 * @napi: NAPI to build an skb and pass it up the stack
 * @rs: onstack libeth RQ stats
 * @desc: pointer to the HW descriptor for that frame
 * @run: driver wrapper to run XDP program
 * @populate: driver callback to populate an skb with the HW descriptor data
 *
 * Wrapper around the underscored version when "fill the descriptor metadata"
 * means just writing the pointer to the HW descriptor as @xdp->desc.
 */
#define libeth_xsk_run_pass(xdp, bq, napi, rs, desc, run, populate)	     \
	__libeth_xsk_run_pass(xdp, bq, napi, rs, desc, libeth_xdp_prep_desc, \
			      run, populate)

/**
 * libeth_xsk_finalize_rx - finalize XDPSQ after an XSk NAPI polling loop
 * @bq: ``XDP_TX`` frame bulk
 * @flush: driver callback to flush the bulk
 * @finalize: driver callback to start sending the frames and run the timer
 *
 * Flush the bulk if there are frames left to send, kick the queue and flush
 * the XDP maps.
 */
#define libeth_xsk_finalize_rx(bq, flush, finalize)			     \
	__libeth_xdp_finalize_rx(bq, LIBETH_XDP_TX_XSK, flush, finalize)

/*
 * Helpers to reduce boilerplate code in drivers.
 *
 * Typical driver XSk Rx flow would be (excl. bulk and buff init, frag attach):
 *
 * LIBETH_XDP_DEFINE_START();
 * LIBETH_XSK_DEFINE_FLUSH_TX(static driver_xsk_flush_tx, driver_xsk_tx_prep,
 *			      driver_xdp_xmit);
 * LIBETH_XSK_DEFINE_RUN(static driver_xsk_run, driver_xsk_run_prog,
 *			 driver_xsk_flush_tx, driver_populate_skb);
 * LIBETH_XSK_DEFINE_FINALIZE(static driver_xsk_finalize_rx,
 *			      driver_xsk_flush_tx, driver_xdp_finalize_sq);
 * LIBETH_XDP_DEFINE_END();
 *
 * This will build a set of 4 static functions. The compiler is free to decide
 * whether to inline them.
 * Then, in the NAPI polling function:
 *
 *	while (packets < budget) {
 *		// ...
 *		if (!driver_xsk_run(xdp, &bq, napi, &rs, desc))
 *			break;
 *	}
 *	driver_xsk_finalize_rx(&bq);
 */

/**
 * LIBETH_XSK_DEFINE_FLUSH_TX - define a driver XSk ``XDP_TX`` flush function
 * @name: name of the function to define
 * @prep: driver callback to clean an XDPSQ
 * @xmit: driver callback to write a HW Tx descriptor
 */
#define LIBETH_XSK_DEFINE_FLUSH_TX(name, prep, xmit)			     \
	__LIBETH_XDP_DEFINE_FLUSH_TX(name, prep, xmit, xsk)

/**
 * LIBETH_XSK_DEFINE_RUN_PROG - define a driver XDP program run function
 * @name: name of the function to define
 * @flush: driver callback to flush an XSk ``XDP_TX`` bulk
 */
#define LIBETH_XSK_DEFINE_RUN_PROG(name, flush)				     \
	u32 __LIBETH_XDP_DEFINE_RUN_PROG(name, flush, xsk)

/**
 * LIBETH_XSK_DEFINE_RUN_PASS - define a driver buffer process + pass function
 * @name: name of the function to define
 * @run: driver callback to run XDP program (above)
 * @populate: driver callback to fill an skb with HW descriptor info
 */
#define LIBETH_XSK_DEFINE_RUN_PASS(name, run, populate)			     \
	bool __LIBETH_XDP_DEFINE_RUN_PASS(name, run, populate, xsk)

/**
 * LIBETH_XSK_DEFINE_RUN - define a driver buffer process, run + pass function
 * @name: name of the function to define
 * @run: name of the XDP prog run function to define
 * @flush: driver callback to flush an XSk ``XDP_TX`` bulk
 * @populate: driver callback to fill an skb with HW descriptor info
 */
#define LIBETH_XSK_DEFINE_RUN(name, run, flush, populate)		     \
	__LIBETH_XDP_DEFINE_RUN(name, run, flush, populate, XSK)

/**
 * LIBETH_XSK_DEFINE_FINALIZE - define a driver XSk NAPI poll finalize function
 * @name: name of the function to define
 * @flush: driver callback to flush an XSk ``XDP_TX`` bulk
 * @finalize: driver callback to finalize an XDPSQ and run the timer
 */
#define LIBETH_XSK_DEFINE_FINALIZE(name, flush, finalize)		     \
	__LIBETH_XDP_DEFINE_FINALIZE(name, flush, finalize, xsk)

/* Refilling */

/**
 * struct libeth_xskfq - structure representing an XSk buffer (fill) queue
 * @fp: hotpath part of the structure
 * @pool: &xsk_buff_pool for buffer management
 * @fqes: array of XSk buffer pointers
 * @descs: opaque pointer to the HW descriptor array
 * @ntu: index of the next buffer to poll
 * @count: number of descriptors/buffers the queue has
 * @pending: current number of XSkFQEs to refill
 * @thresh: threshold below which the queue is refilled
 * @buf_len: HW-writeable length per each buffer
 * @nid: ID of the closest NUMA node with memory
 */
struct libeth_xskfq {
	struct_group_tagged(libeth_xskfq_fp, fp,
		struct xsk_buff_pool	*pool;
		struct libeth_xdp_buff	**fqes;
		void			*descs;

		u32			ntu;
		u32			count;
	);

	/* Cold fields */
	u32			pending;
	u32			thresh;

	u32			buf_len;
	int			nid;
};

int libeth_xskfq_create(struct libeth_xskfq *fq);
void libeth_xskfq_destroy(struct libeth_xskfq *fq);

/**
 * libeth_xsk_buff_xdp_get_dma - get DMA address of XSk &libeth_xdp_buff
 * @xdp: buffer to get the DMA addr for
 */
#define libeth_xsk_buff_xdp_get_dma(xdp)				     \
	xsk_buff_xdp_get_dma(&(xdp)->base)

/**
 * libeth_xskfqe_alloc - allocate @n XSk Rx buffers
 * @fq: hotpath part of the XSkFQ, usually onstack
 * @n: number of buffers to allocate
 * @fill: driver callback to write DMA addresses to HW descriptors
 *
 * Note that @fq->ntu gets updated, but ::pending must be recalculated
 * by the caller.
 *
 * Return: number of buffers refilled.
 */
static __always_inline u32
libeth_xskfqe_alloc(struct libeth_xskfq_fp *fq, u32 n,
		    void (*fill)(const struct libeth_xskfq_fp *fq, u32 i))
{
	u32 this, ret, done = 0;
	struct xdp_buff **xskb;

	this = fq->count - fq->ntu;
	if (likely(this > n))
		this = n;

again:
	xskb = (typeof(xskb))&fq->fqes[fq->ntu];
	ret = xsk_buff_alloc_batch(fq->pool, xskb, this);

	for (u32 i = 0, ntu = fq->ntu; likely(i < ret); i++)
		fill(fq, ntu + i);

	done += ret;
	fq->ntu += ret;

	if (likely(fq->ntu < fq->count) || unlikely(ret < this))
		goto out;

	fq->ntu = 0;

	if (this < n) {
		this = n - this;
		goto again;
	}

out:
	return done;
}

/* .ndo_xsk_wakeup */

void libeth_xsk_init_wakeup(call_single_data_t *csd, struct napi_struct *napi);
void libeth_xsk_wakeup(call_single_data_t *csd, u32 qid);

/* Pool setup */

int libeth_xsk_setup_pool(struct net_device *dev, u32 qid, bool enable);

#endif /* __LIBETH_XSK_H */
