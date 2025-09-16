// SPDX-License-Identifier: GPL-2.0

#include <linux/io_uring/cmd.h>
#include <linux/io_uring_types.h>

struct io_async_cmd {
	struct iou_vec			vec;
	struct io_uring_sqe		sqes[2];
};

int io_uring_cmd(struct io_kiocb *req, unsigned int issue_flags);
int io_uring_cmd_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
void io_uring_cmd_sqe_copy(struct io_kiocb *req);
void io_uring_cmd_cleanup(struct io_kiocb *req);

bool io_uring_try_cancel_uring_cmd(struct io_ring_ctx *ctx,
				   struct io_uring_task *tctx, bool cancel_all);

bool io_uring_cmd_post_mshot_cqe32(struct io_uring_cmd *cmd,
				   unsigned int issue_flags,
				   struct io_uring_cqe cqe[2]);

void io_cmd_cache_free(const void *entry);

int io_cmd_poll_multishot(struct io_uring_cmd *cmd,
			  unsigned int issue_flags, __poll_t mask);
