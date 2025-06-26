// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "opdef.h"
#include "kbuf.h"
#include "memmap.h"

/* BIDs are addressed by a 16-bit field in a CQE */
#define MAX_BIDS_PER_BGID (1 << 16)

/* Mapped buffer ring, return io_uring_buf from head */
#define io_ring_head_to_buf(br, head, mask)	&(br)->bufs[(head) & (mask)]

struct io_provide_buf {
	struct file			*file;
	__u64				addr;
	__u32				len;
	__u32				bgid;
	__u32				nbufs;
	__u16				bid;
};

static bool io_kbuf_inc_commit(struct io_buffer_list *bl, int len)
{
	while (len) {
		struct io_uring_buf *buf;
		u32 this_len;

		buf = io_ring_head_to_buf(bl->buf_ring, bl->head, bl->mask);
		this_len = min_t(int, len, buf->len);
		buf->len -= this_len;
		if (buf->len) {
			buf->addr += this_len;
			return false;
		}
		bl->head++;
		len -= this_len;
	}
	return true;
}

bool io_kbuf_commit(struct io_kiocb *req,
		    struct io_buffer_list *bl, int len, int nr)
{
	if (unlikely(!(req->flags & REQ_F_BUFFERS_COMMIT)))
		return true;

	req->flags &= ~REQ_F_BUFFERS_COMMIT;

	if (unlikely(len < 0))
		return true;
	if (bl->flags & IOBL_INC)
		return io_kbuf_inc_commit(bl, len);
	bl->head += nr;
	return true;
}

static inline struct io_buffer_list *io_buffer_get_list(struct io_ring_ctx *ctx,
							unsigned int bgid)
{
	lockdep_assert_held(&ctx->uring_lock);

	return xa_load(&ctx->io_bl_xa, bgid);
}

static int io_buffer_add_list(struct io_ring_ctx *ctx,
			      struct io_buffer_list *bl, unsigned int bgid)
{
	/*
	 * Store buffer group ID and finally mark the list as visible.
	 * The normal lookup doesn't care about the visibility as we're
	 * always under the ->uring_lock, but lookups from mmap do.
	 */
	bl->bgid = bgid;
	guard(mutex)(&ctx->mmap_lock);
	return xa_err(xa_store(&ctx->io_bl_xa, bgid, bl, GFP_KERNEL));
}

void io_kbuf_drop_legacy(struct io_kiocb *req)
{
	if (WARN_ON_ONCE(!(req->flags & REQ_F_BUFFER_SELECTED)))
		return;
	req->flags &= ~REQ_F_BUFFER_SELECTED;
	kfree(req->kbuf);
	req->kbuf = NULL;
}

bool io_kbuf_recycle_legacy(struct io_kiocb *req, unsigned issue_flags)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_buffer_list *bl;
	struct io_buffer *buf;

	io_ring_submit_lock(ctx, issue_flags);

	buf = req->kbuf;
	bl = io_buffer_get_list(ctx, buf->bgid);
	list_add(&buf->list, &bl->buf_list);
	bl->nbufs++;
	req->flags &= ~REQ_F_BUFFER_SELECTED;

	io_ring_submit_unlock(ctx, issue_flags);
	return true;
}

static void __user *io_provided_buffer_select(struct io_kiocb *req, size_t *len,
					      struct io_buffer_list *bl)
{
	if (!list_empty(&bl->buf_list)) {
		struct io_buffer *kbuf;

		kbuf = list_first_entry(&bl->buf_list, struct io_buffer, list);
		list_del(&kbuf->list);
		bl->nbufs--;
		if (*len == 0 || *len > kbuf->len)
			*len = kbuf->len;
		if (list_empty(&bl->buf_list))
			req->flags |= REQ_F_BL_EMPTY;
		req->flags |= REQ_F_BUFFER_SELECTED;
		req->kbuf = kbuf;
		req->buf_index = kbuf->bid;
		return u64_to_user_ptr(kbuf->addr);
	}
	return NULL;
}

