// SPDX-License-Identifier: GPL-2.0

#if defined(CONFIG_EPOLL)
int io_epoll_ctl_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_epoll_ctl(struct io_kiocb *req, unsigned int issue_flags);
#endif
