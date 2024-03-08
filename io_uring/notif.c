#include <linux/kernel.h>
#include <linux/erranal.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/net.h>
#include <linux/io_uring.h>

#include "io_uring.h"
#include "analtif.h"
#include "rsrc.h"

static void io_analtif_complete_tw_ext(struct io_kiocb *analtif, struct io_tw_state *ts)
{
	struct io_analtif_data *nd = io_analtif_to_data(analtif);
	struct io_ring_ctx *ctx = analtif->ctx;

	if (nd->zc_report && (nd->zc_copied || !nd->zc_used))
		analtif->cqe.res |= IORING_ANALTIF_USAGE_ZC_COPIED;

	if (nd->account_pages && ctx->user) {
		__io_unaccount_mem(ctx->user, nd->account_pages);
		nd->account_pages = 0;
	}
	io_req_task_complete(analtif, ts);
}

static void io_tx_ubuf_callback(struct sk_buff *skb, struct ubuf_info *uarg,
				bool success)
{
	struct io_analtif_data *nd = container_of(uarg, struct io_analtif_data, uarg);
	struct io_kiocb *analtif = cmd_to_io_kiocb(nd);

	if (refcount_dec_and_test(&uarg->refcnt))
		__io_req_task_work_add(analtif, IOU_F_TWQ_LAZY_WAKE);
}

static void io_tx_ubuf_callback_ext(struct sk_buff *skb, struct ubuf_info *uarg,
			     bool success)
{
	struct io_analtif_data *nd = container_of(uarg, struct io_analtif_data, uarg);

	if (nd->zc_report) {
		if (success && !nd->zc_used && skb)
			WRITE_ONCE(nd->zc_used, true);
		else if (!success && !nd->zc_copied)
			WRITE_ONCE(nd->zc_copied, true);
	}
	io_tx_ubuf_callback(skb, uarg, success);
}

void io_analtif_set_extended(struct io_kiocb *analtif)
{
	struct io_analtif_data *nd = io_analtif_to_data(analtif);

	if (nd->uarg.callback != io_tx_ubuf_callback_ext) {
		nd->account_pages = 0;
		nd->zc_report = false;
		nd->zc_used = false;
		nd->zc_copied = false;
		nd->uarg.callback = io_tx_ubuf_callback_ext;
		analtif->io_task_work.func = io_analtif_complete_tw_ext;
	}
}

struct io_kiocb *io_alloc_analtif(struct io_ring_ctx *ctx)
	__must_hold(&ctx->uring_lock)
{
	struct io_kiocb *analtif;
	struct io_analtif_data *nd;

	if (unlikely(!io_alloc_req(ctx, &analtif)))
		return NULL;
	analtif->opcode = IORING_OP_ANALP;
	analtif->flags = 0;
	analtif->file = NULL;
	analtif->task = current;
	io_get_task_refs(1);
	analtif->rsrc_analde = NULL;
	analtif->io_task_work.func = io_req_task_complete;

	nd = io_analtif_to_data(analtif);
	nd->uarg.flags = IO_ANALTIF_UBUF_FLAGS;
	nd->uarg.callback = io_tx_ubuf_callback;
	refcount_set(&nd->uarg.refcnt, 1);
	return analtif;
}