static int io_provided_buffers_select(struct io_kiocb *req, size_t *len,
				      struct io_buffer_list *bl,
				      struct iovec *iov)
{
	void __user *buf;

	buf = io_provided_buffer_select(req, len, bl);
	if (unlikely(!buf))
		return -ENOBUFS;

	iov[0].iov_base = buf;
	iov[0].iov_len = *len;
	return 1;
}

static void __user *io_ring_buffer_select(struct io_kiocb *req, size_t *len,
					  struct io_buffer_list *bl,
					  unsigned int issue_flags)
{
	struct io_uring_buf_ring *br = bl->buf_ring;
	__u16 tail, head = bl->head;
	struct io_uring_buf *buf;
	void __user *ret;

	tail = smp_load_acquire(&br->tail);
	if (unlikely(tail == head))
		return NULL;

	if (head + 1 == tail)
		req->flags |= REQ_F_BL_EMPTY;

	buf = io_ring_head_to_buf(br, head, bl->mask);
	if (*len == 0 || *len > buf->len)
		*len = buf->len;
	req->flags |= REQ_F_BUFFER_RING | REQ_F_BUFFERS_COMMIT;
	req->buf_list = bl;
	req->buf_index = buf->bid;
	ret = u64_to_user_ptr(buf->addr);

	if (issue_flags & IO_URING_F_UNLOCKED || !io_file_can_poll(req)) {
		/*
		 * If we came in unlocked, we have no choice but to consume the
		 * buffer here, otherwise nothing ensures that the buffer won't
		 * get used by others. This does mean it'll be pinned until the
		 * IO completes, coming in unlocked means we're being called from
		 * io-wq context and there may be further retries in async hybrid
		 * mode. For the locked case, the caller must call commit when
		 * the transfer completes (or if we get -EAGAIN and must poll of
		 * retry).
		 */
		io_kbuf_commit(req, bl, *len, 1);
		req->buf_list = NULL;
	}
	return ret;
}

void __user *io_buffer_select(struct io_kiocb *req, size_t *len,
			      unsigned buf_group, unsigned int issue_flags)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_buffer_list *bl;
	void __user *ret = NULL;

	io_ring_submit_lock(req->ctx, issue_flags);

	bl = io_buffer_get_list(ctx, buf_group);
	if (likely(bl)) {
		if (bl->flags & IOBL_BUF_RING)
			ret = io_ring_buffer_select(req, len, bl, issue_flags);
		else
			ret = io_provided_buffer_select(req, len, bl);
	}
	io_ring_submit_unlock(req->ctx, issue_flags);
	return ret;
}

/* cap it at a reasonable 256, will be one page even for 4K */
#define PEEK_MAX_IMPORT		256

static int io_ring_buffers_peek(struct io_kiocb *req, struct buf_sel_arg *arg,
				struct io_buffer_list *bl)
{
	struct io_uring_buf_ring *br = bl->buf_ring;
	struct iovec *iov = arg->iovs;
	int nr_iovs = arg->nr_iovs;
	__u16 nr_avail, tail, head;
	struct io_uring_buf *buf;

	tail = smp_load_acquire(&br->tail);
	head = bl->head;
	nr_avail = min_t(__u16, tail - head, UIO_MAXIOV);
	if (unlikely(!nr_avail))
		return -ENOBUFS;

	buf = io_ring_head_to_buf(br, head, bl->mask);
	if (arg->max_len) {
		u32 len = READ_ONCE(buf->len);
		size_t needed;

		if (unlikely(!len))
			return -ENOBUFS;
		needed = (arg->max_len + len - 1) / len;
		needed = min_not_zero(needed, (size_t) PEEK_MAX_IMPORT);
		if (nr_avail > needed)
			nr_avail = needed;
	}

