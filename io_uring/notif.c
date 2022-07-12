#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/net.h>
#include <linux/io_uring.h>

#include "io_uring.h"
#include "notif.h"

static void __io_notif_complete_tw(struct callback_head *cb)
{
	struct io_notif *notif = container_of(cb, struct io_notif, task_work);
	struct io_ring_ctx *ctx = notif->ctx;

	io_cq_lock(ctx);
	io_fill_cqe_aux(ctx, notif->tag, 0, notif->seq, true);
	io_cq_unlock_post(ctx);

	percpu_ref_put(&ctx->refs);
	kfree(notif);
}

static inline void io_notif_complete(struct io_notif *notif)
{
	__io_notif_complete_tw(&notif->task_work);
}

static void io_notif_complete_wq(struct work_struct *work)
{
	struct io_notif *notif = container_of(work, struct io_notif, commit_work);

	io_notif_complete(notif);
}

static void io_uring_tx_zerocopy_callback(struct sk_buff *skb,
					  struct ubuf_info *uarg,
					  bool success)
{
	struct io_notif *notif = container_of(uarg, struct io_notif, uarg);

	if (!refcount_dec_and_test(&uarg->refcnt))
		return;
	INIT_WORK(&notif->commit_work, io_notif_complete_wq);
	queue_work(system_unbound_wq, &notif->commit_work);
}

struct io_notif *io_alloc_notif(struct io_ring_ctx *ctx,
				struct io_notif_slot *slot)
	__must_hold(&ctx->uring_lock)
{
	struct io_notif *notif;

	notif = kzalloc(sizeof(*notif), GFP_ATOMIC | __GFP_ACCOUNT);
	if (!notif)
		return NULL;

	notif->seq = slot->seq++;
	notif->tag = slot->tag;
	notif->ctx = ctx;
	notif->uarg.flags = SKBFL_ZEROCOPY_FRAG | SKBFL_DONT_ORPHAN;
	notif->uarg.callback = io_uring_tx_zerocopy_callback;
	/* master ref owned by io_notif_slot, will be dropped on flush */
	refcount_set(&notif->uarg.refcnt, 1);
	percpu_ref_get(&ctx->refs);
	return notif;
}

static void io_notif_slot_flush(struct io_notif_slot *slot)
	__must_hold(&ctx->uring_lock)
{
	struct io_notif *notif = slot->notif;

	slot->notif = NULL;

	if (WARN_ON_ONCE(in_interrupt()))
		return;
	/* drop slot's master ref */
	if (refcount_dec_and_test(&notif->uarg.refcnt))
		io_notif_complete(notif);
}

__cold int io_notif_unregister(struct io_ring_ctx *ctx)
	__must_hold(&ctx->uring_lock)
{
	int i;

	if (!ctx->notif_slots)
		return -ENXIO;

	for (i = 0; i < ctx->nr_notif_slots; i++) {
		struct io_notif_slot *slot = &ctx->notif_slots[i];

		if (slot->notif)
			io_notif_slot_flush(slot);
	}

	kvfree(ctx->notif_slots);
	ctx->notif_slots = NULL;
	ctx->nr_notif_slots = 0;
	return 0;
}