// SPDX-License-Identifier: GPL-2.0

int io_statx_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_statx(struct io_kiocb *req, unsigned int issue_flags);
void io_statx_cleanup(struct io_kiocb *req);
