#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/net.h>
#include <linux/io_uring.h>

#include "io_uring.h"
#include "notif.h"
#include "rsrc.h"

static void __io_notif_complete_tw(struct io_kiocb *notif, bool *locked)
{
	struct io_notif_data *nd = io_notif_to_data(notif);
	struct io_ring_ctx *ctx = notif->ctx;

	if (nd->account_pages && ctx->user) {
		__io_unaccount_mem(ctx->user, nd->account_pages);
		nd->account_pages = 0;
	}
	io_req_task_complete(notif, locked);
}

static inline void io_notif_complete(struct io_kiocb *notif)
	__must_hold(&notif->ctx->uring_lock)
{
	bool locked = true;

	__io_notif_complete_tw(notif, &locked);
}

static void io_uring_tx_zerocopy_callback(struct sk_buff *skb,
					  struct ubuf_info *uarg,
					  bool success)
{
	struct io_notif_data *nd = container_of(uarg, struct io_notif_data, uarg);
	struct io_kiocb *notif = cmd_to_io_kiocb(nd);

	if (refcount_dec_and_test(&uarg->refcnt)) {
		notif->io_task_work.func = __io_notif_complete_tw;
		io_req_task_work_add(notif);
	}
}

struct io_kiocb *io_alloc_notif(struct io_ring_ctx *ctx,
				struct io_notif_slot *slot)
	__must_hold(&ctx->uring_lock)
{
	struct io_kiocb *notif;
	struct io_notif_data *nd;

	if (unlikely(!io_alloc_req_refill(ctx)))
		return NULL;
	notif = io_alloc_req(ctx);
	notif->opcode = IORING_OP_NOP;
	notif->flags = 0;
	notif->file = NULL;
	notif->task = current;
	io_get_task_refs(1);
	notif->rsrc_node = NULL;
	io_req_set_rsrc_node(notif, ctx, 0);
	notif->cqe.user_data = slot->tag;
	notif->cqe.flags = slot->seq++;
	notif->cqe.res = 0;

	nd = io_notif_to_data(notif);
	nd->account_pages = 0;
	nd->uarg.flags = SKBFL_ZEROCOPY_FRAG | SKBFL_DONT_ORPHAN;
	nd->uarg.callback = io_uring_tx_zerocopy_callback;
	/* master ref owned by io_notif_slot, will be dropped on flush */
	refcount_set(&nd->uarg.refcnt, 1);
	return notif;
}

void io_notif_slot_flush(struct io_notif_slot *slot)
	__must_hold(&slot->notif->ctx->uring_lock)
{
	struct io_kiocb *notif = slot->notif;
	struct io_notif_data *nd = io_notif_to_data(notif);

	slot->notif = NULL;

	/* drop slot's master ref */
	if (refcount_dec_and_test(&nd->uarg.refcnt)) {
		notif->io_task_work.func = __io_notif_complete_tw;
		io_req_task_work_add(notif);
	}
}

__cold int io_notif_unregister(struct io_ring_ctx *ctx)
	__must_hold(&ctx->uring_lock)
{
	int i;

	if (!ctx->notif_slots)
		return -ENXIO;

	for (i = 0; i < ctx->nr_notif_slots; i++) {
		struct io_notif_slot *slot = &ctx->notif_slots[i];
		struct io_kiocb *notif = slot->notif;
		struct io_notif_data *nd;

		if (!notif)
			continue;
		nd = io_notif_to_data(notif);
		slot->notif = NULL;
		if (!refcount_dec_and_test(&nd->uarg.refcnt))
			continue;
		notif->io_task_work.func = __io_notif_complete_tw;
		io_req_task_work_add(notif);
	}

	kvfree(ctx->notif_slots);
	ctx->notif_slots = NULL;
	ctx->nr_notif_slots = 0;
	return 0;
}

__cold int io_notif_register(struct io_ring_ctx *ctx,
			     void __user *arg, unsigned int size)
	__must_hold(&ctx->uring_lock)
{
	struct io_uring_notification_slot __user *slots;
	struct io_uring_notification_slot slot;
	struct io_uring_notification_register reg;
	unsigned i;

	if (ctx->nr_notif_slots)
		return -EBUSY;
	if (size != sizeof(reg))
		return -EINVAL;
	if (copy_from_user(&reg, arg, sizeof(reg)))
		return -EFAULT;
	if (!reg.nr_slots || reg.nr_slots > IORING_MAX_NOTIF_SLOTS)
		return -EINVAL;
	if (reg.resv || reg.resv2 || reg.resv3)
		return -EINVAL;

	slots = u64_to_user_ptr(reg.data);
	ctx->notif_slots = kvcalloc(reg.nr_slots, sizeof(ctx->notif_slots[0]),
				GFP_KERNEL_ACCOUNT);
	if (!ctx->notif_slots)
		return -ENOMEM;

	for (i = 0; i < reg.nr_slots; i++, ctx->nr_notif_slots++) {
		struct io_notif_slot *notif_slot = &ctx->notif_slots[i];

		if (copy_from_user(&slot, &slots[i], sizeof(slot))) {
			io_notif_unregister(ctx);
			return -EFAULT;
		}
		if (slot.resv[0] | slot.resv[1] | slot.resv[2]) {
			io_notif_unregister(ctx);
			return -EINVAL;
		}
		notif_slot->tag = slot.tag;
	}
	return 0;
}
