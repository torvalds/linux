// SPDX-License-Identifier: GPL-2.0

#include <linux/pagemap.h>

struct io_meta_state {
	u32			seed;
	struct iov_iter_state	iter_meta;
};

struct io_async_rw {
	size_t				bytes_done;
	struct iov_iter			iter;
	struct iov_iter_state		iter_state;
	struct iovec			fast_iov;
	struct iovec			*free_iovec;
	int				free_iov_nr;
	/* wpq is for buffered io, while meta fields are used with direct io */
	union {
		struct wait_page_queue		wpq;
		struct {
			struct uio_meta			meta;
			struct io_meta_state		meta_state;
		};
	};
};

int io_prep_read_fixed(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_prep_write_fixed(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_prep_readv(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_prep_writev(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_prep_read(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_prep_write(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_read(struct io_kiocb *req, unsigned int issue_flags);
int io_write(struct io_kiocb *req, unsigned int issue_flags);
void io_readv_writev_cleanup(struct io_kiocb *req);
void io_rw_fail(struct io_kiocb *req);
void io_req_rw_complete(struct io_kiocb *req, struct io_tw_state *ts);
int io_read_mshot_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_read_mshot(struct io_kiocb *req, unsigned int issue_flags);
void io_rw_cache_free(const void *entry);
