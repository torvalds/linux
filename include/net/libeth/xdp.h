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
	LIBETH_XDP_REDIRECT		= BIT(3),
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

/**
 * __LIBETH_XDP_ONSTACK_BUFF - declare a &libeth_xdp_buff on the stack
 * @name: name of the variable to declare
 * @...: sizeof() of the driver-private data
 */
#define __LIBETH_XDP_ONSTACK_BUFF(name, ...)				      \
	___LIBETH_XDP_ONSTACK_BUFF(name, ##__VA_ARGS__)
/**
 * LIBETH_XDP_ONSTACK_BUFF - declare a &libeth_xdp_buff on the stack
 * @name: name of the variable to declare
 * @...: type or variable name of the driver-private data
 */
#define LIBETH_XDP_ONSTACK_BUFF(name, ...)				      \
	__LIBETH_XDP_ONSTACK_BUFF(name, __libeth_xdp_priv_sz(__VA_ARGS__))

#define ___LIBETH_XDP_ONSTACK_BUFF(name, ...)				      \
	__DEFINE_FLEX(struct libeth_xdp_buff, name, priv,		      \
		      LIBETH_XDP_PRIV_SZ(__VA_ARGS__ + 0),		      \
		      __uninitialized);					      \
	LIBETH_XDP_ASSERT_PRIV_SZ(__VA_ARGS__ + 0)

#define __libeth_xdp_priv_sz(...)					      \
	CONCATENATE(__libeth_xdp_psz, COUNT_ARGS(__VA_ARGS__))(__VA_ARGS__)

#define __libeth_xdp_psz0(...)
#define __libeth_xdp_psz1(...)		sizeof(__VA_ARGS__)

#define LIBETH_XDP_PRIV_SZ(sz)						      \
	(ALIGN(sz, __alignof(struct libeth_xdp_buff)) / sizeof(long))

/* Performs XSK_CHECK_PRIV_TYPE() */
#define LIBETH_XDP_ASSERT_PRIV_SZ(sz)					      \
	static_assert(offsetofend(struct xdp_buff_xsk, cb) >=		      \
		      struct_size_t(struct libeth_xdp_buff, priv,	      \
				    LIBETH_XDP_PRIV_SZ(sz)))

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

/* XDPSQ clean-up timers */

void libeth_xdpsq_init_timer(struct libeth_xdpsq_timer *timer, void *xdpsq,
			     struct libeth_xdpsq_lock *lock,
			     void (*poll)(struct work_struct *work));

/**
 * libeth_xdpsq_deinit_timer - deinitialize &libeth_xdpsq_timer
 * @timer: timer to deinitialize
 *
 * Flush and disable the underlying workqueue.
 */
static inline void libeth_xdpsq_deinit_timer(struct libeth_xdpsq_timer *timer)
{
	cancel_delayed_work_sync(&timer->dwork);
}

/**
 * libeth_xdpsq_queue_timer - run &libeth_xdpsq_timer
 * @timer: timer to queue
 *
 * Should be called after the queue was filled and the transmission was run
 * to complete the pending buffers if no further sending will be done in a
 * second (-> lazy cleaning won't happen).
 * If the timer was already run, it will be requeued back to one second
 * timeout again.
 */
static inline void libeth_xdpsq_queue_timer(struct libeth_xdpsq_timer *timer)
{
	mod_delayed_work_on(raw_smp_processor_id(), system_bh_highpri_wq,
			    &timer->dwork, HZ);
}

/**
 * libeth_xdpsq_run_timer - wrapper to run a queue clean-up on a timer event
 * @work: workqueue belonging to the corresponding timer
 * @poll: driver-specific completion queue poll function
 *
 * Run the polling function on the locked queue and requeue the timer if
 * there's more work to do.
 * Designed to be used via LIBETH_XDP_DEFINE_TIMER() below.
 */
static __always_inline void
libeth_xdpsq_run_timer(struct work_struct *work,
		       u32 (*poll)(void *xdpsq, u32 budget))
{
	struct libeth_xdpsq_timer *timer = container_of(work, typeof(*timer),
							dwork.work);

	libeth_xdpsq_lock(timer->lock);

	if (poll(timer->xdpsq, U32_MAX))
		libeth_xdpsq_queue_timer(timer);

	libeth_xdpsq_unlock(timer->lock);
}

/* Common Tx bits */

/**
 * enum - libeth_xdp internal Tx flags
 * @LIBETH_XDP_TX_BULK: one bulk size at which it will be flushed to the queue
 * @LIBETH_XDP_TX_BATCH: batch size for which the queue fill loop is unrolled
 * @LIBETH_XDP_TX_DROP: indicates the send function must drop frames not sent
 * @LIBETH_XDP_TX_NDO: whether the send function is called from .ndo_xdp_xmit()
 * @LIBETH_XDP_TX_XSK: whether the function is called for ``XDP_TX`` for XSk
 */
enum {
	LIBETH_XDP_TX_BULK		= DEV_MAP_BULK_SIZE,
	LIBETH_XDP_TX_BATCH		= 8,

	LIBETH_XDP_TX_DROP		= BIT(0),
	LIBETH_XDP_TX_NDO		= BIT(1),
	LIBETH_XDP_TX_XSK		= BIT(2),
};

/**
 * enum - &libeth_xdp_tx_frame and &libeth_xdp_tx_desc flags
 * @LIBETH_XDP_TX_LEN: only for ``XDP_TX``, [15:0] of ::len_fl is actual length
 * @LIBETH_XDP_TX_CSUM: for XSk xmit, enable checksum offload
 * @LIBETH_XDP_TX_XSKMD: for XSk xmit, mask of the metadata bits
 * @LIBETH_XDP_TX_FIRST: indicates the frag is the first one of the frame
 * @LIBETH_XDP_TX_LAST: whether the frag is the last one of the frame
 * @LIBETH_XDP_TX_MULTI: whether the frame contains several frags
 * @LIBETH_XDP_TX_FLAGS: only for ``XDP_TX``, [31:16] of ::len_fl is flags
 */
enum {
	LIBETH_XDP_TX_LEN		= GENMASK(15, 0),

	LIBETH_XDP_TX_CSUM		= XDP_TXMD_FLAGS_CHECKSUM,
	LIBETH_XDP_TX_XSKMD		= LIBETH_XDP_TX_LEN,

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
 * @xsk: ``XDP_TX`` for XSk, XDP buffer for any frag
 * @len: frag length for XSk ``XDP_TX`` and .ndo_xdp_xmit()
 * @flags: Tx flags for the above
 * @opts: combined @len + @flags for the above for speed
 * @desc: XSk xmit descriptor for direct casting
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

		/* .ndo_xdp_xmit(), XSk ``XDP_TX`` */
		struct {
			union {
				struct xdp_frame		*xdpf;
				dma_addr_t			dma;

				struct libeth_xdp_buff		*xsk;
			};
			union {
				struct {
					u32				len;
					u32				flags;
				};
				aligned_u64			opts;
			};
		};

		/* XSk xmit */
		struct xdp_desc			desc;
	};
} __aligned(sizeof(struct xdp_desc));
static_assert(offsetof(struct libeth_xdp_tx_frame, frag.len) ==
	      offsetof(struct libeth_xdp_tx_frame, len_fl));