	/*
	 * only alloc a bigger array if we know we have data to map, eg not
	 * a speculative peek operation.
	 */
	if (arg->mode & KBUF_MODE_EXPAND && nr_avail > nr_iovs && arg->max_len) {
		iov = kmalloc_array(nr_avail, sizeof(struct iovec), GFP_KERNEL);
		if (unlikely(!iov))
			return -ENOMEM;
		if (arg->mode & KBUF_MODE_FREE)
			kfree(arg->iovs);
		arg->iovs = iov;
		nr_iovs = nr_avail;
	} else if (nr_avail < nr_iovs) {
		nr_iovs = nr_avail;
	}

	/* set it to max, if not set, so we can use it unconditionally */
	if (!arg->max_len)
		arg->max_len = INT_MAX;

	req->buf_index = buf->bid;
	do {
		u32 len = buf->len;

		/* truncate end piece, if needed, for non partial buffers */
		if (len > arg->max_len) {
			len = arg->max_len;
			if (!(bl->flags & IOBL_INC)) {
				if (iov != arg->iovs)
					break;
				buf->len = len;
			}
		}

		iov->iov_base = u64_to_user_ptr(buf->addr);
		iov->iov_len = len;
		iov++;

		arg->out_len += len;
		arg->max_len -= len;
		if (!arg->max_len)
			break;

		buf = io_ring_head_to_buf(br, ++head, bl->mask);
	} while (--nr_iovs);

	if (head == tail)
		req->flags |= REQ_F_BL_EMPTY;

	req->flags |= REQ_F_BUFFER_RING;
	req->buf_list = bl;
	return iov - arg->iovs;
}

int io_buffers_select(struct io_kiocb *req, struct buf_sel_arg *arg,
		      unsigned int issue_flags)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_buffer_list *bl;
	int ret = -ENOENT;

	io_ring_submit_lock(ctx, issue_flags);
	bl = io_buffer_get_list(ctx, arg->buf_group);
	if (unlikely(!bl))
		goto out_unlock;

	if (bl->flags & IOBL_BUF_RING) {
		ret = io_ring_buffers_peek(req, arg, bl);
		/*
		 * Don't recycle these buffers if we need to go through poll.
		 * Nobody else can use them anyway, and holding on to provided
		 * buffers for a send/write operation would happen on the app
		 * side anyway with normal buffers. Besides, we already
		 * committed them, they cannot be put back in the queue.
		 */
		if (ret > 0) {
			req->flags |= REQ_F_BUFFERS_COMMIT | REQ_F_BL_NO_RECYCLE;
			io_kbuf_commit(req, bl, arg->out_len, ret);
		}
	} else {
		ret = io_provided_buffers_select(req, &arg->out_len, bl, arg->iovs);
	}
out_unlock:
	io_ring_submit_unlock(ctx, issue_flags);
	return ret;
}

int io_buffers_peek(struct io_kiocb *req, struct buf_sel_arg *arg)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_buffer_list *bl;
	int ret;

	lockdep_assert_held(&ctx->uring_lock);

	bl = io_buffer_get_list(ctx, arg->buf_group);
	if (unlikely(!bl))
		return -ENOENT;

	if (bl->flags & IOBL_BUF_RING) {
		ret = io_ring_buffers_peek(req, arg, bl);
		if (ret > 0)
			req->flags |= REQ_F_BUFFERS_COMMIT;
		return ret;
	}

	/* don't support multiple buffer selections for legacy */
	return io_provided_buffers_select(req, &arg->max_len, bl, arg->iovs);
}

static inline bool __io_put_kbuf_ring(struct io_kiocb *req, int len, int nr)
{
	struct io_buffer_list *bl = req->buf_list;
	bool ret = true;

	if (bl)
		ret = io_kbuf_commit(req, bl, len, nr);

	req->flags &= ~REQ_F_BUFFER_RING;
	return ret;
}

unsigned int __io_put_kbufs(struct io_kiocb *req, int len, int nbufs)
{
	unsigned int ret;

	ret = IORING_CQE_F_BUFFER | (req->buf_index << IORING_CQE_BUFFER_SHIFT);

	if (unlikely(!(req->flags & REQ_F_BUFFER_RING))) {
		io_kbuf_drop_legacy(req);
		return ret;
	}

	if (!__io_put_kbuf_ring(req, len, nbufs))
		ret |= IORING_CQE_F_BUF_MORE;
	return ret;
}

