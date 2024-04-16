// SPDX-License-Identifier: GPL-2.0

int io_madvise_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_madvise(struct io_kiocb *req, unsigned int issue_flags);

int io_fadvise_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_fadvise(struct io_kiocb *req, unsigned int issue_flags);