static_assert(sizeof(struct libeth_xdp_tx_frame) == sizeof(struct xdp_desc));

/**
 * struct libeth_xdp_tx_bulk - XDP Tx frame bulk for bulk sending
 * @prog: corresponding active XDP program, %NULL for .ndo_xdp_xmit()
 * @dev: &net_device which the frames are transmitted on
 * @xdpsq: shortcut to the corresponding driver-specific XDPSQ structure
 * @act_mask: Rx only, mask of all the XDP prog verdicts for that NAPI session
 * @count: current number of frames in @bulk
 * @bulk: array of queued frames for bulk Tx
 *
 * All XDP Tx operations except XSk xmit queue each frame to the bulk first
 * and flush it when @count reaches the array end. Bulk is always placed on
 * the stack for performance. One bulk element contains all the data necessary
 * for sending a frame and then freeing it on completion.
 * For XSk xmit, Tx descriptor array from &xsk_buff_pool is casted directly
 * to &libeth_xdp_tx_frame as they are compatible and the bulk structure is
 * not used.
 */
struct libeth_xdp_tx_bulk {
	const struct bpf_prog		*prog;
	struct net_device		*dev;
	void				*xdpsq;

	u32				act_mask;
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
 * @pool: XSk buffer pool for XSk ``XDP_TX`` and xmit
 * @sqes: array of Tx buffers from the actual queue struct
 * @descs: opaque pointer to the HW descriptor array
 * @ntu: pointer to the next free descriptor index
 * @count: number of descriptors on that queue
 * @pending: pointer to the number of sent-not-completed descs on that queue
 * @xdp_tx: pointer to the above, but only for non-XSk-xmit frames
 * @lock: corresponding XDPSQ lock
 *
 * Abstraction for driver-independent implementation of Tx. Placed on the stack
 * and filled by the driver before the transmission, so that the generic
 * functions can access and modify driver-specific resources.
 */
struct libeth_xdpsq {
	struct xsk_buff_pool		*pool;
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
 * libeth_xdp_ptr_to_priv - convert pointer to a libeth_xdp u64 priv
 * @ptr: pointer to convert
 *
 * The main sending function passes private data as the largest scalar, u64.
 * Use this helper when you want to pass a pointer there.
 */
#define libeth_xdp_ptr_to_priv(ptr) ({					      \
	typecheck_pointer(ptr);						      \
	((u64)(uintptr_t)(ptr));					      \
})
/**
 * libeth_xdp_priv_to_ptr - convert libeth_xdp u64 priv to a pointer
 * @priv: private data to convert
 *
 * The main sending function passes private data as the largest scalar, u64.
 * Use this helper when your callback takes this u64 and you want to convert
 * it back to a pointer.
 */
#define libeth_xdp_priv_to_ptr(priv) ({					      \
	static_assert(__same_type(priv, u64));				      \
	((const void *)(uintptr_t)(priv));				      \
})

/*
 * On 64-bit systems, assigning one u64 is faster than two u32s. When ::len
 * occupies lowest 32 bits (LE), whole ::opts can be assigned directly instead.
 */
#ifdef __LITTLE_ENDIAN
#define __LIBETH_WORD_ACCESS		1
#endif
#ifdef __LIBETH_WORD_ACCESS
#define __libeth_xdp_tx_len(flen, ...)					      \
	.opts = ((flen) | FIELD_PREP(GENMASK_ULL(63, 32), (__VA_ARGS__ + 0)))
