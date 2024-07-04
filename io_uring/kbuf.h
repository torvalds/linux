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
		struct rcu_head rcu;
	};
	__u16 bgid;

	/* below is for ring provided buffers */
	__u16 buf_nr_pages;
	__u16 nr_entries;
	__u16 head;
	__u16 mask;

	atomic_t refs;

	/* ring mapped provided buffers */
	__u8 is_buf_ring;
	/* ring mapped provided buffers, but mmap'ed by application */
	__u8 is_mmap;
};

struct io_buffer {
	struct list_head list;
	__u64 addr;
	__u32 len;
	__u16 bid;
	__u16 bgid;
};

enum {
	/* can alloc a bigger vec */
	KBUF_MODE_EXPAND	= 1,
	/* if bigger vec allocated, free old one */
	KBUF_MODE_FREE		= 2,
};

struct buf_sel_arg {
	struct iovec *iovs;
	size_t out_len;
	size_t max_len;
	int nr_iovs;
	int mode;
};

void __user *io_buffer_select(struct io_kiocb *req, size_t *len,
			      unsigned int issue_flags);
int io_buffers_select(struct io_kiocb *req, struct buf_sel_arg *arg,
		      unsigned int issue_flags);
int io_buffers_peek(struct io_kiocb *req, struct buf_sel_arg *arg);
void io_destroy_buffers(struct io_ring_ctx *ctx);

int io_remove_buffers_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_remove_buffers(struct io_kiocb *req, unsigned int issue_flags);

int io_provide_buffers_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_provide_buffers(struct io_kiocb *req, unsigned int issue_flags);

int io_register_pbuf_ring(struct io_ring_ctx *ctx, void __user *arg);
int io_unregister_pbuf_ring(struct io_ring_ctx *ctx, void __user *arg);
int io_register_pbuf_status(struct io_ring_ctx *ctx, void __user *arg);

void __io_put_kbuf(struct io_kiocb *req, unsigned issue_flags);

bool io_kbuf_recycle_legacy(struct io_kiocb *req, unsigned issue_flags);

void io_put_bl(struct io_ring_ctx *ctx, struct io_buffer_list *bl);
struct io_buffer_list *io_pbuf_get_bl(struct io_ring_ctx *ctx,
				      unsigned long bgid);
int io_pbuf_mmap(struct file *file, struct vm_area_struct *vma);

static inline bool io_kbuf_recycle_ring(struct io_kiocb *req)
{
	/*
	 * We don't need to recycle for REQ_F_BUFFER_RING, we can just clear
	 * the flag and hence ensure that bl->head doesn't get incremented.
	 * If the tail has already been incremented, hang on to it.
	 * The exception is partial io, that case we should increment bl->head
	 * to monopolize the buffer.
	 */
	if (req->buf_list) {
		req->buf_index = req->buf_list->bgid;
		req->flags &= ~(REQ_F_BUFFER_RING|REQ_F_BUFFERS_COMMIT);
		return true;
	}
	return false;
}

static inline bool io_do_buffer_select(struct io_kiocb *req)
{
	if (!(req->flags & REQ_F_BUFFER_SELECT))
		return false;
	return !(req->flags & (REQ_F_BUFFER_SELECTED|REQ_F_BUFFER_RING));
}

static inline bool io_kbuf_recycle(struct io_kiocb *req, unsigned issue_flags)
{
	if (req->flags & REQ_F_BL_NO_RECYCLE)
		return false;
	if (req->flags & REQ_F_BUFFER_SELECTED)
		return io_kbuf_recycle_legacy(req, issue_flags);
	if (req->flags & REQ_F_BUFFER_RING)
		return io_kbuf_recycle_ring(req);
	return false;
}

static inline void __io_put_kbuf_ring(struct io_kiocb *req, int nr)
{
	struct io_buffer_list *bl = req->buf_list;

	if (bl) {
		if (req->flags & REQ_F_BUFFERS_COMMIT) {
			bl->head += nr;
			req->flags &= ~REQ_F_BUFFERS_COMMIT;
		}
		req->buf_index = bl->bgid;
	}
	req->flags &= ~REQ_F_BUFFER_RING;
}

static inline void __io_put_kbuf_list(struct io_kiocb *req,
				      struct list_head *list)
{
	if (req->flags & REQ_F_BUFFER_RING) {
		__io_put_kbuf_ring(req, 1);
	} else {
		req->buf_index = req->kbuf->bgid;
		list_add(&req->kbuf->list, list);
		req->flags &= ~REQ_F_BUFFER_SELECTED;
	}
}

static inline void io_kbuf_drop(struct io_kiocb *req)
{
	lockdep_assert_held(&req->ctx->completion_lock);

	if (!(req->flags & (REQ_F_BUFFER_SELECTED|REQ_F_BUFFER_RING)))
		return;

	__io_put_kbuf_list(req, &req->ctx->io_buffers_comp);
}

static inline unsigned int __io_put_kbufs(struct io_kiocb *req, int nbufs,
					  unsigned issue_flags)
{
	unsigned int ret;

	if (!(req->flags & (REQ_F_BUFFER_RING | REQ_F_BUFFER_SELECTED)))
		return 0;

	ret = IORING_CQE_F_BUFFER | (req->buf_index << IORING_CQE_BUFFER_SHIFT);
	if (req->flags & REQ_F_BUFFER_RING)
		__io_put_kbuf_ring(req, nbufs);
	else
		__io_put_kbuf(req, issue_flags);
	return ret;
}

static inline unsigned int io_put_kbuf(struct io_kiocb *req,
				       unsigned issue_flags)
{
	return __io_put_kbufs(req, 1, issue_flags);
}

static inline unsigned int io_put_kbufs(struct io_kiocb *req, int nbufs,
					unsigned issue_flags)
{
	return __io_put_kbufs(req, nbufs, issue_flags);
}
#endif
