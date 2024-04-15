#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/net.h>
#include <linux/io_uring.h>

#include "io_uring.h"
#include "notif.h"
#include "rsrc.h"

void io_notif_tw_complete(struct io_kiocb *notif, struct io_tw_state *ts)
{
	struct io_notif_data *nd = io_notif_to_data(notif);

	if (unlikely(nd->zc_report) && (nd->zc_copied || !nd->zc_used))
		notif->cqe.res |= IORING_NOTIF_USAGE_ZC_COPIED;

	if (nd->account_pages && notif->ctx->user) {
		__io_unaccount_mem(notif->ctx->user, nd->account_pages);
		nd->account_pages = 0;
	}
	io_req_task_complete(notif, ts);
}

static void io_tx_ubuf_callback(struct sk_buff *skb, struct ubuf_info *uarg,
				bool success)
{
	struct io_notif_data *nd = container_of(uarg, struct io_notif_data, uarg);
	struct io_kiocb *notif = cmd_to_io_kiocb(nd);

	if (nd->zc_report) {
		if (success && !nd->zc_used && skb)
			WRITE_ONCE(nd->zc_used, true);
		else if (!success && !nd->zc_copied)
			WRITE_ONCE(nd->zc_copied, true);
	}

	if (!refcount_dec_and_test(&uarg->refcnt))
		return;

	notif->io_task_work.func = io_notif_tw_complete;
	__io_req_task_work_add(notif, IOU_F_TWQ_LAZY_WAKE);
}

struct io_kiocb *io_alloc_notif(struct io_ring_ctx *ctx)
	__must_hold(&ctx->uring_lock)
{
	struct io_kiocb *notif;
	struct io_notif_data *nd;

	if (unlikely(!io_alloc_req(ctx, &notif)))
		return NULL;
	notif->opcode = IORING_OP_NOP;
	notif->flags = 0;
	notif->file = NULL;
	notif->task = current;
	io_get_task_refs(1);
	notif->rsrc_node = NULL;

	nd = io_notif_to_data(notif);
	nd->zc_report = false;
	nd->account_pages = 0;
	nd->uarg.flags = IO_NOTIF_UBUF_FLAGS;
	nd->uarg.callback = io_tx_ubuf_callback;
	refcount_set(&nd->uarg.refcnt, 1);
	return notif;
}
