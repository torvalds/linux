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

struct io_kiocb *io_alloc_notif(struct io_ring_ctx *ctx)
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

	nd = io_notif_to_data(notif);
	nd->account_pages = 0;
	nd->uarg.flags = SKBFL_ZEROCOPY_FRAG | SKBFL_DONT_ORPHAN;
	nd->uarg.callback = io_uring_tx_zerocopy_callback;
	refcount_set(&nd->uarg.refcnt, 1);
	return notif;
}

void io_notif_flush(struct io_kiocb *notif)
	__must_hold(&slot->notif->ctx->uring_lock)
{
	struct io_notif_data *nd = io_notif_to_data(notif);

	/* drop slot's master ref */
	if (refcount_dec_and_test(&nd->uarg.refcnt)) {
		notif->io_task_work.func = __io_notif_complete_tw;
		io_req_task_work_add(notif);
	}
}
