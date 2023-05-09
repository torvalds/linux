// SPDX-License-Identifier: GPL-2.0

int io_uring_cmd(struct io_kiocb *req, unsigned int issue_flags);
int io_uring_cmd_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_uring_cmd_prep_async(struct io_kiocb *req);