#else
#define __libeth_xdp_tx_len(flen, ...)					      \
	.len = (flen), .flags = (__VA_ARGS__ + 0)
#endif

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
 * all types of frames: ``XDP_TX``, .ndo_xdp_xmit(), XSk ``XDP_TX``, and XSk
 * xmit.
 * @prep must lock the queue as this function releases it at the end. @unroll
 * greatly increases the object code size, but also greatly increases XSk xmit
 * performance; for other types of frames, it's not enabled.
 * The compilers inline all those onstack abstractions to direct data accesses.
 *
 * Return: number of frames actually placed on the queue, <= @n. The function
 * can't fail, but can send less frames if there's no enough free descriptors
 * available. The actual free space is returned by @prep from the driver.
 */
static __always_inline __nocfi_generic u32
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
 * @flags: XDP TX flags (.ndo_xdp_xmit(), XSk etc.)
 * @prep: driver-specific callback to prepare the queue for sending
 * @fill: libeth_xdp callback to fill &libeth_sqe and &libeth_xdp_tx_desc
 * @xmit: driver callback to fill a HW descriptor
 *
 * Internal abstraction to create bulk flush functions for drivers. Used for
 * everything except XSk xmit.
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
 *
 * Use via LIBETH_XDP_DEFINE_FLUSH_TX() to define an ``XDP_TX`` driver
 * callback.
 */
#define libeth_xdp_tx_flush_bulk(bq, flags, prep, xmit)			      \
	__libeth_xdp_tx_flush_bulk(bq, flags, prep, libeth_xdp_tx_fill_buf,   \
				   xmit)

/* .ndo_xdp_xmit() implementation */

/**
 * libeth_xdp_xmit_init_bulk - internal helper to initialize bulk for XDP xmit
 * @bq: bulk to initialize
 * @dev: target &net_device
 * @xdpsqs: array of driver-specific XDPSQ structs
 * @num: number of active XDPSQs (the above array length)
 */
#define libeth_xdp_xmit_init_bulk(bq, dev, xdpsqs, num)			      \
	__libeth_xdp_xmit_init_bulk(bq, dev, (xdpsqs)[libeth_xdpsq_id(num)])

static inline void __libeth_xdp_xmit_init_bulk(struct libeth_xdp_tx_bulk *bq,
					       struct net_device *dev,
					       void *xdpsq)
{
	bq->dev = dev;
	bq->xdpsq = xdpsq;
	bq->count = 0;
}

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
		__libeth_xdp_tx_len(xdpf->len, LIBETH_XDP_TX_FIRST),
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
		__libeth_xdp_tx_len(skb_frag_size(frag)),
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
 *
 * Use via LIBETH_XDP_DEFINE_FLUSH_XMIT() to define an XDP xmit driver
 * callback.
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

/**
 * libeth_xdp_xmit_do_bulk - implement full .ndo_xdp_xmit() in driver
 * @dev: target &net_device
 * @n: number of frames to send
 * @fr: XDP frames to send
 * @f: flags passed by the stack
 * @xqs: array of XDPSQs driver structs
 * @nqs: number of active XDPSQs, the above array length
 * @fl: driver callback to flush an XDP xmit bulk
 * @fin: driver cabback to finalize the queue
 *
 * If the driver has active XDPSQs, perform common checks and send the frames.
 * Finalize the queue, if requested.
 *
 * Return: number of frames sent or -errno on error.
 */
#define libeth_xdp_xmit_do_bulk(dev, n, fr, f, xqs, nqs, fl, fin)	      \
	_libeth_xdp_xmit_do_bulk(dev, n, fr, f, xqs, nqs, fl, fin,	      \
				 __UNIQUE_ID(bq_), __UNIQUE_ID(ret_),	      \
				 __UNIQUE_ID(nqs_))

#define _libeth_xdp_xmit_do_bulk(d, n, fr, f, xqs, nqs, fl, fin, ub, ur, un)  \
({									      \
	u32 un = (nqs);							      \
	int ur;								      \
									      \
	if (likely(un)) {						      \
		LIBETH_XDP_ONSTACK_BULK(ub);				      \
									      \
		libeth_xdp_xmit_init_bulk(&ub, d, xqs, un);		      \
		ur = __libeth_xdp_xmit_do_bulk(&ub, fr, n, f, fl, fin);	      \
	} else {							      \
		ur = -ENXIO;						      \
	}								      \
									      \
	ur;								      \
})

/* Rx polling path */

/**
 * libeth_xdp_tx_init_bulk - initialize an XDP Tx bulk for Rx NAPI poll
 * @bq: bulk to initialize
 * @prog: RCU pointer to the XDP program (can be %NULL)
 * @dev: target &net_device
 * @xdpsqs: array of driver XDPSQ structs
 * @num: number of active XDPSQs, the above array length
 *
 * Should be called on an onstack XDP Tx bulk before the NAPI polling loop.
 * Initializes all the needed fields to run libeth_xdp functions. If @num == 0,
 * assumes XDP is not enabled.
 * Do not use for XSk, it has its own optimized helper.
 */
#define libeth_xdp_tx_init_bulk(bq, prog, dev, xdpsqs, num)		      \
	__libeth_xdp_tx_init_bulk(bq, prog, dev, xdpsqs, num, false,	      \
				  __UNIQUE_ID(bq_), __UNIQUE_ID(nqs_))