static int io_remove_buffers_legacy(struct io_ring_ctx *ctx,
				    struct io_buffer_list *bl,
				    unsigned long nbufs)
{
	unsigned long i = 0;
	struct io_buffer *nxt;

	/* protects io_buffers_cache */
	lockdep_assert_held(&ctx->uring_lock);
	WARN_ON_ONCE(bl->flags & IOBL_BUF_RING);

	for (i = 0; i < nbufs && !list_empty(&bl->buf_list); i++) {
		nxt = list_first_entry(&bl->buf_list, struct io_buffer, list);
		list_del(&nxt->list);
		bl->nbufs--;
		kfree(nxt);
		cond_resched();
	}
	return i;
}

static void io_put_bl(struct io_ring_ctx *ctx, struct io_buffer_list *bl)
{
	if (bl->flags & IOBL_BUF_RING)
		io_free_region(ctx, &bl->region);
	else
		io_remove_buffers_legacy(ctx, bl, -1U);

	kfree(bl);
}

void io_destroy_buffers(struct io_ring_ctx *ctx)
{
	struct io_buffer_list *bl;

	while (1) {
		unsigned long index = 0;

		scoped_guard(mutex, &ctx->mmap_lock) {
			bl = xa_find(&ctx->io_bl_xa, &index, ULONG_MAX, XA_PRESENT);
			if (bl)
				xa_erase(&ctx->io_bl_xa, bl->bgid);
		}
		if (!bl)
			break;
		io_put_bl(ctx, bl);
	}
}

static void io_destroy_bl(struct io_ring_ctx *ctx, struct io_buffer_list *bl)
{
	scoped_guard(mutex, &ctx->mmap_lock)
		WARN_ON_ONCE(xa_erase(&ctx->io_bl_xa, bl->bgid) != bl);
	io_put_bl(ctx, bl);
}

int io_remove_buffers_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_provide_buf *p = io_kiocb_to_cmd(req, struct io_provide_buf);
	u64 tmp;

	if (sqe->rw_flags || sqe->addr || sqe->len || sqe->off ||
	    sqe->splice_fd_in)
		return -EINVAL;

	tmp = READ_ONCE(sqe->fd);
	if (!tmp || tmp > MAX_BIDS_PER_BGID)
		return -EINVAL;

	memset(p, 0, sizeof(*p));
	p->nbufs = tmp;
	p->bgid = READ_ONCE(sqe->buf_group);
	return 0;
}

int io_provide_buffers_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	unsigned long size, tmp_check;
	struct io_provide_buf *p = io_kiocb_to_cmd(req, struct io_provide_buf);
	u64 tmp;

	if (sqe->rw_flags || sqe->splice_fd_in)
		return -EINVAL;

	tmp = READ_ONCE(sqe->fd);
	if (!tmp || tmp > MAX_BIDS_PER_BGID)
		return -E2BIG;
	p->nbufs = tmp;
	p->addr = READ_ONCE(sqe->addr);
	p->len = READ_ONCE(sqe->len);
	if (!p->len)
		return -EINVAL;

	if (check_mul_overflow((unsigned long)p->len, (unsigned long)p->nbufs,
				&size))
		return -EOVERFLOW;
	if (check_add_overflow((unsigned long)p->addr, size, &tmp_check))
		return -EOVERFLOW;
	if (!access_ok(u64_to_user_ptr(p->addr), size))
		return -EFAULT;

	p->bgid = READ_ONCE(sqe->buf_group);
	tmp = READ_ONCE(sqe->off);
	if (tmp > USHRT_MAX)
		return -E2BIG;
	if (tmp + p->nbufs > MAX_BIDS_PER_BGID)
		return -EINVAL;
	p->bid = tmp;
	return 0;
}

