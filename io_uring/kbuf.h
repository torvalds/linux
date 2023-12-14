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

	/* ring mapped provided buffers */
	__u8 is_mapped;
	/* ring mapped provided buffers, but mmap'ed by application */
	__u8 is_mmap;
	/* bl is visible from an RCU point of view for lookup */
	__u8 is_ready;
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
void io_destroy_buffers(struct io_ring_ctx *ctx);

int io_remove_buffers_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_remove_buffers(struct io_kiocb *req, unsigned int issue_flags);

int io_provide_buffers_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_provide_buffers(struct io_kiocb *req, unsigned int issue_flags);

int io_register_pbuf_ring(struct io_ring_ctx *ctx, void __user *arg);
int io_unregister_pbuf_ring(struct io_ring_ctx *ctx, void __user *arg);

void io_kbuf_mmap_list_free(struct io_ring_ctx *ctx);

unsigned int __io_put_kbuf(struct io_kiocb *req, unsigned issue_flags);

void io_kbuf_recycle_legacy(struct io_kiocb *req, unsigned issue_flags);

void *io_pbuf_get_address(struct io_ring_ctx *ctx, unsigned long bgid);

static inline void io_kbuf_recycle_ring(struct io_kiocb *req)
{
	/*
	 * We don't need to recycle for REQ_F_BUFFER_RING, we can just clear
	 * the flag and hence ensure that bl->head doesn't get incremented.
	 * If the tail has already been incremented, hang on to it.
	 * The exception is partial io, that case we should increment bl->head
	 * to monopolize the buffer.
	 */
	if (req->buf_list) {
		if (req->flags & REQ_F_PARTIAL_IO) {
			/*
			 * If we end up here, then the io_uring_lock has
			 * been kept held since we retrieved the buffer.
			 * For the io-wq case, we already cleared
			 * req->buf_list when the buffer was retrieved,
			 * hence it cannot be set here for that case.
			 */
			req->buf_list->head++;
			req->buf_list = NULL;
		} else {
			req->buf_index = req->buf_list->bgid;
			req->flags &= ~REQ_F_BUFFER_RING;
		}
	}
}

static inline bool io_do_buffer_select(struct io_kiocb *req)
{
	if (!(req->flags & REQ_F_BUFFER_SELECT))
		return false;
	return !(req->flags & (REQ_F_BUFFER_SELECTED|REQ_F_BUFFER_RING));
}

static inline void io_kbuf_recycle(struct io_kiocb *req, unsigned issue_flags)
{
	if (req->flags & REQ_F_BUFFER_SELECTED)
		io_kbuf_recycle_legacy(req, issue_flags);
	if (req->flags & REQ_F_BUFFER_RING)
		io_kbuf_recycle_ring(req);
}

static inline unsigned int __io_put_kbuf_list(struct io_kiocb *req,
					      struct list_head *list)
{
	unsigned int ret = IORING_CQE_F_BUFFER | (req->buf_index << IORING_CQE_BUFFER_SHIFT);

	if (req->flags & REQ_F_BUFFER_RING) {
		if (req->buf_list) {
			req->buf_index = req->buf_list->bgid;
			req->buf_list->head++;
		}
		req->flags &= ~REQ_F_BUFFER_RING;
	} else {
		req->buf_index = req->kbuf->bgid;
		list_add(&req->kbuf->list, list);
		req->flags &= ~REQ_F_BUFFER_SELECTED;
	}

	return ret;
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