#define __libeth_xdp_tx_init_bulk(bq, pr, d, xdpsqs, num, xsk, ub, un) do {   \
	typeof(bq) ub = (bq);						      \
	u32 un = (num);							      \
									      \
	rcu_read_lock();						      \
									      \
	if (un || (xsk)) {						      \
		ub->prog = rcu_dereference(pr);				      \
		ub->dev = (d);						      \
		ub->xdpsq = (xdpsqs)[libeth_xdpsq_id(un)];		      \
	} else {							      \
		ub->prog = NULL;					      \
	}								      \
									      \
	ub->act_mask = 0;						      \
	ub->count = 0;							      \
} while (0)

void libeth_xdp_load_stash(struct libeth_xdp_buff *dst,
			   const struct libeth_xdp_buff_stash *src);
void libeth_xdp_save_stash(struct libeth_xdp_buff_stash *dst,
			   const struct libeth_xdp_buff *src);
void __libeth_xdp_return_stash(struct libeth_xdp_buff_stash *stash);

/**
 * libeth_xdp_init_buff - initialize a &libeth_xdp_buff for Rx NAPI poll
 * @dst: onstack buffer to initialize
 * @src: XDP buffer stash placed on the queue
 * @rxq: registered &xdp_rxq_info corresponding to this queue
 *
 * Should be called before the main NAPI polling loop. Loads the content of
 * the previously saved stash or initializes the buffer from scratch.
 * Do not use for XSk.
 */
static inline void
libeth_xdp_init_buff(struct libeth_xdp_buff *dst,
		     const struct libeth_xdp_buff_stash *src,
		     struct xdp_rxq_info *rxq)
{
	if (likely(!src->data))
		dst->data = NULL;
	else
		libeth_xdp_load_stash(dst, src);

	dst->base.rxq = rxq;
}

/**
 * libeth_xdp_save_buff - save a partially built buffer on a queue
 * @dst: XDP buffer stash placed on the queue
 * @src: onstack buffer to save
 *
 * Should be called after the main NAPI polling loop. If the loop exited before
 * the buffer was finished, saves its content on the queue, so that it can be
 * completed during the next poll. Otherwise, clears the stash.
 */
static inline void libeth_xdp_save_buff(struct libeth_xdp_buff_stash *dst,
					const struct libeth_xdp_buff *src)
{
	if (likely(!src->data))
		dst->data = NULL;
	else
		libeth_xdp_save_stash(dst, src);
}

/**
 * libeth_xdp_return_stash - free an XDP buffer stash from a queue
 * @stash: stash to free
 *
 * If the queue is about to be destroyed, but it still has an incompleted
 * buffer stash, this helper should be called to free it.
 */
static inline void libeth_xdp_return_stash(struct libeth_xdp_buff_stash *stash)
{
	if (stash->data)
		__libeth_xdp_return_stash(stash);
}

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

bool libeth_xdp_buff_add_frag(struct libeth_xdp_buff *xdp,
			      const struct libeth_fqe *fqe,
			      u32 len);

/**
 * libeth_xdp_prepare_buff - fill &libeth_xdp_buff with head FQE data
 * @xdp: XDP buffer to attach the head to
 * @fqe: FQE containing the head buffer
 * @len: buffer len passed from HW
 *
 * Internal, use libeth_xdp_process_buff() instead. Initializes XDP buffer
 * head with the Rx buffer data: data pointer, length, headroom, and
 * truesize/tailroom. Zeroes the flags.
 */
static inline void libeth_xdp_prepare_buff(struct libeth_xdp_buff *xdp,
					   const struct libeth_fqe *fqe,
					   u32 len)
{
	const struct page *page = __netmem_to_page(fqe->netmem);

	xdp_prepare_buff(&xdp->base, page_address(page) + fqe->offset,
			 pp_page_to_nmdesc(page)->pp->p.offset, len, true);
	xdp_init_buff(&xdp->base, fqe->truesize, xdp->base.rxq);
}

/**
 * libeth_xdp_process_buff - attach Rx buffer to &libeth_xdp_buff
 * @xdp: XDP buffer to attach the Rx buffer to
 * @fqe: Rx buffer to process
 * @len: received data length from the descriptor
 *
 * If the XDP buffer is empty, attaches the Rx buffer as head and initializes
 * the required fields. Otherwise, attaches the buffer as a frag.
 * Already performs DMA sync-for-CPU and frame start prefetch
 * (for head buffers only).
 *
 * Return: true on success, false if the descriptor must be skipped (empty or
 * no space for a new frag).
 */
static inline bool libeth_xdp_process_buff(struct libeth_xdp_buff *xdp,
					   const struct libeth_fqe *fqe,
					   u32 len)
{
	if (!libeth_rx_sync_for_cpu(fqe, len))
		return false;

	if (xdp->data)
		return libeth_xdp_buff_add_frag(xdp, fqe, len);

	libeth_xdp_prepare_buff(xdp, fqe, len);

	prefetch(xdp->data);

	return true;
}

/**
 * libeth_xdp_buff_stats_frags - update onstack RQ stats with XDP frags info
 * @ss: onstack stats to update
 * @xdp: buffer to account
 *
 * Internal helper used by __libeth_xdp_run_pass(), do not call directly.
 * Adds buffer's frags count and total len to the onstack stats.
 */
