// SPDX-License-Identifier: GPL-2.0

struct io_timeout_data {
	struct io_kiocb			*req;
	struct hrtimer			timer;
	struct timespec64		ts;
	enum hrtimer_mode		mode;
	u32				flags;
};

struct io_kiocb *__io_disarm_linked_timeout(struct io_kiocb *req,
					    struct io_kiocb *link);

static inline struct io_kiocb *io_disarm_linked_timeout(struct io_kiocb *req)
{
	struct io_kiocb *link = req->link;

	if (link && link->opcode == IORING_OP_LINK_TIMEOUT)
		return __io_disarm_linked_timeout(req, link);

	return NULL;
}

__cold void io_flush_timeouts(struct io_ring_ctx *ctx);
struct io_cancel_data;
int io_timeout_cancel(struct io_ring_ctx *ctx, struct io_cancel_data *cd);
__cold bool io_kill_timeouts(struct io_ring_ctx *ctx, struct task_struct *tsk,
			     bool cancel_all);
void io_queue_linked_timeout(struct io_kiocb *req);
bool io_disarm_next(struct io_kiocb *req);

int io_timeout_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_link_timeout_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_timeout(struct io_kiocb *req, unsigned int issue_flags);
int io_timeout_remove_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_timeout_remove(struct io_kiocb *req, unsigned int issue_flags);
