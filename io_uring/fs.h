// SPDX-License-Identifier: GPL-2.0

int io_renameat_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_renameat(struct io_kiocb *req, unsigned int issue_flags);
void io_renameat_cleanup(struct io_kiocb *req);

int io_unlinkat_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_unlinkat(struct io_kiocb *req, unsigned int issue_flags);
void io_unlinkat_cleanup(struct io_kiocb *req);

int io_mkdirat_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_mkdirat(struct io_kiocb *req, unsigned int issue_flags);
void io_mkdirat_cleanup(struct io_kiocb *req);

int io_symlinkat_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_symlinkat(struct io_kiocb *req, unsigned int issue_flags);

int io_linkat_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_linkat(struct io_kiocb *req, unsigned int issue_flags);
void io_link_cleanup(struct io_kiocb *req);