static inline void
libeth_xdp_buff_stats_frags(struct libeth_rq_napi_stats *ss,
			    const struct libeth_xdp_buff *xdp)
{
	const struct skb_shared_info *sinfo;

	sinfo = xdp_get_shared_info_from_buff(&xdp->base);
	ss->bytes += sinfo->xdp_frags_size;
	ss->fragments += sinfo->nr_frags + 1;
}

u32 libeth_xdp_prog_exception(const struct libeth_xdp_tx_bulk *bq,
			      struct libeth_xdp_buff *xdp,
			      enum xdp_action act, int ret);

/**
 * __libeth_xdp_run_prog - run XDP program on an XDP buffer
 * @xdp: XDP buffer to run the prog on
 * @bq: buffer bulk for ``XDP_TX`` queueing
 *
 * Internal inline abstraction to run XDP program. Handles ``XDP_DROP``
 * and ``XDP_REDIRECT`` only, the rest is processed levels up.
 * Reports an XDP prog exception on errors.
 *
 * Return: libeth_xdp prog verdict depending on the prog's verdict.
 */
static __always_inline u32
__libeth_xdp_run_prog(struct libeth_xdp_buff *xdp,
		      const struct libeth_xdp_tx_bulk *bq)
{
	enum xdp_action act;

	act = bpf_prog_run_xdp(bq->prog, &xdp->base);
	if (unlikely(act < XDP_DROP || act > XDP_REDIRECT))
		goto out;

	switch (act) {
	case XDP_PASS:
		return LIBETH_XDP_PASS;
	case XDP_DROP:
		libeth_xdp_return_buff(xdp);

		return LIBETH_XDP_DROP;
	case XDP_TX:
		return LIBETH_XDP_TX;
	case XDP_REDIRECT:
		if (unlikely(xdp_do_redirect(bq->dev, &xdp->base, bq->prog)))
			break;

		xdp->data = NULL;

		return LIBETH_XDP_REDIRECT;
	default:
		break;
	}

out:
	return libeth_xdp_prog_exception(bq, xdp, act, 0);
}

/**
 * __libeth_xdp_run_flush - run XDP program and handle ``XDP_TX`` verdict
 * @xdp: XDP buffer to run the prog on
 * @bq: buffer bulk for ``XDP_TX`` queueing
 * @run: internal callback for running XDP program
 * @queue: internal callback for queuing ``XDP_TX`` frame
 * @flush_bulk: driver callback for flushing a bulk
 *
 * Internal inline abstraction to run XDP program and additionally handle
 * ``XDP_TX`` verdict. Used by both XDP and XSk, hence @run and @queue.
 * Do not use directly.
 *
 * Return: libeth_xdp prog verdict depending on the prog's verdict.
 */
static __always_inline u32
__libeth_xdp_run_flush(struct libeth_xdp_buff *xdp,
		       struct libeth_xdp_tx_bulk *bq,
		       u32 (*run)(struct libeth_xdp_buff *xdp,
				  const struct libeth_xdp_tx_bulk *bq),
		       bool (*queue)(struct libeth_xdp_tx_bulk *bq,
				     struct libeth_xdp_buff *xdp,
				     bool (*flush_bulk)
					  (struct libeth_xdp_tx_bulk *bq,
					   u32 flags)),
		       bool (*flush_bulk)(struct libeth_xdp_tx_bulk *bq,
					  u32 flags))
{
	u32 act;

	act = run(xdp, bq);
	if (act == LIBETH_XDP_TX && unlikely(!queue(bq, xdp, flush_bulk)))
		act = LIBETH_XDP_DROP;

	bq->act_mask |= act;

	return act;
}

/**
 * libeth_xdp_run_prog - run XDP program (non-XSk path) and handle all verdicts
 * @xdp: XDP buffer to process
 * @bq: XDP Tx bulk to queue ``XDP_TX`` buffers
 * @fl: driver ``XDP_TX`` bulk flush callback
 *
 * Run the attached XDP program and handle all possible verdicts. XSk has its
 * own version.
 * Prefer using it via LIBETH_XDP_DEFINE_RUN{,_PASS,_PROG}().
 *
 * Return: true if the buffer should be passed up the stack, false if the poll
 * should go to the next buffer.
 */
#define libeth_xdp_run_prog(xdp, bq, fl)				      \
	(__libeth_xdp_run_flush(xdp, bq, __libeth_xdp_run_prog,		      \
				libeth_xdp_tx_queue_bulk,		      \
				fl) == LIBETH_XDP_PASS)

/**
 * __libeth_xdp_run_pass - helper to run XDP program and handle the result
 * @xdp: XDP buffer to process
 * @bq: XDP Tx bulk to queue ``XDP_TX`` frames
 * @napi: NAPI to build an skb and pass it up the stack
 * @rs: onstack libeth RQ stats
 * @md: metadata that should be filled to the XDP buffer
 * @prep: callback for filling the metadata
 * @run: driver wrapper to run XDP program
 * @populate: driver callback to populate an skb with the HW descriptor data
 *
 * Inline abstraction that does the following (non-XSk path):
 * 1) adds frame size and frag number (if needed) to the onstack stats;
 * 2) fills the descriptor metadata to the onstack &libeth_xdp_buff
 * 3) runs XDP program if present;
 * 4) handles all possible verdicts;
 * 5) on ``XDP_PASS`, builds an skb from the buffer;
 * 6) populates it with the descriptor metadata;
 * 7) passes it up the stack.
 *
 * In most cases, number 2 means just writing the pointer to the HW descriptor
 * to the XDP buffer. If so, please use LIBETH_XDP_DEFINE_RUN{,_PASS}()
 * wrappers to build a driver function.
 */
