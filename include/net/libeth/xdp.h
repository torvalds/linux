/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef __LIBETH_XDP_H
#define __LIBETH_XDP_H

#include <linux/bpf_trace.h>
#include <linux/unroll.h>

#include <net/libeth/rx.h>
#include <net/libeth/tx.h>
#include <net/xsk_buff_pool.h>

/*
 * Defined as bits to be able to use them as a mask on Rx.
 * Also used as internal return values on Tx.
 */
enum {
	LIBETH_XDP_PASS			= 0U,
	LIBETH_XDP_DROP			= BIT(0),
	LIBETH_XDP_ABORTED		= BIT(1),
	LIBETH_XDP_TX			= BIT(2),
};

/*
 * &xdp_buff_xsk is the largest structure &libeth_xdp_buff gets casted to,
 * pick maximum pointer-compatible alignment.
 */
#define __LIBETH_XDP_BUFF_ALIGN						      \
	(IS_ALIGNED(sizeof(struct xdp_buff_xsk), 16) ? 16 :		      \
	 IS_ALIGNED(sizeof(struct xdp_buff_xsk), 8) ? 8 :		      \
	 sizeof(long))

/**
 * struct libeth_xdp_buff - libeth extension over &xdp_buff
 * @base: main &xdp_buff
 * @data: shortcut for @base.data
 * @desc: RQ descriptor containing metadata for this buffer
 * @priv: driver-private scratchspace
 *
 * The main reason for this is to have a pointer to the descriptor to be able
 * to quickly get frame metadata from xdpmo and driver buff-to-xdp callbacks
 * (as well as bigger alignment).
 * Pointer/layout-compatible with &xdp_buff and &xdp_buff_xsk.
 */
struct libeth_xdp_buff {
	union {
		struct xdp_buff		base;
		void			*data;
	};

	const void			*desc;
	unsigned long			priv[]
					__aligned(__LIBETH_XDP_BUFF_ALIGN);
} __aligned(__LIBETH_XDP_BUFF_ALIGN);
static_assert(offsetof(struct libeth_xdp_buff, data) ==
	      offsetof(struct xdp_buff_xsk, xdp.data));
static_assert(offsetof(struct libeth_xdp_buff, desc) ==
	      offsetof(struct xdp_buff_xsk, cb));
static_assert(IS_ALIGNED(sizeof(struct xdp_buff_xsk),
			 __alignof(struct libeth_xdp_buff)));

/* XDPSQ sharing */

DECLARE_STATIC_KEY_FALSE(libeth_xdpsq_share);

/**
 * libeth_xdpsq_num - calculate optimal number of XDPSQs for this device + sys
 * @rxq: current number of active Rx queues
 * @txq: current number of active Tx queues
 * @max: maximum number of Tx queues
 *
 * Each RQ must have its own XDPSQ for XSk pairs, each CPU must have own XDPSQ
 * for lockless sending (``XDP_TX``, .ndo_xdp_xmit()). Cap the maximum of these
 * two with the number of SQs the device can have (minus used ones).
 *
 * Return: number of XDP Tx queues the device needs to use.
 */
static inline u32 libeth_xdpsq_num(u32 rxq, u32 txq, u32 max)
{
	return min(max(nr_cpu_ids, rxq), max - txq);
}

/**
 * libeth_xdpsq_shared - whether XDPSQs can be shared between several CPUs
 * @num: number of active XDPSQs
 *
 * Return: true if there's no 1:1 XDPSQ/CPU association, false otherwise.
 */
static inline bool libeth_xdpsq_shared(u32 num)
{
	return num < nr_cpu_ids;
}

/**
 * libeth_xdpsq_id - get XDPSQ index corresponding to this CPU
 * @num: number of active XDPSQs
 *
 * Helper for libeth_xdp routines, do not use in drivers directly.
 *
 * Return: XDPSQ index needs to be used on this CPU.
 */
static inline u32 libeth_xdpsq_id(u32 num)
{
	u32 ret = raw_smp_processor_id();

	if (static_branch_unlikely(&libeth_xdpsq_share) &&
	    libeth_xdpsq_shared(num))
		ret %= num;

	return ret;
}

void __libeth_xdpsq_get(struct libeth_xdpsq_lock *lock,
			const struct net_device *dev);
void __libeth_xdpsq_put(struct libeth_xdpsq_lock *lock,
			const struct net_device *dev);

/**
 * libeth_xdpsq_get - initialize &libeth_xdpsq_lock
 * @lock: lock to initialize
 * @dev: netdev which this lock belongs to
 * @share: whether XDPSQs can be shared
 *
 * Tracks the current XDPSQ association and enables the static lock
 * if needed.
 */
static inline void libeth_xdpsq_get(struct libeth_xdpsq_lock *lock,
				    const struct net_device *dev,
				    bool share)
{
	if (unlikely(share))
		__libeth_xdpsq_get(lock, dev);
}

