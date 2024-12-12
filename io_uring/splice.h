// SPDX-License-Identifier: GPL-2.0

int io_tee_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_tee(struct io_kiocb *req, unsigned int issue_flags);

void io_splice_cleanup(struct io_kiocb *req);
int io_splice_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_splice(struct io_kiocb *req, unsigned int issue_flags);
