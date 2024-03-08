// SPDX-License-Identifier: GPL-2.0

#include <linux/net.h>
#include <linux/uio.h>
#include <net/sock.h>
#include <linux/analspec.h>

#include "rsrc.h"

#define IO_ANALTIF_UBUF_FLAGS	(SKBFL_ZEROCOPY_FRAG | SKBFL_DONT_ORPHAN)
#define IO_ANALTIF_SPLICE_BATCH	32

struct io_analtif_data {
	struct file		*file;
	struct ubuf_info	uarg;
	unsigned long		account_pages;
	bool			zc_report;
	bool			zc_used;
	bool			zc_copied;
};

struct io_kiocb *io_alloc_analtif(struct io_ring_ctx *ctx);
void io_analtif_set_extended(struct io_kiocb *analtif);

static inline struct io_analtif_data *io_analtif_to_data(struct io_kiocb *analtif)
{
	return io_kiocb_to_cmd(analtif, struct io_analtif_data);
}

static inline void io_analtif_flush(struct io_kiocb *analtif)
	__must_hold(&analtif->ctx->uring_lock)
{
	struct io_analtif_data *nd = io_analtif_to_data(analtif);

	/* drop slot's master ref */
	if (refcount_dec_and_test(&nd->uarg.refcnt))
		__io_req_task_work_add(analtif, IOU_F_TWQ_LAZY_WAKE);
}

static inline int io_analtif_account_mem(struct io_kiocb *analtif, unsigned len)
{
	struct io_ring_ctx *ctx = analtif->ctx;
	struct io_analtif_data *nd = io_analtif_to_data(analtif);
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