/**
 * libeth_xdpsq_put - deinitialize &libeth_xdpsq_lock
 * @lock: lock to deinitialize
 * @dev: netdev which this lock belongs to
 *
 * Tracks the current XDPSQ association and disables the static lock
 * if needed.
 */
static inline void libeth_xdpsq_put(struct libeth_xdpsq_lock *lock,
				    const struct net_device *dev)
{
	if (static_branch_unlikely(&libeth_xdpsq_share) && lock->share)
		__libeth_xdpsq_put(lock, dev);
}

void __libeth_xdpsq_lock(struct libeth_xdpsq_lock *lock);
void __libeth_xdpsq_unlock(struct libeth_xdpsq_lock *lock);

/**
 * libeth_xdpsq_lock - grab &libeth_xdpsq_lock if needed
 * @lock: lock to take
 *
 * Touches the underlying spinlock only if the static key is enabled
 * and the queue itself is marked as shareable.
 */
static inline void libeth_xdpsq_lock(struct libeth_xdpsq_lock *lock)
{
	if (static_branch_unlikely(&libeth_xdpsq_share) && lock->share)
		__libeth_xdpsq_lock(lock);
}

/**
 * libeth_xdpsq_unlock - free &libeth_xdpsq_lock if needed
 * @lock: lock to free
 *
 * Touches the underlying spinlock only if the static key is enabled
 * and the queue itself is marked as shareable.
 */
static inline void libeth_xdpsq_unlock(struct libeth_xdpsq_lock *lock)
{
	if (static_branch_unlikely(&libeth_xdpsq_share) && lock->share)
		__libeth_xdpsq_unlock(lock);
}

/* Common Tx bits */

/**
 * enum - libeth_xdp internal Tx flags
 * @LIBETH_XDP_TX_BULK: one bulk size at which it will be flushed to the queue
 * @LIBETH_XDP_TX_BATCH: batch size for which the queue fill loop is unrolled
 * @LIBETH_XDP_TX_DROP: indicates the send function must drop frames not sent
 * @LIBETH_XDP_TX_NDO: whether the send function is called from .ndo_xdp_xmit()
 */
enum {
	LIBETH_XDP_TX_BULK		= DEV_MAP_BULK_SIZE,
	LIBETH_XDP_TX_BATCH		= 8,

	LIBETH_XDP_TX_DROP		= BIT(0),
	LIBETH_XDP_TX_NDO		= BIT(1),
};

/**
 * enum - &libeth_xdp_tx_frame and &libeth_xdp_tx_desc flags
 * @LIBETH_XDP_TX_LEN: only for ``XDP_TX``, [15:0] of ::len_fl is actual length
 * @LIBETH_XDP_TX_FIRST: indicates the frag is the first one of the frame
 * @LIBETH_XDP_TX_LAST: whether the frag is the last one of the frame
 * @LIBETH_XDP_TX_MULTI: whether the frame contains several frags
 * @LIBETH_XDP_TX_FLAGS: only for ``XDP_TX``, [31:16] of ::len_fl is flags
 */
enum {
	LIBETH_XDP_TX_LEN		= GENMASK(15, 0),

	LIBETH_XDP_TX_FIRST		= BIT(16),
	LIBETH_XDP_TX_LAST		= BIT(17),
	LIBETH_XDP_TX_MULTI		= BIT(18),

	LIBETH_XDP_TX_FLAGS		= GENMASK(31, 16),
};

/**
 * struct libeth_xdp_tx_frame - represents one XDP Tx element
 * @data: frame start pointer for ``XDP_TX``
 * @len_fl: ``XDP_TX``, combined flags [31:16] and len [15:0] field for speed
 * @soff: ``XDP_TX``, offset from @data to the start of &skb_shared_info
 * @frag: one (non-head) frag for ``XDP_TX``
 * @xdpf: &xdp_frame for the head frag for .ndo_xdp_xmit()
 * @dma: DMA address of the non-head frag for .ndo_xdp_xmit()
 * @len: frag length for .ndo_xdp_xmit()
 * @flags: Tx flags for the above
 * @opts: combined @len + @flags for the above for speed
 */
struct libeth_xdp_tx_frame {
	union {
		/* ``XDP_TX`` */
		struct {
			void				*data;
			u32				len_fl;
			u32				soff;
		};

		/* ``XDP_TX`` frag */
		skb_frag_t			frag;

		/* .ndo_xdp_xmit() */
		struct {
			union {
				struct xdp_frame		*xdpf;
				dma_addr_t			dma;
			};
			union {
				struct {
					u32				len;
					u32				flags;
				};
				aligned_u64			opts;
			};
		};
	};
} __aligned_largest;
static_assert(offsetof(struct libeth_xdp_tx_frame, frag.len) ==
	      offsetof(struct libeth_xdp_tx_frame, len_fl));

