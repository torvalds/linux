// SPDX-License-Identifier: GPL-2.0

#include <linux/net.h>
#include <linux/uio.h>
#include <net/sock.h>
#include <linux/nospec.h>

#include "rsrc.h"

#define IO_NOTIF_SPLICE_BATCH	32

struct io_notif_data {
	struct file		*file;
	struct ubuf_info	uarg;
	unsigned long		account_pages;
	bool			zc_report;
	bool			zc_used;
	bool			zc_copied;
};

struct io_kiocb *io_alloc_notif(struct io_ring_ctx *ctx);
void io_notif_set_extended(struct io_kiocb *notif);

static inline struct io_notif_data *io_notif_to_data(struct io_kiocb *notif)
{
	return io_kiocb_to_cmd(notif, struct io_notif_data);
}

static inline void io_notif_flush(struct io_kiocb *notif)
	__must_hold(&notif->ctx->uring_lock)
{
	struct io_notif_data *nd = io_notif_to_data(notif);

	/* drop slot's master ref */
	if (refcount_dec_and_test(&nd->uarg.refcnt))
		io_req_task_work_add(notif);
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
