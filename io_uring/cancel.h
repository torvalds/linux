// SPDX-License-Identifier: GPL-2.0

int io_async_cancel_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_async_cancel(struct io_kiocb *req, unsigned int issue_flags);

int io_try_cancel(struct io_kiocb *req, struct io_cancel_data *cd);
void init_hash_table(struct io_hash_bucket *hash_table, unsigned size);

struct io_hash_bucket {
	spinlock_t		lock;
	struct hlist_head	list;
} ____cacheline_aligned_in_smp;