/**
 * struct libeth_xdp_tx_bulk - XDP Tx frame bulk for bulk sending
 * @prog: corresponding active XDP program, %NULL for .ndo_xdp_xmit()
 * @dev: &net_device which the frames are transmitted on
 * @xdpsq: shortcut to the corresponding driver-specific XDPSQ structure
 * @count: current number of frames in @bulk
 * @bulk: array of queued frames for bulk Tx
 *
 * All XDP Tx operations queue each frame to the bulk first and flush it
 * when @count reaches the array end. Bulk is always placed on the stack
 * for performance. One bulk element contains all the data necessary
 * for sending a frame and then freeing it on completion.
 */
struct libeth_xdp_tx_bulk {
	const struct bpf_prog		*prog;
	struct net_device		*dev;
	void				*xdpsq;

	u32				count;
	struct libeth_xdp_tx_frame	bulk[LIBETH_XDP_TX_BULK];
} __aligned(sizeof(struct libeth_xdp_tx_frame));

/**
 * LIBETH_XDP_ONSTACK_BULK - declare &libeth_xdp_tx_bulk on the stack
 * @bq: name of the variable to declare
 *
 * Helper to declare a bulk on the stack with a compiler hint that it should
 * not be initialized automatically (with `CONFIG_INIT_STACK_ALL_*`) for
 * performance reasons.
 */
#define LIBETH_XDP_ONSTACK_BULK(bq)					      \
	struct libeth_xdp_tx_bulk bq __uninitialized

/**
 * struct libeth_xdpsq - abstraction for an XDPSQ
 * @sqes: array of Tx buffers from the actual queue struct
 * @descs: opaque pointer to the HW descriptor array
 * @ntu: pointer to the next free descriptor index
 * @count: number of descriptors on that queue
 * @pending: pointer to the number of sent-not-completed descs on that queue
 * @xdp_tx: pointer to the above
 * @lock: corresponding XDPSQ lock
 *
 * Abstraction for driver-independent implementation of Tx. Placed on the stack
 * and filled by the driver before the transmission, so that the generic
 * functions can access and modify driver-specific resources.
 */
struct libeth_xdpsq {
	struct libeth_sqe		*sqes;
	void				*descs;

	u32				*ntu;
	u32				count;

	u32				*pending;
	u32				*xdp_tx;
	struct libeth_xdpsq_lock	*lock;
};

/**
 * struct libeth_xdp_tx_desc - abstraction for an XDP Tx descriptor
 * @addr: DMA address of the frame
 * @len: length of the frame
 * @flags: XDP Tx flags
 * @opts: combined @len + @flags for speed
 *
 * Filled by the generic functions and then passed to driver-specific functions
 * to fill a HW Tx descriptor, always placed on the [function] stack.
 */
struct libeth_xdp_tx_desc {
	dma_addr_t			addr;
	union {
		struct {
			u32				len;
			u32				flags;
		};
		aligned_u64			opts;
	};
} __aligned_largest;

/**
 * libeth_xdp_tx_xmit_bulk - main XDP Tx function
 * @bulk: array of frames to send
 * @xdpsq: pointer to the driver-specific XDPSQ struct
 * @n: number of frames to send
 * @unroll: whether to unroll the queue filling loop for speed
 * @priv: driver-specific private data
 * @prep: callback for cleaning the queue and filling abstract &libeth_xdpsq
 * @fill: internal callback for filling &libeth_sqe and &libeth_xdp_tx_desc
 * @xmit: callback for filling a HW descriptor with the frame info
 *
 * Internal abstraction for placing @n XDP Tx frames on the HW XDPSQ. Used for
 * all types of frames.
 * @prep must lock the queue as this function releases it at the end. @unroll
 * greatly increases the object code size, but also greatly increases
 * performance.
 * The compilers inline all those onstack abstractions to direct data accesses.
 *
 * Return: number of frames actually placed on the queue, <= @n. The function
 * can't fail, but can send less frames if there's no enough free descriptors
 * available. The actual free space is returned by @prep from the driver.
 */
