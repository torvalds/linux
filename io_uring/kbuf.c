// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/poll.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "opdef.h"
#include "kbuf.h"

#define IO_BUFFER_LIST_BUF_PER_PAGE (PAGE_SIZE / sizeof(struct io_uring_buf))

#define BGID_ARRAY	64

struct io_provide_buf {
	struct file			*file;
	__u64				addr;
	__u32				len;
	__u32				bgid;
	__u16				nbufs;
	__u16				bid;
};

static inline struct io_buffer_list *io_buffer_get_list(struct io_ring_ctx *ctx,
							unsigned int bgid)
{
	if (ctx->io_bl && bgid < BGID_ARRAY)
		return &ctx->io_bl[bgid];

	return xa_load(&ctx->io_bl_xa, bgid);
}

static int io_buffer_add_list(struct io_ring_ctx *ctx,
			      struct io_buffer_list *bl, unsigned int bgid)
{
	bl->bgid = bgid;
	if (bgid < BGID_ARRAY)
		return 0;

	return xa_err(xa_store(&ctx->io_bl_xa, bgid, bl, GFP_KERNEL));
}

void io_kbuf_recycle_legacy(struct io_kiocb *req, unsigned issue_flags)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_buffer_list *bl;
	struct io_buffer *buf;

	/*
	 * For legacy provided buffer mode, don't recycle if we already did
	 * IO to this buffer. For ring-mapped provided buffer mode, we should
	 * increment ring->head to explicitly monopolize the buffer to avoid
	 * multiple use.
	 */
	if (req->flags & REQ_F_PARTIAL_IO)
		return;

	io_ring_submit_lock(ctx, issue_flags);

	buf = req->kbuf;
	bl = io_buffer_get_list(ctx, buf->bgid);
	list_add(&buf->list, &bl->buf_list);
	req->flags &= ~REQ_F_BUFFER_SELECTED;
	req->buf_index = buf->bgid;

	io_ring_submit_unlock(ctx, issue_flags);
	return;
}

unsigned int __io_put_kbuf(struct io_kiocb *req, unsigned issue_flags)
{
	unsigned int cflags;

	/*
	 * We can add this buffer back to two lists:
	 *
	 * 1) The io_buffers_cache list. This one is protected by the
	 *    ctx->uring_lock. If we already hold this lock, add back to this
	 *    list as we can grab it from issue as well.
	 * 2) The io_buffers_comp list. This one is protected by the
	 *    ctx->completion_lock.
	 *
	 * We migrate buffers from the comp_list to the issue cache list
	 * when we need one.
	 */
	if (req->flags & REQ_F_BUFFER_RING) {
		/* no buffers to recycle for this case */
		cflags = __io_put_kbuf_list(req, NULL);
	} else if (issue_flags & IO_URING_F_UNLOCKED) {
		struct io_ring_ctx *ctx = req->ctx;

		spin_lock(&ctx->completion_lock);
		cflags = __io_put_kbuf_list(req, &ctx->io_buffers_comp);
		spin_unlock(&ctx->completion_lock);
	} else {
		lockdep_assert_held(&req->ctx->uring_lock);

		cflags = __io_put_kbuf_list(req, &req->ctx->io_buffers_cache);
	}
	return cflags;
}

static void __user *io_provided_buffer_select(struct io_kiocb *req, size_t *len,
					      struct io_buffer_list *bl)
{
	if (!list_empty(&bl->buf_list)) {
		struct io_buffer *kbuf;

		kbuf = list_first_entry(&bl->buf_list, struct io_buffer, list);
		list_del(&kbuf->list);
		if (*len == 0 || *len > kbuf->len)
			*len = kbuf->len;
		req->flags |= REQ_F_BUFFER_SELECTED;
		req->kbuf = kbuf;
		req->buf_index = kbuf->bid;
		return u64_to_user_ptr(kbuf->addr);
	}
	return NULL;
}

