// SPDX-License-Identifier: GPL-2.0

int io_nop_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_nop(struct io_kiocb *req, unsigned int issue_flags);