static __always_inline u32
libeth_xdp_tx_xmit_bulk(const struct libeth_xdp_tx_frame *bulk, void *xdpsq,
			u32 n, bool unroll, u64 priv,
			u32 (*prep)(void *xdpsq, struct libeth_xdpsq *sq),
			struct libeth_xdp_tx_desc
			(*fill)(struct libeth_xdp_tx_frame frm, u32 i,
				const struct libeth_xdpsq *sq, u64 priv),
			void (*xmit)(struct libeth_xdp_tx_desc desc, u32 i,
				     const struct libeth_xdpsq *sq, u64 priv))
{
	struct libeth_xdpsq sq __uninitialized;
	u32 this, batched, off = 0;
	u32 ntu, i = 0;

	n = min(n, prep(xdpsq, &sq));
	if (unlikely(!n))
		goto unlock;

	ntu = *sq.ntu;

	this = sq.count - ntu;
	if (likely(this > n))
		this = n;

again:
	if (!unroll)
		goto linear;

	batched = ALIGN_DOWN(this, LIBETH_XDP_TX_BATCH);

	for ( ; i < off + batched; i += LIBETH_XDP_TX_BATCH) {
		u32 base = ntu + i - off;

		unrolled_count(LIBETH_XDP_TX_BATCH)
		for (u32 j = 0; j < LIBETH_XDP_TX_BATCH; j++)
			xmit(fill(bulk[i + j], base + j, &sq, priv),
			     base + j, &sq, priv);
	}

	if (batched < this) {
linear:
		for ( ; i < off + this; i++)
			xmit(fill(bulk[i], ntu + i - off, &sq, priv),
			     ntu + i - off, &sq, priv);
	}

	ntu += this;
	if (likely(ntu < sq.count))
		goto out;

	ntu = 0;

	if (i < n) {
		this = n - i;
		off = i;

		goto again;
	}

out:
	*sq.ntu = ntu;
	*sq.pending += n;
	if (sq.xdp_tx)
		*sq.xdp_tx += n;

unlock:
	libeth_xdpsq_unlock(sq.lock);

	return n;
}

/* ``XDP_TX`` bulking */

void libeth_xdp_return_buff_slow(struct libeth_xdp_buff *xdp);

/**
 * libeth_xdp_tx_queue_head - internal helper for queueing one ``XDP_TX`` head
 * @bq: XDP Tx bulk to queue the head frag to
 * @xdp: XDP buffer with the head to queue
 *
 * Return: false if it's the only frag of the frame, true if it's an S/G frame.
 */
static inline bool libeth_xdp_tx_queue_head(struct libeth_xdp_tx_bulk *bq,
					    const struct libeth_xdp_buff *xdp)
{
	const struct xdp_buff *base = &xdp->base;

	bq->bulk[bq->count++] = (typeof(*bq->bulk)){
		.data	= xdp->data,
		.len_fl	= (base->data_end - xdp->data) | LIBETH_XDP_TX_FIRST,
		.soff	= xdp_data_hard_end(base) - xdp->data,
	};

	if (!xdp_buff_has_frags(base))
		return false;

	bq->bulk[bq->count - 1].len_fl |= LIBETH_XDP_TX_MULTI;

	return true;
}

/**
 * libeth_xdp_tx_queue_frag - internal helper for queueing one ``XDP_TX`` frag
 * @bq: XDP Tx bulk to queue the frag to
 * @frag: frag to queue
 */
static inline void libeth_xdp_tx_queue_frag(struct libeth_xdp_tx_bulk *bq,
					    const skb_frag_t *frag)
{
	bq->bulk[bq->count++].frag = *frag;
}

/**
 * libeth_xdp_tx_queue_bulk - internal helper for queueing one ``XDP_TX`` frame
 * @bq: XDP Tx bulk to queue the frame to
 * @xdp: XDP buffer to queue
 * @flush_bulk: driver callback to flush the bulk to the HW queue
 *
 * Return: true on success, false on flush error.
 */
static __always_inline bool
libeth_xdp_tx_queue_bulk(struct libeth_xdp_tx_bulk *bq,
			 struct libeth_xdp_buff *xdp,
			 bool (*flush_bulk)(struct libeth_xdp_tx_bulk *bq,
					    u32 flags))
{
	const struct skb_shared_info *sinfo;
	bool ret = true;
	u32 nr_frags;

	if (unlikely(bq->count == LIBETH_XDP_TX_BULK) &&
	    unlikely(!flush_bulk(bq, 0))) {
		libeth_xdp_return_buff_slow(xdp);
		return false;
	}

	if (!libeth_xdp_tx_queue_head(bq, xdp))
		goto out;

	sinfo = xdp_get_shared_info_from_buff(&xdp->base);
	nr_frags = sinfo->nr_frags;

	for (u32 i = 0; i < nr_frags; i++) {
		if (unlikely(bq->count == LIBETH_XDP_TX_BULK) &&
		    unlikely(!flush_bulk(bq, 0))) {
			ret = false;
			break;
		}

		libeth_xdp_tx_queue_frag(bq, &sinfo->frags[i]);
	}

out:
	bq->bulk[bq->count - 1].len_fl |= LIBETH_XDP_TX_LAST;
	xdp->data = NULL;

	return ret;
}