static __always_inline void
__libeth_xdp_run_pass(struct libeth_xdp_buff *xdp,
		      struct libeth_xdp_tx_bulk *bq, struct napi_struct *napi,
		      struct libeth_rq_napi_stats *rs, const void *md,
		      void (*prep)(struct libeth_xdp_buff *xdp,
				   const void *md),
		      bool (*run)(struct libeth_xdp_buff *xdp,
				  struct libeth_xdp_tx_bulk *bq),
		      bool (*populate)(struct sk_buff *skb,
				       const struct libeth_xdp_buff *xdp,
				       struct libeth_rq_napi_stats *rs))
{
	struct sk_buff *skb;

	rs->bytes += xdp->base.data_end - xdp->data;
	rs->packets++;

	if (xdp_buff_has_frags(&xdp->base))
		libeth_xdp_buff_stats_frags(rs, xdp);

	if (prep && (!__builtin_constant_p(!!md) || md))
		prep(xdp, md);

	if (!bq || !run || !bq->prog)
		goto build;

	if (!run(xdp, bq))
		return;

build:
	skb = xdp_build_skb_from_buff(&xdp->base);
	if (unlikely(!skb)) {
		libeth_xdp_return_buff_slow(xdp);
		return;
	}

	xdp->data = NULL;

	if (unlikely(!populate(skb, xdp, rs))) {
		napi_consume_skb(skb, true);
		return;
	}

	napi_gro_receive(napi, skb);
}

static inline void libeth_xdp_prep_desc(struct libeth_xdp_buff *xdp,
					const void *desc)
{
	xdp->desc = desc;
}

/**
 * libeth_xdp_run_pass - helper to run XDP program and handle the result
 * @xdp: XDP buffer to process
 * @bq: XDP Tx bulk to queue ``XDP_TX`` frames
 * @napi: NAPI to build an skb and pass it up the stack
 * @ss: onstack libeth RQ stats
 * @desc: pointer to the HW descriptor for that frame
 * @run: driver wrapper to run XDP program
 * @populate: driver callback to populate an skb with the HW descriptor data
 *
 * Wrapper around the underscored version when "fill the descriptor metadata"
 * means just writing the pointer to the HW descriptor as @xdp->desc.
 */
#define libeth_xdp_run_pass(xdp, bq, napi, ss, desc, run, populate)	      \
	__libeth_xdp_run_pass(xdp, bq, napi, ss, desc, libeth_xdp_prep_desc,  \
			      run, populate)

/**
 * libeth_xdp_finalize_rx - finalize XDPSQ after a NAPI polling loop (non-XSk)
 * @bq: ``XDP_TX`` frame bulk
 * @flush: driver callback to flush the bulk
 * @finalize: driver callback to start sending the frames and run the timer
 *
 * Flush the bulk if there are frames left to send, kick the queue and flush
 * the XDP maps.
 */
#define libeth_xdp_finalize_rx(bq, flush, finalize)			      \
	__libeth_xdp_finalize_rx(bq, 0, flush, finalize)

static __always_inline void
__libeth_xdp_finalize_rx(struct libeth_xdp_tx_bulk *bq, u32 flags,
			 bool (*flush_bulk)(struct libeth_xdp_tx_bulk *bq,
					    u32 flags),
			 void (*finalize)(void *xdpsq, bool sent, bool flush))
{
	if (bq->act_mask & LIBETH_XDP_TX) {
		if (bq->count)
			flush_bulk(bq, flags | LIBETH_XDP_TX_DROP);
		finalize(bq->xdpsq, true, true);
	}
	if (bq->act_mask & LIBETH_XDP_REDIRECT)
		xdp_do_flush();

	rcu_read_unlock();
}

/*
 * Helpers to reduce boilerplate code in drivers.
 *
 * Typical driver Rx flow would be (excl. bulk and buff init, frag attach):
 *
 * LIBETH_XDP_DEFINE_START();
 * LIBETH_XDP_DEFINE_FLUSH_TX(static driver_xdp_flush_tx, driver_xdp_tx_prep,
 *			      driver_xdp_xmit);
 * LIBETH_XDP_DEFINE_RUN(static driver_xdp_run, driver_xdp_run_prog,
 *			 driver_xdp_flush_tx, driver_populate_skb);
 * LIBETH_XDP_DEFINE_FINALIZE(static driver_xdp_finalize_rx,
 *			      driver_xdp_flush_tx, driver_xdp_finalize_sq);
 * LIBETH_XDP_DEFINE_END();
 *
 * This will build a set of 4 static functions. The compiler is free to decide
 * whether to inline them.
 * Then, in the NAPI polling function:
 *
 *	while (packets < budget) {
 *		// ...
 *		driver_xdp_run(xdp, &bq, napi, &rs, desc);
 *	}
 *	driver_xdp_finalize_rx(&bq);
 */

#define LIBETH_XDP_DEFINE_START()					      \
	__diag_push();							      \
	__diag_ignore(GCC, 8, "-Wold-style-declaration",		      \
		      "Allow specifying \'static\' after the return type")

/**
 * LIBETH_XDP_DEFINE_TIMER - define a driver XDPSQ cleanup timer callback
 * @name: name of the function to define
 * @poll: Tx polling/completion function
 */
