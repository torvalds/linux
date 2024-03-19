// SPDX-License-Identifier: GPL-2.0

struct uring_cache {
	union {
		struct io_cache_entry cache;
		struct io_uring_sqe sqes[2];
	};
};

int io_uring_cmd(struct io_kiocb *req, unsigned int issue_flags);
int io_uring_cmd_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_uring_cmd_prep_async(struct io_kiocb *req);
void io_uring_cache_free(struct io_cache_entry *entry);

bool io_uring_try_cancel_uring_cmd(struct io_ring_ctx *ctx,
				   struct task_struct *task, bool cancel_all);