/**
 * libeth_xdp_tx_fill_stats - fill &libeth_sqe with ``XDP_TX`` frame stats
 * @sqe: SQ element to fill
 * @desc: libeth_xdp Tx descriptor
 * @sinfo: &skb_shared_info for this frame
 *
 * Internal helper for filling an SQE with the frame stats, do not use in
 * drivers. Fills the number of frags and bytes for this frame.
 */
#define libeth_xdp_tx_fill_stats(sqe, desc, sinfo)			      \
	__libeth_xdp_tx_fill_stats(sqe, desc, sinfo, __UNIQUE_ID(sqe_),	      \
				   __UNIQUE_ID(desc_), __UNIQUE_ID(sinfo_))

#define __libeth_xdp_tx_fill_stats(sqe, desc, sinfo, ue, ud, us) do {	      \
	const struct libeth_xdp_tx_desc *ud = (desc);			      \
	const struct skb_shared_info *us;				      \
	struct libeth_sqe *ue = (sqe);					      \
									      \
	ue->nr_frags = 1;						      \
	ue->bytes = ud->len;						      \
									      \
	if (ud->flags & LIBETH_XDP_TX_MULTI) {				      \
		us = (sinfo);						      \
		ue->nr_frags += us->nr_frags;				      \
		ue->bytes += us->xdp_frags_size;			      \
	}								      \
} while (0)

/**
 * libeth_xdp_tx_fill_buf - internal helper to fill one ``XDP_TX`` &libeth_sqe
 * @frm: XDP Tx frame from the bulk
 * @i: index on the HW queue
 * @sq: XDPSQ abstraction for the queue
 * @priv: private data
 *
 * Return: XDP Tx descriptor with the synced DMA and other info to pass to
 * the driver callback.
 */
static inline struct libeth_xdp_tx_desc
libeth_xdp_tx_fill_buf(struct libeth_xdp_tx_frame frm, u32 i,
		       const struct libeth_xdpsq *sq, u64 priv)
{
	struct libeth_xdp_tx_desc desc;
	struct skb_shared_info *sinfo;
	skb_frag_t *frag = &frm.frag;
	struct libeth_sqe *sqe;
	netmem_ref netmem;

	if (frm.len_fl & LIBETH_XDP_TX_FIRST) {
		sinfo = frm.data + frm.soff;
		skb_frag_fill_netmem_desc(frag, virt_to_netmem(frm.data),
					  offset_in_page(frm.data),
					  frm.len_fl);
	} else {
		sinfo = NULL;
	}

	netmem = skb_frag_netmem(frag);
	desc = (typeof(desc)){
		.addr	= page_pool_get_dma_addr_netmem(netmem) +
			  skb_frag_off(frag),
		.len	= skb_frag_size(frag) & LIBETH_XDP_TX_LEN,
		.flags	= skb_frag_size(frag) & LIBETH_XDP_TX_FLAGS,
	};

	dma_sync_single_for_device(__netmem_get_pp(netmem)->p.dev, desc.addr,
				   desc.len, DMA_BIDIRECTIONAL);

	if (!sinfo)
		return desc;

	sqe = &sq->sqes[i];
	sqe->type = LIBETH_SQE_XDP_TX;
	sqe->sinfo = sinfo;
	libeth_xdp_tx_fill_stats(sqe, &desc, sinfo);

	return desc;
}

void libeth_xdp_tx_exception(struct libeth_xdp_tx_bulk *bq, u32 sent,
			     u32 flags);

/**
 * __libeth_xdp_tx_flush_bulk - internal helper to flush one XDP Tx bulk
 * @bq: bulk to flush
 * @flags: XDP TX flags (.ndo_xdp_xmit() etc.)
 * @prep: driver-specific callback to prepare the queue for sending
 * @fill: libeth_xdp callback to fill &libeth_sqe and &libeth_xdp_tx_desc
 * @xmit: driver callback to fill a HW descriptor
 *
 * Internal abstraction to create bulk flush functions for drivers.
 *
 * Return: true if anything was sent, false otherwise.
 */
static __always_inline bool
__libeth_xdp_tx_flush_bulk(struct libeth_xdp_tx_bulk *bq, u32 flags,
			   u32 (*prep)(void *xdpsq, struct libeth_xdpsq *sq),
			   struct libeth_xdp_tx_desc
			   (*fill)(struct libeth_xdp_tx_frame frm, u32 i,
				   const struct libeth_xdpsq *sq, u64 priv),
			   void (*xmit)(struct libeth_xdp_tx_desc desc, u32 i,
					const struct libeth_xdpsq *sq,
					u64 priv))
{
	u32 sent, drops;
	int err = 0;

	sent = libeth_xdp_tx_xmit_bulk(bq->bulk, bq->xdpsq,
				       min(bq->count, LIBETH_XDP_TX_BULK),
				       false, 0, prep, fill, xmit);
	drops = bq->count - sent;

	if (unlikely(drops)) {
		libeth_xdp_tx_exception(bq, sent, flags);
		err = -ENXIO;
	} else {
		bq->count = 0;
	}

	trace_xdp_bulk_tx(bq->dev, sent, drops, err);

	return likely(sent);
}

