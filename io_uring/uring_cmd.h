// SPDX-License-Identifier: GPL-2.0

#include <linux/io_uring/cmd.h>
#include <linux/io_uring_types.h>

struct io_async_cmd {
	struct io_uring_cmd_data	data;
	struct iou_vec			vec;
	struct io_uring_sqe		sqes[2];
};

int io_uring_cmd(struct io_kiocb *req, unsigned int issue_flags);
int io_uring_cmd_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
void io_uring_cmd_cleanup(struct io_kiocb *req);

bool io_uring_try_cancel_uring_cmd(struct io_ring_ctx *ctx,
				   struct io_uring_task *tctx, bool cancel_all);

void io_cmd_cache_free(const void *entry);