static void __user *io_ring_buffer_select(struct io_kiocb *req, size_t *len,
					  struct io_buffer_list *bl,
					  unsigned int issue_flags)
{
	struct io_uring_buf_ring *br = bl->buf_ring;
	struct io_uring_buf *buf;
	__u16 head = bl->head;

	if (unlikely(smp_load_acquire(&br->tail) == head))
		return NULL;

	head &= bl->mask;
	if (head < IO_BUFFER_LIST_BUF_PER_PAGE) {
		buf = &br->bufs[head];
	} else {
		int off = head & (IO_BUFFER_LIST_BUF_PER_PAGE - 1);
		int index = head / IO_BUFFER_LIST_BUF_PER_PAGE;
		buf = page_address(bl->buf_pages[index]);
		buf += off;
	}
	if (*len == 0 || *len > buf->len)
		*len = buf->len;
	req->flags |= REQ_F_BUFFER_RING;
	req->buf_list = bl;
	req->buf_index = buf->bid;

	if (issue_flags & IO_URING_F_UNLOCKED || !file_can_poll(req->file)) {
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
		req->buf_list = NULL;
		bl->head++;
	}
	return u64_to_user_ptr(buf->addr);
}

void __user *io_buffer_select(struct io_kiocb *req, size_t *len,
			      unsigned int issue_flags)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_buffer_list *bl;
	void __user *ret = NULL;

	io_ring_submit_lock(req->ctx, issue_flags);

	bl = io_buffer_get_list(ctx, req->buf_index);
	if (likely(bl)) {
		if (bl->buf_nr_pages)
			ret = io_ring_buffer_select(req, len, bl, issue_flags);
		else
			ret = io_provided_buffer_select(req, len, bl);
	}
	io_ring_submit_unlock(req->ctx, issue_flags);
	return ret;
}

static __cold int io_init_bl_list(struct io_ring_ctx *ctx)
{
	int i;

	ctx->io_bl = kcalloc(BGID_ARRAY, sizeof(struct io_buffer_list),
				GFP_KERNEL);
	if (!ctx->io_bl)
		return -ENOMEM;

	for (i = 0; i < BGID_ARRAY; i++) {
		INIT_LIST_HEAD(&ctx->io_bl[i].buf_list);
		ctx->io_bl[i].bgid = i;
	}

	return 0;
}

static int __io_remove_buffers(struct io_ring_ctx *ctx,
			       struct io_buffer_list *bl, unsigned nbufs)
{
	unsigned i = 0;

	/* shouldn't happen */
	if (!nbufs)
		return 0;

	if (bl->buf_nr_pages) {
		int j;

		i = bl->buf_ring->tail - bl->head;
		for (j = 0; j < bl->buf_nr_pages; j++)
			unpin_user_page(bl->buf_pages[j]);
		kvfree(bl->buf_pages);
		bl->buf_pages = NULL;
		bl->buf_nr_pages = 0;
		/* make sure it's seen as empty */
		INIT_LIST_HEAD(&bl->buf_list);
		return i;
	}

	/* protects io_buffers_cache */
	lockdep_assert_held(&ctx->uring_lock);

	while (!list_empty(&bl->buf_list)) {
		struct io_buffer *nxt;

		nxt = list_first_entry(&bl->buf_list, struct io_buffer, list);
		list_move(&nxt->list, &ctx->io_buffers_cache);
		if (++i == nbufs)
			return i;
		cond_resched();
	}

	return i;
}

void io_destroy_buffers(struct io_ring_ctx *ctx)
{
	struct io_buffer_list *bl;
	unsigned long index;
	int i;

	for (i = 0; i < BGID_ARRAY; i++) {
		if (!ctx->io_bl)
			break;
		__io_remove_buffers(ctx, &ctx->io_bl[i], -1U);
	}

	xa_for_each(&ctx->io_bl_xa, index, bl) {
		xa_erase(&ctx->io_bl_xa, bl->bgid);
		__io_remove_buffers(ctx, bl, -1U);
		kfree(bl);
	}

	while (!list_empty(&ctx->io_buffers_pages)) {
		struct page *page;

		page = list_first_entry(&ctx->io_buffers_pages, struct page, lru);
		list_del_init(&page->lru);
		__free_page(page);
	}
}

int io_remove_buffers_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_provide_buf *p = io_kiocb_to_cmd(req, struct io_provide_buf);
	u64 tmp;

	if (sqe->rw_flags || sqe->addr || sqe->len || sqe->off ||
	    sqe->splice_fd_in)
		return -EINVAL;

	tmp = READ_ONCE(sqe->fd);
	if (!tmp || tmp > USHRT_MAX)
		return -EINVAL;

	memset(p, 0, sizeof(*p));
	p->nbufs = tmp;
	p->bgid = READ_ONCE(sqe->buf_group);
	return 0;
}