/**
 * libeth_xdp_tx_flush_bulk - wrapper to define flush of one ``XDP_TX`` bulk
 * @bq: bulk to flush
 * @flags: Tx flags, see above
 * @prep: driver callback to prepare the queue
 * @xmit: driver callback to fill a HW descriptor
 */
#define libeth_xdp_tx_flush_bulk(bq, flags, prep, xmit)			      \
	__libeth_xdp_tx_flush_bulk(bq, flags, prep, libeth_xdp_tx_fill_buf,   \
				   xmit)

/* .ndo_xdp_xmit() implementation */

/**
 * libeth_xdp_xmit_frame_dma - internal helper to access DMA of an &xdp_frame
 * @xf: pointer to the XDP frame
 *
 * There's no place in &libeth_xdp_tx_frame to store DMA address for an
 * &xdp_frame head. The headroom is used then, the address is placed right
 * after the frame struct, naturally aligned.
 *
 * Return: pointer to the DMA address to use.
 */
#define libeth_xdp_xmit_frame_dma(xf)					      \
	_Generic((xf),							      \
		 const struct xdp_frame *:				      \
			(const dma_addr_t *)__libeth_xdp_xmit_frame_dma(xf),  \
		 struct xdp_frame *:					      \
			(dma_addr_t *)__libeth_xdp_xmit_frame_dma(xf)	      \
	)

static inline void *__libeth_xdp_xmit_frame_dma(const struct xdp_frame *xdpf)
{
	void *addr = (void *)(xdpf + 1);

	if (!IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) &&
	    __alignof(*xdpf) < sizeof(dma_addr_t))
		addr = PTR_ALIGN(addr, sizeof(dma_addr_t));

	return addr;
}

/**
 * libeth_xdp_xmit_queue_head - internal helper for queueing one XDP xmit head
 * @bq: XDP Tx bulk to queue the head frag to
 * @xdpf: XDP frame with the head to queue
 * @dev: device to perform DMA mapping
 *
 * Return: ``LIBETH_XDP_DROP`` on DMA mapping error,
 *	   ``LIBETH_XDP_PASS`` if it's the only frag in the frame,
 *	   ``LIBETH_XDP_TX`` if it's an S/G frame.
 */
static inline u32 libeth_xdp_xmit_queue_head(struct libeth_xdp_tx_bulk *bq,
					     struct xdp_frame *xdpf,
					     struct device *dev)
{
	dma_addr_t dma;

	dma = dma_map_single(dev, xdpf->data, xdpf->len, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma))
		return LIBETH_XDP_DROP;

	*libeth_xdp_xmit_frame_dma(xdpf) = dma;

	bq->bulk[bq->count++] = (typeof(*bq->bulk)){
		.xdpf	= xdpf,
		.len	= xdpf->len,
		.flags	= LIBETH_XDP_TX_FIRST,
	};

	if (!xdp_frame_has_frags(xdpf))
		return LIBETH_XDP_PASS;

	bq->bulk[bq->count - 1].flags |= LIBETH_XDP_TX_MULTI;

	return LIBETH_XDP_TX;
}

/**
 * libeth_xdp_xmit_queue_frag - internal helper for queueing one XDP xmit frag
 * @bq: XDP Tx bulk to queue the frag to
 * @frag: frag to queue
 * @dev: device to perform DMA mapping
 *
 * Return: true on success, false on DMA mapping error.
 */
static inline bool libeth_xdp_xmit_queue_frag(struct libeth_xdp_tx_bulk *bq,
					      const skb_frag_t *frag,
					      struct device *dev)
{
	dma_addr_t dma;

	dma = skb_frag_dma_map(dev, frag);
	if (dma_mapping_error(dev, dma))
		return false;

	bq->bulk[bq->count++] = (typeof(*bq->bulk)){
		.dma	= dma,
		.len	= skb_frag_size(frag),
	};

	return true;
}

/**
 * libeth_xdp_xmit_queue_bulk - internal helper for queueing one XDP xmit frame
 * @bq: XDP Tx bulk to queue the frame to
 * @xdpf: XDP frame to queue
 * @flush_bulk: driver callback to flush the bulk to the HW queue
 *
 * Return: ``LIBETH_XDP_TX`` on success,
 *	   ``LIBETH_XDP_DROP`` if the frame should be dropped by the stack,
 *	   ``LIBETH_XDP_ABORTED`` if the frame will be dropped by libeth_xdp.
 */
