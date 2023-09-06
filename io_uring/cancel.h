// SPDX-License-Identifier: GPL-2.0

#include <linux/io_uring_types.h>

struct io_cancel_data {
	struct io_ring_ctx *ctx;
	union {
		u64 data;
		struct file *file;
	};
	u8 opcode;
	u32 flags;
	int seq;
};

int io_async_cancel_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_async_cancel(struct io_kiocb *req, unsigned int issue_flags);

int io_try_cancel(struct io_uring_task *tctx, struct io_cancel_data *cd,
		  unsigned int issue_flags);
void init_hash_table(struct io_hash_table *table, unsigned size);

int io_sync_cancel(struct io_ring_ctx *ctx, void __user *arg);
bool io_cancel_req_match(struct io_kiocb *req, struct io_cancel_data *cd);