int io_remove_buffers(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_provide_buf *p = io_kiocb_to_cmd(req, struct io_provide_buf);
	struct io_ring_ctx *ctx = req->ctx;
	struct io_buffer_list *bl;
	int ret = 0;

	io_ring_submit_lock(ctx, issue_flags);

	ret = -ENOENT;
	bl = io_buffer_get_list(ctx, p->bgid);
	if (bl) {
		ret = -EINVAL;
		/* can't use provide/remove buffers command on mapped buffers */
		if (!bl->buf_nr_pages)
			ret = __io_remove_buffers(ctx, bl, p->nbufs);
	}
	if (ret < 0)
		req_set_fail(req);

	/* complete before unlock, IOPOLL may need the lock */
	io_req_set_res(req, ret, 0);
	__io_req_complete(req, issue_flags);
	io_ring_submit_unlock(ctx, issue_flags);
	return IOU_ISSUE_SKIP_COMPLETE;
}

int io_provide_buffers_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	unsigned long size, tmp_check;
	struct io_provide_buf *p = io_kiocb_to_cmd(req, struct io_provide_buf);
	u64 tmp;

	if (sqe->rw_flags || sqe->splice_fd_in)
		return -EINVAL;

	tmp = READ_ONCE(sqe->fd);
	if (!tmp || tmp > USHRT_MAX)
		return -E2BIG;
	p->nbufs = tmp;
	p->addr = READ_ONCE(sqe->addr);
	p->len = READ_ONCE(sqe->len);

	if (check_mul_overflow((unsigned long)p->len, (unsigned long)p->nbufs,
				&size))
		return -EOVERFLOW;
	if (check_add_overflow((unsigned long)p->addr, size, &tmp_check))
		return -EOVERFLOW;

	size = (unsigned long)p->len * p->nbufs;
	if (!access_ok(u64_to_user_ptr(p->addr), size))
		return -EFAULT;

	p->bgid = READ_ONCE(sqe->buf_group);
	tmp = READ_ONCE(sqe->off);
	if (tmp > USHRT_MAX)
		return -E2BIG;
	if (tmp + p->nbufs >= USHRT_MAX)
		return -EINVAL;
	p->bid = tmp;
	return 0;
}

static int io_refill_buffer_cache(struct io_ring_ctx *ctx)
{
	struct io_buffer *buf;
	struct page *page;
	int bufs_in_page;

	/*
	 * Completions that don't happen inline (eg not under uring_lock) will
	 * add to ->io_buffers_comp. If we don't have any free buffers, check
	 * the completion list and splice those entries first.
	 */
	if (!list_empty_careful(&ctx->io_buffers_comp)) {
		spin_lock(&ctx->completion_lock);
		if (!list_empty(&ctx->io_buffers_comp)) {
			list_splice_init(&ctx->io_buffers_comp,
						&ctx->io_buffers_cache);
			spin_unlock(&ctx->completion_lock);
			return 0;
		}
		spin_unlock(&ctx->completion_lock);
	}

	/*
	 * No free buffers and no completion entries either. Allocate a new
	 * page worth of buffer entries and add those to our freelist.
	 */
	page = alloc_page(GFP_KERNEL_ACCOUNT);
	if (!page)
		return -ENOMEM;

	list_add(&page->lru, &ctx->io_buffers_pages);

	buf = page_address(page);
	bufs_in_page = PAGE_SIZE / sizeof(*buf);
	while (bufs_in_page) {
		list_add_tail(&buf->list, &ctx->io_buffers_cache);
		buf++;
		bufs_in_page--;
	}

	return 0;
}

static int io_add_buffers(struct io_ring_ctx *ctx, struct io_provide_buf *pbuf,
			  struct io_buffer_list *bl)
{
	struct io_buffer *buf;
	u64 addr = pbuf->addr;
	int i, bid = pbuf->bid;

	for (i = 0; i < pbuf->nbufs; i++) {
		if (list_empty(&ctx->io_buffers_cache) &&
		    io_refill_buffer_cache(ctx))
			break;
		buf = list_first_entry(&ctx->io_buffers_cache, struct io_buffer,
					list);
		list_move_tail(&buf->list, &bl->buf_list);
		buf->addr = addr;
		buf->len = min_t(__u32, pbuf->len, MAX_RW_COUNT);
		buf->bid = bid;
		buf->bgid = pbuf->bgid;
		addr += pbuf->len;
		bid++;
		cond_resched();
	}