static __always_inline u32
libeth_xdp_xmit_queue_bulk(struct libeth_xdp_tx_bulk *bq,
			   struct xdp_frame *xdpf,
			   bool (*flush_bulk)(struct libeth_xdp_tx_bulk *bq,
					      u32 flags))
{
	u32 head, nr_frags, i, ret = LIBETH_XDP_TX;
	struct device *dev = bq->dev->dev.parent;
	const struct skb_shared_info *sinfo;

	if (unlikely(bq->count == LIBETH_XDP_TX_BULK) &&
	    unlikely(!flush_bulk(bq, LIBETH_XDP_TX_NDO)))
		return LIBETH_XDP_DROP;

	head = libeth_xdp_xmit_queue_head(bq, xdpf, dev);
	if (head == LIBETH_XDP_PASS)
		goto out;
	else if (head == LIBETH_XDP_DROP)
		return LIBETH_XDP_DROP;

	sinfo = xdp_get_shared_info_from_frame(xdpf);
	nr_frags = sinfo->nr_frags;

	for (i = 0; i < nr_frags; i++) {
		if (unlikely(bq->count == LIBETH_XDP_TX_BULK) &&
		    unlikely(!flush_bulk(bq, LIBETH_XDP_TX_NDO)))
			break;

		if (!libeth_xdp_xmit_queue_frag(bq, &sinfo->frags[i], dev))
			break;
	}

	if (unlikely(i < nr_frags))
		ret = LIBETH_XDP_ABORTED;

out:
	bq->bulk[bq->count - 1].flags |= LIBETH_XDP_TX_LAST;

	return ret;
}

/**
 * libeth_xdp_xmit_fill_buf - internal helper to fill one XDP xmit &libeth_sqe
 * @frm: XDP Tx frame from the bulk
 * @i: index on the HW queue
 * @sq: XDPSQ abstraction for the queue
 * @priv: private data
 *
 * Return: XDP Tx descriptor with the mapped DMA and other info to pass to
 * the driver callback.
 */
static inline struct libeth_xdp_tx_desc
libeth_xdp_xmit_fill_buf(struct libeth_xdp_tx_frame frm, u32 i,
			 const struct libeth_xdpsq *sq, u64 priv)
{
	struct libeth_xdp_tx_desc desc;
	struct libeth_sqe *sqe;
	struct xdp_frame *xdpf;

	if (frm.flags & LIBETH_XDP_TX_FIRST) {
		xdpf = frm.xdpf;
		desc.addr = *libeth_xdp_xmit_frame_dma(xdpf);
	} else {
		xdpf = NULL;
		desc.addr = frm.dma;
	}
	desc.opts = frm.opts;

	sqe = &sq->sqes[i];
	dma_unmap_addr_set(sqe, dma, desc.addr);
	dma_unmap_len_set(sqe, len, desc.len);

	if (!xdpf) {
		sqe->type = LIBETH_SQE_XDP_XMIT_FRAG;
		return desc;
	}

	sqe->type = LIBETH_SQE_XDP_XMIT;
	sqe->xdpf = xdpf;
	libeth_xdp_tx_fill_stats(sqe, &desc,
				 xdp_get_shared_info_from_frame(xdpf));

	return desc;
}

/**
 * libeth_xdp_xmit_flush_bulk - wrapper to define flush of one XDP xmit bulk
 * @bq: bulk to flush
 * @flags: Tx flags, see __libeth_xdp_tx_flush_bulk()
 * @prep: driver callback to prepare the queue
 * @xmit: driver callback to fill a HW descriptor
 */
#define libeth_xdp_xmit_flush_bulk(bq, flags, prep, xmit)		      \
	__libeth_xdp_tx_flush_bulk(bq, (flags) | LIBETH_XDP_TX_NDO, prep,     \
				   libeth_xdp_xmit_fill_buf, xmit)

u32 libeth_xdp_xmit_return_bulk(const struct libeth_xdp_tx_frame *bq,
				u32 count, const struct net_device *dev);

/**
 * __libeth_xdp_xmit_do_bulk - internal function to implement .ndo_xdp_xmit()
 * @bq: XDP Tx bulk to queue frames to
 * @frames: XDP frames passed by the stack
 * @n: number of frames
 * @flags: flags passed by the stack
 * @flush_bulk: driver callback to flush an XDP xmit bulk
 * @finalize: driver callback to finalize sending XDP Tx frames on the queue
 *
 * Perform common checks, map the frags and queue them to the bulk, then flush
 * the bulk to the XDPSQ. If requested by the stack, finalize the queue.
 *
 * Return: number of frames send or -errno on error.
 */