#define LIBETH_XDP_DEFINE_TIMER(name, poll)				      \
void name(struct work_struct *work)					      \
{									      \
	libeth_xdpsq_run_timer(work, poll);				      \
}

/**
 * LIBETH_XDP_DEFINE_FLUSH_TX - define a driver ``XDP_TX`` bulk flush function
 * @name: name of the function to define
 * @prep: driver callback to clean an XDPSQ
 * @xmit: driver callback to write a HW Tx descriptor
 */
#define LIBETH_XDP_DEFINE_FLUSH_TX(name, prep, xmit)			      \
	__LIBETH_XDP_DEFINE_FLUSH_TX(name, prep, xmit, xdp)

#define __LIBETH_XDP_DEFINE_FLUSH_TX(name, prep, xmit, pfx)		      \
bool name(struct libeth_xdp_tx_bulk *bq, u32 flags)			      \
{									      \
	return libeth_##pfx##_tx_flush_bulk(bq, flags, prep, xmit);	      \
}

/**
 * LIBETH_XDP_DEFINE_FLUSH_XMIT - define a driver XDP xmit bulk flush function
 * @name: name of the function to define
 * @prep: driver callback to clean an XDPSQ
 * @xmit: driver callback to write a HW Tx descriptor
 */
#define LIBETH_XDP_DEFINE_FLUSH_XMIT(name, prep, xmit)			      \
bool name(struct libeth_xdp_tx_bulk *bq, u32 flags)			      \
{									      \
	return libeth_xdp_xmit_flush_bulk(bq, flags, prep, xmit);	      \
}

/**
 * LIBETH_XDP_DEFINE_RUN_PROG - define a driver XDP program run function
 * @name: name of the function to define
 * @flush: driver callback to flush an ``XDP_TX`` bulk
 */
#define LIBETH_XDP_DEFINE_RUN_PROG(name, flush)				      \
	bool __LIBETH_XDP_DEFINE_RUN_PROG(name, flush, xdp)

#define __LIBETH_XDP_DEFINE_RUN_PROG(name, flush, pfx)			      \
name(struct libeth_xdp_buff *xdp, struct libeth_xdp_tx_bulk *bq)	      \
{									      \
	return libeth_##pfx##_run_prog(xdp, bq, flush);			      \
}

/**
 * LIBETH_XDP_DEFINE_RUN_PASS - define a driver buffer process + pass function
 * @name: name of the function to define
 * @run: driver callback to run XDP program (above)
 * @populate: driver callback to fill an skb with HW descriptor info
 */
#define LIBETH_XDP_DEFINE_RUN_PASS(name, run, populate)			      \
	void __LIBETH_XDP_DEFINE_RUN_PASS(name, run, populate, xdp)

#define __LIBETH_XDP_DEFINE_RUN_PASS(name, run, populate, pfx)		      \
name(struct libeth_xdp_buff *xdp, struct libeth_xdp_tx_bulk *bq,	      \
     struct napi_struct *napi, struct libeth_rq_napi_stats *ss,		      \
     const void *desc)							      \
{									      \
	return libeth_##pfx##_run_pass(xdp, bq, napi, ss, desc, run,	      \
				       populate);			      \
}

/**
 * LIBETH_XDP_DEFINE_RUN - define a driver buffer process, run + pass function
 * @name: name of the function to define
 * @run: name of the XDP prog run function to define
 * @flush: driver callback to flush an ``XDP_TX`` bulk
 * @populate: driver callback to fill an skb with HW descriptor info
 */
#define LIBETH_XDP_DEFINE_RUN(name, run, flush, populate)		      \
	__LIBETH_XDP_DEFINE_RUN(name, run, flush, populate, XDP)

#define __LIBETH_XDP_DEFINE_RUN(name, run, flush, populate, pfx)	      \
	LIBETH_##pfx##_DEFINE_RUN_PROG(static run, flush);		      \
	LIBETH_##pfx##_DEFINE_RUN_PASS(name, run, populate)

/**
 * LIBETH_XDP_DEFINE_FINALIZE - define a driver Rx NAPI poll finalize function
 * @name: name of the function to define
 * @flush: driver callback to flush an ``XDP_TX`` bulk
 * @finalize: driver callback to finalize an XDPSQ and run the timer
 */
#define LIBETH_XDP_DEFINE_FINALIZE(name, flush, finalize)		      \
	__LIBETH_XDP_DEFINE_FINALIZE(name, flush, finalize, xdp)

#define __LIBETH_XDP_DEFINE_FINALIZE(name, flush, finalize, pfx)	      \
void name(struct libeth_xdp_tx_bulk *bq)				      \
{									      \
	libeth_##pfx##_finalize_rx(bq, flush, finalize);		      \
}

#define LIBETH_XDP_DEFINE_END()		__diag_pop()

/* XMO */

/**
 * libeth_xdp_buff_to_rq - get RQ pointer from an XDP buffer pointer
 * @xdp: &libeth_xdp_buff corresponding to the queue
 * @type: typeof() of the driver Rx queue structure
 * @member: name of &xdp_rxq_info inside @type
 *
 * Often times, pointer to the RQ is needed when reading/filling metadata from
 * HW descriptors. The helper can be used to quickly jump from an XDP buffer
 * to the queue corresponding to its &xdp_rxq_info without introducing
 * additional fields (&libeth_xdp_buff is precisely 1 cacheline long on x64).
 */
