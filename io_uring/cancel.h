// SPDX-License-Identifier: GPL-2.0

int io_async_cancel_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_async_cancel(struct io_kiocb *req, unsigned int issue_flags);

int io_try_cancel(struct io_kiocb *req, struct io_cancel_data *cd);
void init_hash_table(struct io_hash_table *table, unsigned size);