static int io_add_buffers(struct io_ring_ctx *ctx, struct io_provide_buf *pbuf,
			  struct io_buffer_list *bl)
{
	struct io_buffer *buf;
	u64 addr = pbuf->addr;
	int ret = -ENOMEM, i, bid = pbuf->bid;

	for (i = 0; i < pbuf->nbufs; i++) {
		/*
		 * Nonsensical to have more than sizeof(bid) buffers in a
		 * buffer list, as the application then has no way of knowing
		 * which duplicate bid refers to what buffer.
		 */
		if (bl->nbufs == USHRT_MAX) {
			ret = -EOVERFLOW;
			break;
		}
		buf = kmalloc(sizeof(*buf), GFP_KERNEL_ACCOUNT);
		if (!buf)
			break;

		list_add_tail(&buf->list, &bl->buf_list);
		bl->nbufs++;
		buf->addr = addr;
		buf->len = min_t(__u32, pbuf->len, MAX_RW_COUNT);
		buf->bid = bid;
		buf->bgid = pbuf->bgid;
		addr += pbuf->len;
		bid++;
		cond_resched();
	}

	return i ? 0 : ret;
}

static int __io_manage_buffers_legacy(struct io_kiocb *req,
					struct io_buffer_list *bl)
{
	struct io_provide_buf *p = io_kiocb_to_cmd(req, struct io_provide_buf);
	int ret;

	if (!bl) {
		if (req->opcode != IORING_OP_PROVIDE_BUFFERS)
			return -ENOENT;
		bl = kzalloc(sizeof(*bl), GFP_KERNEL_ACCOUNT);
		if (!bl)
			return -ENOMEM;

		INIT_LIST_HEAD(&bl->buf_list);
		ret = io_buffer_add_list(req->ctx, bl, p->bgid);
		if (ret) {
			kfree(bl);
			return ret;
		}
	}
	/* can't use provide/remove buffers command on mapped buffers */
	if (bl->flags & IOBL_BUF_RING)
		return -EINVAL;
	if (req->opcode == IORING_OP_PROVIDE_BUFFERS)
		return io_add_buffers(req->ctx, p, bl);
	return io_remove_buffers_legacy(req->ctx, bl, p->nbufs);
}

int io_manage_buffers_legacy(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_provide_buf *p = io_kiocb_to_cmd(req, struct io_provide_buf);
	struct io_ring_ctx *ctx = req->ctx;
	struct io_buffer_list *bl;
	int ret;

	io_ring_submit_lock(ctx, issue_flags);
	bl = io_buffer_get_list(ctx, p->bgid);
	ret = __io_manage_buffers_legacy(req, bl);
	io_ring_submit_unlock(ctx, issue_flags);

	if (ret < 0)
		req_set_fail(req);
	io_req_set_res(req, ret, 0);
	return IOU_COMPLETE;
}

