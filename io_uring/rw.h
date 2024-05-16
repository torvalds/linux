// SPDX-License-Identifier: GPL-2.0

#include <linux/pagemap.h>

struct io_rw_state {
	struct iov_iter			iter;
	struct iov_iter_state		iter_state;
	struct iovec			fast_iov[UIO_FASTIOV];
};

struct io_async_rw {
	struct io_rw_state		s;
	const struct iovec		*free_iovec;
	size_t				bytes_done;
	struct wait_page_queue		wpq;
};

int io_prep_rw(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_prep_rwv(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_prep_rw_fixed(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_read(struct io_kiocb *req, unsigned int issue_flags);
int io_readv_prep_async(struct io_kiocb *req);
int io_write(struct io_kiocb *req, unsigned int issue_flags);
int io_writev_prep_async(struct io_kiocb *req);
void io_readv_writev_cleanup(struct io_kiocb *req);
void io_rw_fail(struct io_kiocb *req);
void io_req_rw_complete(struct io_kiocb *req, struct io_tw_state *ts);
int io_read_mshot_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_read_mshot(struct io_kiocb *req, unsigned int issue_flags);