static __always_inline int
__libeth_xdp_xmit_do_bulk(struct libeth_xdp_tx_bulk *bq,
			  struct xdp_frame **frames, u32 n, u32 flags,
			  bool (*flush_bulk)(struct libeth_xdp_tx_bulk *bq,
					     u32 flags),
			  void (*finalize)(void *xdpsq, bool sent, bool flush))
{
	u32 nxmit = 0;

	if (unlikely(flags & ~XDP_XMIT_FLAGS_MASK))
		return -EINVAL;

	for (u32 i = 0; likely(i < n); i++) {
		u32 ret;

		ret = libeth_xdp_xmit_queue_bulk(bq, frames[i], flush_bulk);
		if (unlikely(ret != LIBETH_XDP_TX)) {
			nxmit += ret == LIBETH_XDP_ABORTED;
			break;
		}

		nxmit++;
	}

	if (bq->count) {
		flush_bulk(bq, LIBETH_XDP_TX_NDO);
		if (unlikely(bq->count))
			nxmit -= libeth_xdp_xmit_return_bulk(bq->bulk,
							     bq->count,
							     bq->dev);
	}

	finalize(bq->xdpsq, nxmit, flags & XDP_XMIT_FLUSH);

	return nxmit;
}

/* Rx polling path */

static inline void libeth_xdp_return_va(const void *data, bool napi)
{
	netmem_ref netmem = virt_to_netmem(data);

	page_pool_put_full_netmem(__netmem_get_pp(netmem), netmem, napi);
}

static inline void libeth_xdp_return_frags(const struct skb_shared_info *sinfo,
					   bool napi)
{
	for (u32 i = 0; i < sinfo->nr_frags; i++) {
		netmem_ref netmem = skb_frag_netmem(&sinfo->frags[i]);

		page_pool_put_full_netmem(netmem_get_pp(netmem), netmem, napi);
	}
}

/**
 * libeth_xdp_return_buff - free/recycle &libeth_xdp_buff
 * @xdp: buffer to free
 *
 * Hotpath helper to free &libeth_xdp_buff. Comparing to xdp_return_buff(),
 * it's faster as it gets inlined and always assumes order-0 pages and safe
 * direct recycling. Zeroes @xdp->data to avoid UAFs.
 */
#define libeth_xdp_return_buff(xdp)	__libeth_xdp_return_buff(xdp, true)

static inline void __libeth_xdp_return_buff(struct libeth_xdp_buff *xdp,
					    bool napi)
{
	if (!xdp_buff_has_frags(&xdp->base))
		goto out;

	libeth_xdp_return_frags(xdp_get_shared_info_from_buff(&xdp->base),
				napi);

out:
	libeth_xdp_return_va(xdp->data, napi);
	xdp->data = NULL;
}

/* Tx buffer completion */

void libeth_xdp_return_buff_bulk(const struct skb_shared_info *sinfo,
				 struct xdp_frame_bulk *bq, bool frags);

/**
 * __libeth_xdp_complete_tx - complete sent XDPSQE
 * @sqe: SQ element / Tx buffer to complete
 * @cp: Tx polling/completion params
 * @bulk: internal callback to bulk-free ``XDP_TX`` buffers
 *
 * Use the non-underscored version in drivers instead. This one is shared
 * internally with libeth_tx_complete_any().
 * Complete an XDPSQE of any type of XDP frame. This includes DMA unmapping
 * when needed, buffer freeing, stats update, and SQE invalidation.
 */
static __always_inline void
__libeth_xdp_complete_tx(struct libeth_sqe *sqe, struct libeth_cq_pp *cp,
			 typeof(libeth_xdp_return_buff_bulk) bulk)
{
	enum libeth_sqe_type type = sqe->type;

	switch (type) {
	case LIBETH_SQE_EMPTY:
		return;
	case LIBETH_SQE_XDP_XMIT:
	case LIBETH_SQE_XDP_XMIT_FRAG:
		dma_unmap_page(cp->dev, dma_unmap_addr(sqe, dma),
			       dma_unmap_len(sqe, len), DMA_TO_DEVICE);
		break;
	default:
		break;
	}

	switch (type) {
	case LIBETH_SQE_XDP_TX:
		bulk(sqe->sinfo, cp->bq, sqe->nr_frags != 1);
		break;
	case LIBETH_SQE_XDP_XMIT:
		xdp_return_frame_bulk(sqe->xdpf, cp->bq);
		break;
	default:
		break;
	}

	switch (type) {
	case LIBETH_SQE_XDP_TX:
	case LIBETH_SQE_XDP_XMIT:
		cp->xdp_tx -= sqe->nr_frags;

		cp->xss->packets++;
		cp->xss->bytes += sqe->bytes;
		break;
	default:
		break;
	}

	sqe->type = LIBETH_SQE_EMPTY;
}

static inline void libeth_xdp_complete_tx(struct libeth_sqe *sqe,
					  struct libeth_cq_pp *cp)
{
	__libeth_xdp_complete_tx(sqe, cp, libeth_xdp_return_buff_bulk);
}

#endif /* __LIBETH_XDP_H */