int io_register_pbuf_ring(struct io_ring_ctx *ctx, void __user *arg)
{
	struct io_uring_buf_reg reg;
	struct io_buffer_list *bl;
	struct io_uring_region_desc rd;
	struct io_uring_buf_ring *br;
	unsigned long mmap_offset;
	unsigned long ring_size;
	int ret;

	lockdep_assert_held(&ctx->uring_lock);

	if (copy_from_user(&reg, arg, sizeof(reg)))
		return -EFAULT;
	if (!mem_is_zero(reg.resv, sizeof(reg.resv)))
		return -EINVAL;
	if (reg.flags & ~(IOU_PBUF_RING_MMAP | IOU_PBUF_RING_INC))
		return -EINVAL;
	if (!is_power_of_2(reg.ring_entries))
		return -EINVAL;
	/* cannot disambiguate full vs empty due to head/tail size */
	if (reg.ring_entries >= 65536)
		return -EINVAL;

	bl = io_buffer_get_list(ctx, reg.bgid);
	if (bl) {
		/* if mapped buffer ring OR classic exists, don't allow */
		if (bl->flags & IOBL_BUF_RING || !list_empty(&bl->buf_list))
			return -EEXIST;
		io_destroy_bl(ctx, bl);
	}

	bl = kzalloc(sizeof(*bl), GFP_KERNEL_ACCOUNT);
	if (!bl)
		return -ENOMEM;

	mmap_offset = (unsigned long)reg.bgid << IORING_OFF_PBUF_SHIFT;
	ring_size = flex_array_size(br, bufs, reg.ring_entries);

	memset(&rd, 0, sizeof(rd));
	rd.size = PAGE_ALIGN(ring_size);
	if (!(reg.flags & IOU_PBUF_RING_MMAP)) {
		rd.user_addr = reg.ring_addr;
		rd.flags |= IORING_MEM_REGION_TYPE_USER;
	}
	ret = io_create_region_mmap_safe(ctx, &bl->region, &rd, mmap_offset);
	if (ret)
		goto fail;
	br = io_region_get_ptr(&bl->region);

#ifdef SHM_COLOUR
	/*
	 * On platforms that have specific aliasing requirements, SHM_COLOUR
	 * is set and we must guarantee that the kernel and user side align
	 * nicely. We cannot do that if IOU_PBUF_RING_MMAP isn't set and
	 * the application mmap's the provided ring buffer. Fail the request
	 * if we, by chance, don't end up with aligned addresses. The app
	 * should use IOU_PBUF_RING_MMAP instead, and liburing will handle
	 * this transparently.
	 */
	if (!(reg.flags & IOU_PBUF_RING_MMAP) &&
	    ((reg.ring_addr | (unsigned long)br) & (SHM_COLOUR - 1))) {
		ret = -EINVAL;
		goto fail;
	}
#endif

	bl->nr_entries = reg.ring_entries;
	bl->mask = reg.ring_entries - 1;
	bl->flags |= IOBL_BUF_RING;
	bl->buf_ring = br;
	if (reg.flags & IOU_PBUF_RING_INC)
		bl->flags |= IOBL_INC;
	io_buffer_add_list(ctx, bl, reg.bgid);
	return 0;
fail:
	io_free_region(ctx, &bl->region);
	kfree(bl);
	return ret;
}

int io_unregister_pbuf_ring(struct io_ring_ctx *ctx, void __user *arg)
{
	struct io_uring_buf_reg reg;
	struct io_buffer_list *bl;

	lockdep_assert_held(&ctx->uring_lock);

	if (copy_from_user(&reg, arg, sizeof(reg)))
		return -EFAULT;
	if (!mem_is_zero(reg.resv, sizeof(reg.resv)) || reg.flags)
		return -EINVAL;

	bl = io_buffer_get_list(ctx, reg.bgid);
	if (!bl)
		return -ENOENT;
	if (!(bl->flags & IOBL_BUF_RING))
		return -EINVAL;

	scoped_guard(mutex, &ctx->mmap_lock)
		xa_erase(&ctx->io_bl_xa, bl->bgid);

	io_put_bl(ctx, bl);
	return 0;
}

int io_register_pbuf_status(struct io_ring_ctx *ctx, void __user *arg)
{
	struct io_uring_buf_status buf_status;
	struct io_buffer_list *bl;

	if (copy_from_user(&buf_status, arg, sizeof(buf_status)))
		return -EFAULT;
	if (!mem_is_zero(buf_status.resv, sizeof(buf_status.resv)))
		return -EINVAL;

	bl = io_buffer_get_list(ctx, buf_status.buf_group);
	if (!bl)
		return -ENOENT;
	if (!(bl->flags & IOBL_BUF_RING))
		return -EINVAL;

	buf_status.head = bl->head;
	if (copy_to_user(arg, &buf_status, sizeof(buf_status)))
		return -EFAULT;

	return 0;
}

struct io_mapped_region *io_pbuf_get_region(struct io_ring_ctx *ctx,
					    unsigned int bgid)
{
	struct io_buffer_list *bl;

	lockdep_assert_held(&ctx->mmap_lock);

	bl = xa_load(&ctx->io_bl_xa, bgid);
	if (!bl || !(bl->flags & IOBL_BUF_RING))
		return NULL;
	return &bl->region;
}