#define libeth_xdp_buff_to_rq(xdp, type, member)			      \
	container_of_const((xdp)->base.rxq, type, member)

/**
 * libeth_xdpmo_rx_hash - convert &libeth_rx_pt to an XDP RSS hash metadata
 * @hash: pointer to the variable to write the hash to
 * @rss_type: pointer to the variable to write the hash type to
 * @val: hash value from the HW descriptor
 * @pt: libeth parsed packet type
 *
 * Handle zeroed/non-available hash and convert libeth parsed packet type to
 * the corresponding XDP RSS hash type. To be called at the end of
 * xdp_metadata_ops idpf_xdpmo::xmo_rx_hash() implementation.
 * Note that if the driver doesn't use a constant packet type lookup table but
 * generates it at runtime, it must call libeth_rx_pt_gen_hash_type(pt) to
 * generate XDP RSS hash type for each packet type.
 *
 * Return: 0 on success, -ENODATA when the hash is not available.
 */
static inline int libeth_xdpmo_rx_hash(u32 *hash,
				       enum xdp_rss_hash_type *rss_type,
				       u32 val, struct libeth_rx_pt pt)
{
	if (unlikely(!val))
		return -ENODATA;

	*hash = val;
	*rss_type = pt.hash_type;

	return 0;
}

/* Tx buffer completion */

void libeth_xdp_return_buff_bulk(const struct skb_shared_info *sinfo,
				 struct xdp_frame_bulk *bq, bool frags);
void libeth_xsk_buff_free_slow(struct libeth_xdp_buff *xdp);

/**
 * __libeth_xdp_complete_tx - complete sent XDPSQE
 * @sqe: SQ element / Tx buffer to complete
 * @cp: Tx polling/completion params
 * @bulk: internal callback to bulk-free ``XDP_TX`` buffers
 * @xsk: internal callback to free XSk ``XDP_TX`` buffers
 *
 * Use the non-underscored version in drivers instead. This one is shared
 * internally with libeth_tx_complete_any().
 * Complete an XDPSQE of any type of XDP frame. This includes DMA unmapping
 * when needed, buffer freeing, stats update, and SQE invalidation.
 */
static __always_inline void
__libeth_xdp_complete_tx(struct libeth_sqe *sqe, struct libeth_cq_pp *cp,
			 typeof(libeth_xdp_return_buff_bulk) bulk,
			 typeof(libeth_xsk_buff_free_slow) xsk)
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
	case LIBETH_SQE_XSK_TX:
	case LIBETH_SQE_XSK_TX_FRAG:
		xsk(sqe->xsk);
		break;
	default:
		break;
	}

	switch (type) {
	case LIBETH_SQE_XDP_TX:
	case LIBETH_SQE_XDP_XMIT:
	case LIBETH_SQE_XSK_TX:
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
	__libeth_xdp_complete_tx(sqe, cp, libeth_xdp_return_buff_bulk,
				 libeth_xsk_buff_free_slow);
}

/* Misc */

u32 libeth_xdp_queue_threshold(u32 count);

void __libeth_xdp_set_features(struct net_device *dev,
			       const struct xdp_metadata_ops *xmo,
			       u32 zc_segs,
			       const struct xsk_tx_metadata_ops *tmo);
void libeth_xdp_set_redirect(struct net_device *dev, bool enable);

/**
 * libeth_xdp_set_features - set XDP features for netdev
 * @dev: &net_device to configure
 * @...: optional params, see __libeth_xdp_set_features()
 *
 * Set all the features libeth_xdp supports, including .ndo_xdp_xmit(). That
 * said, it should be used only when XDPSQs are always available regardless
 * of whether an XDP prog is attached to @dev.
 */
#define libeth_xdp_set_features(dev, ...)				      \
	CONCATENATE(__libeth_xdp_feat,					      \
		    COUNT_ARGS(__VA_ARGS__))(dev, ##__VA_ARGS__)

#define __libeth_xdp_feat0(dev)						      \
	__libeth_xdp_set_features(dev, NULL, 0, NULL)
#define __libeth_xdp_feat1(dev, xmo)					      \
	__libeth_xdp_set_features(dev, xmo, 0, NULL)
#define __libeth_xdp_feat2(dev, xmo, zc_segs)				      \
	__libeth_xdp_set_features(dev, xmo, zc_segs, NULL)
#define __libeth_xdp_feat3(dev, xmo, zc_segs, tmo)			      \
	__libeth_xdp_set_features(dev, xmo, zc_segs, tmo)

/**
 * libeth_xdp_set_features_noredir - enable all libeth_xdp features w/o redir
 * @dev: target &net_device
 * @...: optional params, see __libeth_xdp_set_features()
 *
 * Enable everything except the .ndo_xdp_xmit() feature, use when XDPSQs are
 * not available right after netdev registration.
 */
#define libeth_xdp_set_features_noredir(dev, ...)			      \
	__libeth_xdp_set_features_noredir(dev, __UNIQUE_ID(dev_),	      \
					  ##__VA_ARGS__)

#define __libeth_xdp_set_features_noredir(dev, ud, ...) do {		      \
	struct net_device *ud = (dev);					      \
									      \
	libeth_xdp_set_features(ud, ##__VA_ARGS__);			      \
	libeth_xdp_set_redirect(ud, false);				      \
} while (0)

#define libeth_xsktmo			((const void *)GOLDEN_RATIO_PRIME)

#endif /* __LIBETH_XDP_H */