	return i ? 0 : -ENOMEM;
}

int io_provide_buffers(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_provide_buf *p = io_kiocb_to_cmd(req, struct io_provide_buf);
	struct io_ring_ctx *ctx = req->ctx;
	struct io_buffer_list *bl;
	int ret = 0;

	io_ring_submit_lock(ctx, issue_flags);

	if (unlikely(p->bgid < BGID_ARRAY && !ctx->io_bl)) {
		ret = io_init_bl_list(ctx);
		if (ret)
			goto err;
	}

	bl = io_buffer_get_list(ctx, p->bgid);
	if (unlikely(!bl)) {
		bl = kzalloc(sizeof(*bl), GFP_KERNEL_ACCOUNT);
		if (!bl) {
			ret = -ENOMEM;
			goto err;
		}
		INIT_LIST_HEAD(&bl->buf_list);
		ret = io_buffer_add_list(ctx, bl, p->bgid);
		if (ret) {
			kfree(bl);
			goto err;
		}
	}
	/* can't add buffers via this command for a mapped buffer ring */
	if (bl->buf_nr_pages) {
		ret = -EINVAL;
		goto err;
	}

	ret = io_add_buffers(ctx, p, bl);
err:
	if (ret < 0)
		req_set_fail(req);
	/* complete before unlock, IOPOLL may need the lock */
	io_req_set_res(req, ret, 0);
	__io_req_complete(req, issue_flags);
	io_ring_submit_unlock(ctx, issue_flags);
	return IOU_ISSUE_SKIP_COMPLETE;
}

int io_register_pbuf_ring(struct io_ring_ctx *ctx, void __user *arg)
{
	struct io_uring_buf_ring *br;
	struct io_uring_buf_reg reg;
	struct io_buffer_list *bl, *free_bl = NULL;
	struct page **pages;
	int nr_pages;

	if (copy_from_user(&reg, arg, sizeof(reg)))
		return -EFAULT;

	if (reg.pad || reg.resv[0] || reg.resv[1] || reg.resv[2])
		return -EINVAL;
	if (!reg.ring_addr)
		return -EFAULT;
	if (reg.ring_addr & ~PAGE_MASK)
		return -EINVAL;
	if (!is_power_of_2(reg.ring_entries))
		return -EINVAL;

	/* cannot disambiguate full vs empty due to head/tail size */
	if (reg.ring_entries >= 65536)
		return -EINVAL;

	if (unlikely(reg.bgid < BGID_ARRAY && !ctx->io_bl)) {
		int ret = io_init_bl_list(ctx);
		if (ret)
			return ret;
	}

	bl = io_buffer_get_list(ctx, reg.bgid);
	if (bl) {
		/* if mapped buffer ring OR classic exists, don't allow */
		if (bl->buf_nr_pages || !list_empty(&bl->buf_list))
			return -EEXIST;
	} else {
		free_bl = bl = kzalloc(sizeof(*bl), GFP_KERNEL);
		if (!bl)
			return -ENOMEM;
	}

	pages = io_pin_pages(reg.ring_addr,
			     flex_array_size(br, bufs, reg.ring_entries),
			     &nr_pages);
	if (IS_ERR(pages)) {
		kfree(free_bl);
		return PTR_ERR(pages);
	}

	br = page_address(pages[0]);
	bl->buf_pages = pages;
	bl->buf_nr_pages = nr_pages;
	bl->nr_entries = reg.ring_entries;
	bl->buf_ring = br;
	bl->mask = reg.ring_entries - 1;
	io_buffer_add_list(ctx, bl, reg.bgid);
	return 0;
}

int io_unregister_pbuf_ring(struct io_ring_ctx *ctx, void __user *arg)
{
	struct io_uring_buf_reg reg;
	struct io_buffer_list *bl;

	if (copy_from_user(&reg, arg, sizeof(reg)))
		return -EFAULT;
	if (reg.pad || reg.resv[0] || reg.resv[1] || reg.resv[2])
		return -EINVAL;

	bl = io_buffer_get_list(ctx, reg.bgid);
	if (!bl)
		return -ENOENT;
	if (!bl->buf_nr_pages)
		return -EINVAL;

	__io_remove_buffers(ctx, bl, -1U);
	if (bl->bgid >= BGID_ARRAY) {
		xa_erase(&ctx->io_bl_xa, bl->bgid);
		kfree(bl);
	}
	return 0;
}
