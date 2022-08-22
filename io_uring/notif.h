// SPDX-License-Identifier: GPL-2.0

#include <linux/net.h>
#include <linux/uio.h>
#include <net/sock.h>
#include <linux/nospec.h>

#include "rsrc.h"

#define IO_NOTIF_SPLICE_BATCH	32
#define IORING_MAX_NOTIF_SLOTS	(1U << 15)

struct io_notif_data {
	struct file		*file;
	struct ubuf_info	uarg;
	unsigned long		account_pages;
};

struct io_notif_slot {
	/*
	 * Current/active notifier. A slot holds only one active notifier at a
	 * time and keeps one reference to it. Flush releases the reference and
	 * lazily replaces it with a new notifier.
	 */
	struct io_kiocb		*notif;

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

int io_notif_register(struct io_ring_ctx *ctx,
		      void __user *arg, unsigned int size);
int io_notif_unregister(struct io_ring_ctx *ctx);

void io_notif_slot_flush(struct io_notif_slot *slot);
struct io_kiocb *io_alloc_notif(struct io_ring_ctx *ctx,
				struct io_notif_slot *slot);

static inline struct io_notif_data *io_notif_to_data(struct io_kiocb *notif)
{
	return io_kiocb_to_cmd(notif, struct io_notif_data);
}

static inline struct io_kiocb *io_get_notif(struct io_ring_ctx *ctx,
					    struct io_notif_slot *slot)
{
	if (!slot->notif)
		slot->notif = io_alloc_notif(ctx, slot);
	return slot->notif;
}

static inline struct io_notif_slot *io_get_notif_slot(struct io_ring_ctx *ctx,
						      unsigned idx)
	__must_hold(&ctx->uring_lock)
{
	if (idx >= ctx->nr_notif_slots)
		return NULL;
	idx = array_index_nospec(idx, ctx->nr_notif_slots);
	return &ctx->notif_slots[idx];
}

static inline void io_notif_slot_flush_submit(struct io_notif_slot *slot,
					      unsigned int issue_flags)
{
	io_notif_slot_flush(slot);
}

static inline int io_notif_account_mem(struct io_kiocb *notif, unsigned len)
{
	struct io_ring_ctx *ctx = notif->ctx;
	struct io_notif_data *nd = io_notif_to_data(notif);
	unsigned nr_pages = (len >> PAGE_SHIFT) + 2;
	int ret;

	if (ctx->user) {
		ret = __io_account_mem(ctx->user, nr_pages);
		if (ret)
			return ret;
		nd->account_pages += nr_pages;
	}
	return 0;
}
