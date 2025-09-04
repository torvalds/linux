// SPDX-License-Identifier: GPL-2.0

#ifndef IO_URING_MSG_RING_H
#define IO_URING_MSG_RING_H

struct io_uring_sqe;
struct io_kiocb;

int io_uring_sync_msg_ring(struct io_uring_sqe *sqe);
int io_msg_ring_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_msg_ring(struct io_kiocb *req, unsigned int issue_flags);
void io_msg_ring_cleanup(struct io_kiocb *req);

#endif
