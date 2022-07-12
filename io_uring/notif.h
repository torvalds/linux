// SPDX-License-Identifier: GPL-2.0

#include <linux/net.h>
#include <linux/uio.h>
#include <net/sock.h>
#include <linux/nospec.h>

#define IO_NOTIF_SPLICE_BATCH	32

struct io_notif {
	struct ubuf_info	uarg;
	struct io_ring_ctx	*ctx;

	/* cqe->user_data, io_notif_slot::tag if not overridden */
	u64			tag;
	/* see struct io_notif_slot::seq */
	u32			seq;
	/* hook into ctx->notif_list and ctx->notif_list_locked */
	struct list_head	cache_node;

	union {
		struct callback_head	task_work;
		struct work_struct	commit_work;
	};
};

struct io_notif_slot {
	/*
	 * Current/active notifier. A slot holds only one active notifier at a
	 * time and keeps one reference to it. Flush releases the reference and
	 * lazily replaces it with a new notifier.
	 */
	struct io_notif		*notif;

	/*
	 * Default ->user_data for this slot notifiers CQEs
	 */
	u64			tag;
	/*
	 * Notifiers of a slot live in generations, we create a new notifier
	 * only after flushing the previous one. Track the sequential number
	 * for all notifiers and copy it into notifiers's cqe->cflags
	 */
	u32			seq;
};

int io_notif_unregister(struct io_ring_ctx *ctx);
void io_notif_cache_purge(struct io_ring_ctx *ctx);

struct io_notif *io_alloc_notif(struct io_ring_ctx *ctx,
				struct io_notif_slot *slot);

static inline struct io_notif *io_get_notif(struct io_ring_ctx *ctx,
					    struct io_notif_slot *slot)
{
	if (!slot->notif)
		slot->notif = io_alloc_notif(ctx, slot);
	return slot->notif;
}

static inline struct io_notif_slot *io_get_notif_slot(struct io_ring_ctx *ctx,
						      int idx)
	__must_hold(&ctx->uring_lock)
{
	if (idx >= ctx->nr_notif_slots)
		return NULL;
	idx = array_index_nospec(idx, ctx->nr_notif_slots);
	return &ctx->notif_slots[idx];
}
