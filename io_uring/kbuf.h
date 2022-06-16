// SPDX-License-Identifier: GPL-2.0
#ifndef IOU_KBUF_H
#define IOU_KBUF_H

#include <uapi/linux/io_uring.h>

struct io_buffer_list {
	/*
	 * If ->buf_nr_pages is set, then buf_pages/buf_ring are used. If not,
	 * then these are classic provided buffers and ->buf_list is used.
	 */
	union {
		struct list_head buf_list;
		struct {
			struct page **buf_pages;
			struct io_uring_buf_ring *buf_ring;
		};
	};
	__u16 bgid;

	/* below is for ring provided buffers */
	__u16 buf_nr_pages;
	__u16 nr_entries;
	__u16 head;
	__u16 mask;
};

struct io_buffer {
	struct list_head list;
	__u64 addr;
	__u32 len;
	__u16 bid;
	__u16 bgid;
};

void __user *io_buffer_select(struct io_kiocb *req, size_t *len,
			      unsigned int issue_flags);
void __io_kbuf_recycle(struct io_kiocb *req, unsigned issue_flags);
void io_destroy_buffers(struct io_ring_ctx *ctx);

int io_remove_buffers_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_remove_buffers(struct io_kiocb *req, unsigned int issue_flags);

int io_provide_buffers_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_provide_buffers(struct io_kiocb *req, unsigned int issue_flags);

int io_register_pbuf_ring(struct io_ring_ctx *ctx, void __user *arg);
int io_unregister_pbuf_ring(struct io_ring_ctx *ctx, void __user *arg);

unsigned int __io_put_kbuf(struct io_kiocb *req, unsigned issue_flags);

static inline bool io_do_buffer_select(struct io_kiocb *req)
{
	if (!(req->flags & REQ_F_BUFFER_SELECT))
		return false;
	return !(req->flags & (REQ_F_BUFFER_SELECTED|REQ_F_BUFFER_RING));
}

static inline void io_kbuf_recycle(struct io_kiocb *req, unsigned issue_flags)
{
	if (!(req->flags & (REQ_F_BUFFER_SELECTED|REQ_F_BUFFER_RING)))
		return;
	/*
	 * For legacy provided buffer mode, don't recycle if we already did
	 * IO to this buffer. For ring-mapped provided buffer mode, we should
	 * increment ring->head to explicitly monopolize the buffer to avoid
	 * multiple use.
	 */
	if ((req->flags & REQ_F_BUFFER_SELECTED) &&
	    (req->flags & REQ_F_PARTIAL_IO))
		return;

	/*
	 * READV uses fields in `struct io_rw` (len/addr) to stash the selected
	 * buffer data. However if that buffer is recycled the original request
	 * data stored in addr is lost. Therefore forbid recycling for now.
	 */
	if (req->opcode == IORING_OP_READV)
		return;

	__io_kbuf_recycle(req, issue_flags);
}

static inline unsigned int __io_put_kbuf_list(struct io_kiocb *req,
					      struct list_head *list)
{
	if (req->flags & REQ_F_BUFFER_RING) {
		if (req->buf_list)
			req->buf_list->head++;
		req->flags &= ~REQ_F_BUFFER_RING;
	} else {
		list_add(&req->kbuf->list, list);
		req->flags &= ~REQ_F_BUFFER_SELECTED;
	}

	return IORING_CQE_F_BUFFER | (req->buf_index << IORING_CQE_BUFFER_SHIFT);
}

static inline unsigned int io_put_kbuf_comp(struct io_kiocb *req)
{
	lockdep_assert_held(&req->ctx->completion_lock);

	if (!(req->flags & (REQ_F_BUFFER_SELECTED|REQ_F_BUFFER_RING)))
		return 0;
	return __io_put_kbuf_list(req, &req->ctx->io_buffers_comp);
}

static inline unsigned int io_put_kbuf(struct io_kiocb *req,
				       unsigned issue_flags)
{

	if (!(req->flags & (REQ_F_BUFFER_SELECTED|REQ_F_BUFFER_RING)))
		return 0;
	return __io_put_kbuf(req, issue_flags);
}
#endif
