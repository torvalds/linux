// SPDX-License-Identifier: GPL-2.0

struct io_timeout_data {
	struct io_kiocb			*req;
	struct hrtimer			timer;
	struct timespec64		ts;
	enum hrtimer_mode		mode;
	u32				flags;
};

__cold void io_flush_timeouts(struct io_ring_ctx *ctx);
struct io_cancel_data;
int io_timeout_cancel(struct io_ring_ctx *ctx, struct io_cancel_data *cd);
__cold bool io_kill_timeouts(struct io_ring_ctx *ctx, struct io_uring_task *tctx,
			     bool cancel_all);
void io_queue_linked_timeout(struct io_kiocb *req);
void io_disarm_next(struct io_kiocb *req);

int io_timeout_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_link_timeout_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_timeout(struct io_kiocb *req, unsigned int issue_flags);
int io_timeout_remove_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_timeout_remove(struct io_kiocb *req, unsigned int issue_flags);
