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

struct kmem_cache *io_buf_cachep;

struct io_provide_buf {
	struct file			*file;
	__u64				addr;
	__u32				len;
	__u32				bgid;
	__u32				nbufs;
	__u16				bid;
};

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
	 * always under the ->uring_lock, but the RCU lookup from mmap does.
	 */
	bl->bgid = bgid;
	atomic_set(&bl->refs, 1);
	return xa_err(xa_store(&ctx->io_bl_xa, bgid, bl, GFP_KERNEL));
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
	req->flags &= ~REQ_F_BUFFER_SELECTED;
	req->buf_index = buf->bgid;

	io_ring_submit_unlock(ctx, issue_flags);
	return true;
}

void __io_put_kbuf(struct io_kiocb *req, unsigned issue_flags)
{
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
	if (issue_flags & IO_URING_F_UNLOCKED) {
		struct io_ring_ctx *ctx = req->ctx;

		spin_lock(&ctx->completion_lock);
		__io_put_kbuf_list(req, &ctx->io_buffers_comp);
		spin_unlock(&ctx->completion_lock);
	} else {
		lockdep_assert_held(&req->ctx->uring_lock);

		__io_put_kbuf_list(req, &req->ctx->io_buffers_cache);
	}
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
	return 0;
}

static struct io_uring_buf *io_ring_head_to_buf(struct io_uring_buf_ring *br,
						__u16 head, __u16 mask)
{
	return &br->bufs[head & mask];
}

static void __user *io_ring_buffer_select(struct io_kiocb *req, size_t *len,
					  struct io_buffer_list *bl,
					  unsigned int issue_flags)
{
	struct io_uring_buf_ring *br = bl->buf_ring;
	__u16 tail, head = bl->head;
	struct io_uring_buf *buf;

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
		req->flags &= ~REQ_F_BUFFERS_COMMIT;
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
		if (bl->is_buf_ring)
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
		int needed;

		needed = (arg->max_len + buf->len - 1) / buf->len;
		needed = min(needed, PEEK_MAX_IMPORT);
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
		/* truncate end piece, if needed */
		if (buf->len > arg->max_len)
			buf->len = arg->max_len;

		iov->iov_base = u64_to_user_ptr(buf->addr);
		iov->iov_len = buf->len;
		iov++;

		arg->out_len += buf->len;
		arg->max_len -= buf->len;
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
	bl = io_buffer_get_list(ctx, req->buf_index);
	if (unlikely(!bl))
		goto out_unlock;

	if (bl->is_buf_ring) {
		ret = io_ring_buffers_peek(req, arg, bl);
		/*
		 * Don't recycle these buffers if we need to go through poll.
		 * Nobody else can use them anyway, and holding on to provided
		 * buffers for a send/write operation would happen on the app
		 * side anyway with normal buffers. Besides, we already
		 * committed them, they cannot be put back in the queue.
		 */
		if (ret > 0) {
			req->flags |= REQ_F_BL_NO_RECYCLE;
			req->buf_list->head += ret;
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

	bl = io_buffer_get_list(ctx, req->buf_index);
	if (unlikely(!bl))
		return -ENOENT;

	if (bl->is_buf_ring) {
		ret = io_ring_buffers_peek(req, arg, bl);
		if (ret > 0)
			req->flags |= REQ_F_BUFFERS_COMMIT;
		return ret;
	}

	/* don't support multiple buffer selections for legacy */
	return io_provided_buffers_select(req, &arg->max_len, bl, arg->iovs);
}

static int __io_remove_buffers(struct io_ring_ctx *ctx,
			       struct io_buffer_list *bl, unsigned nbufs)
{
	unsigned i = 0;

	/* shouldn't happen */
	if (!nbufs)
		return 0;

	if (bl->is_buf_ring) {
		i = bl->buf_ring->tail - bl->head;
		if (bl->buf_nr_pages) {
			int j;

			if (!bl->is_mmap) {
				for (j = 0; j < bl->buf_nr_pages; j++)
					unpin_user_page(bl->buf_pages[j]);
			}
			io_pages_unmap(bl->buf_ring, &bl->buf_pages,
					&bl->buf_nr_pages, bl->is_mmap);
			bl->is_mmap = 0;
		}
		/* make sure it's seen as empty */
		INIT_LIST_HEAD(&bl->buf_list);
		bl->is_buf_ring = 0;
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

void io_put_bl(struct io_ring_ctx *ctx, struct io_buffer_list *bl)
{
	if (atomic_dec_and_test(&bl->refs)) {
		__io_remove_buffers(ctx, bl, -1U);
		kfree_rcu(bl, rcu);
	}
}

void io_destroy_buffers(struct io_ring_ctx *ctx)
{
	struct io_buffer_list *bl;
	struct list_head *item, *tmp;
	struct io_buffer *buf;
	unsigned long index;

	xa_for_each(&ctx->io_bl_xa, index, bl) {
		xa_erase(&ctx->io_bl_xa, bl->bgid);
		io_put_bl(ctx, bl);
	}

	/*
	 * Move deferred locked entries to cache before pruning
	 */
	spin_lock(&ctx->completion_lock);
	if (!list_empty(&ctx->io_buffers_comp))
		list_splice_init(&ctx->io_buffers_comp, &ctx->io_buffers_cache);
	spin_unlock(&ctx->completion_lock);

	list_for_each_safe(item, tmp, &ctx->io_buffers_cache) {
		buf = list_entry(item, struct io_buffer, list);
		kmem_cache_free(io_buf_cachep, buf);
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
	if (!tmp || tmp > MAX_BIDS_PER_BGID)
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
		if (!bl->is_buf_ring)
			ret = __io_remove_buffers(ctx, bl, p->nbufs);
	}
	io_ring_submit_unlock(ctx, issue_flags);
	if (ret < 0)
		req_set_fail(req);
	io_req_set_res(req, ret, 0);
	return IOU_OK;
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
	if (tmp + p->nbufs > MAX_BIDS_PER_BGID)
		return -EINVAL;
	p->bid = tmp;
	return 0;
}

#define IO_BUFFER_ALLOC_BATCH 64

static int io_refill_buffer_cache(struct io_ring_ctx *ctx)
{
	struct io_buffer *bufs[IO_BUFFER_ALLOC_BATCH];
	int allocated;

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
	 * batch of buffer entries and add those to our freelist.
	 */

	allocated = kmem_cache_alloc_bulk(io_buf_cachep, GFP_KERNEL_ACCOUNT,
					  ARRAY_SIZE(bufs), (void **) bufs);
	if (unlikely(!allocated)) {
		/*
		 * Bulk alloc is all-or-nothing. If we fail to get a batch,
		 * retry single alloc to be on the safe side.
		 */
		bufs[0] = kmem_cache_alloc(io_buf_cachep, GFP_KERNEL);
		if (!bufs[0])
			return -ENOMEM;
		allocated = 1;
	}

	while (allocated)
		list_add_tail(&bufs[--allocated]->list, &ctx->io_buffers_cache);

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
			/*
			 * Doesn't need rcu free as it was never visible, but
			 * let's keep it consistent throughout.
			 */
			kfree_rcu(bl, rcu);
			goto err;
		}
	}
	/* can't add buffers via this command for a mapped buffer ring */
	if (bl->is_buf_ring) {
		ret = -EINVAL;
		goto err;
	}

	ret = io_add_buffers(ctx, p, bl);
err:
	io_ring_submit_unlock(ctx, issue_flags);

	if (ret < 0)
		req_set_fail(req);
	io_req_set_res(req, ret, 0);
	return IOU_OK;
}

static int io_pin_pbuf_ring(struct io_uring_buf_reg *reg,
			    struct io_buffer_list *bl)
{
	struct io_uring_buf_ring *br = NULL;
	struct page **pages;
	int nr_pages, ret;

	pages = io_pin_pages(reg->ring_addr,
			     flex_array_size(br, bufs, reg->ring_entries),
			     &nr_pages);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	br = vmap(pages, nr_pages, VM_MAP, PAGE_KERNEL);
	if (!br) {
		ret = -ENOMEM;
		goto error_unpin;
	}

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
	if ((reg->ring_addr | (unsigned long) br) & (SHM_COLOUR - 1)) {
		ret = -EINVAL;
		goto error_unpin;
	}
#endif
	bl->buf_pages = pages;
	bl->buf_nr_pages = nr_pages;
	bl->buf_ring = br;
	bl->is_buf_ring = 1;
	bl->is_mmap = 0;
	return 0;
error_unpin:
	unpin_user_pages(pages, nr_pages);
	kvfree(pages);
	vunmap(br);
	return ret;
}

static int io_alloc_pbuf_ring(struct io_ring_ctx *ctx,
			      struct io_uring_buf_reg *reg,
			      struct io_buffer_list *bl)
{
	size_t ring_size;

	ring_size = reg->ring_entries * sizeof(struct io_uring_buf_ring);

	bl->buf_ring = io_pages_map(&bl->buf_pages, &bl->buf_nr_pages, ring_size);
	if (IS_ERR(bl->buf_ring)) {
		bl->buf_ring = NULL;
		return -ENOMEM;
	}

	bl->is_buf_ring = 1;
	bl->is_mmap = 1;
	return 0;
}

int io_register_pbuf_ring(struct io_ring_ctx *ctx, void __user *arg)
{
	struct io_uring_buf_reg reg;
	struct io_buffer_list *bl, *free_bl = NULL;
	int ret;

	lockdep_assert_held(&ctx->uring_lock);

	if (copy_from_user(&reg, arg, sizeof(reg)))
		return -EFAULT;

	if (reg.resv[0] || reg.resv[1] || reg.resv[2])
		return -EINVAL;
	if (reg.flags & ~IOU_PBUF_RING_MMAP)
		return -EINVAL;
	if (!(reg.flags & IOU_PBUF_RING_MMAP)) {
		if (!reg.ring_addr)
			return -EFAULT;
		if (reg.ring_addr & ~PAGE_MASK)
			return -EINVAL;
	} else {
		if (reg.ring_addr)
			return -EINVAL;
	}

	if (!is_power_of_2(reg.ring_entries))
		return -EINVAL;

	/* cannot disambiguate full vs empty due to head/tail size */
	if (reg.ring_entries >= 65536)
		return -EINVAL;

	bl = io_buffer_get_list(ctx, reg.bgid);
	if (bl) {
		/* if mapped buffer ring OR classic exists, don't allow */
		if (bl->is_buf_ring || !list_empty(&bl->buf_list))
			return -EEXIST;
	} else {
		free_bl = bl = kzalloc(sizeof(*bl), GFP_KERNEL);
		if (!bl)
			return -ENOMEM;
	}

	if (!(reg.flags & IOU_PBUF_RING_MMAP))
		ret = io_pin_pbuf_ring(&reg, bl);
	else
		ret = io_alloc_pbuf_ring(ctx, &reg, bl);

	if (!ret) {
		bl->nr_entries = reg.ring_entries;
		bl->mask = reg.ring_entries - 1;

		io_buffer_add_list(ctx, bl, reg.bgid);
		return 0;
	}

	kfree_rcu(free_bl, rcu);
	return ret;
}

int io_unregister_pbuf_ring(struct io_ring_ctx *ctx, void __user *arg)
{
	struct io_uring_buf_reg reg;
	struct io_buffer_list *bl;

	lockdep_assert_held(&ctx->uring_lock);

	if (copy_from_user(&reg, arg, sizeof(reg)))
		return -EFAULT;
	if (reg.resv[0] || reg.resv[1] || reg.resv[2])
		return -EINVAL;
	if (reg.flags)
		return -EINVAL;

	bl = io_buffer_get_list(ctx, reg.bgid);
	if (!bl)
		return -ENOENT;
	if (!bl->is_buf_ring)
		return -EINVAL;

	xa_erase(&ctx->io_bl_xa, bl->bgid);
	io_put_bl(ctx, bl);
	return 0;
}

int io_register_pbuf_status(struct io_ring_ctx *ctx, void __user *arg)
{
	struct io_uring_buf_status buf_status;
	struct io_buffer_list *bl;
	int i;

	if (copy_from_user(&buf_status, arg, sizeof(buf_status)))
		return -EFAULT;

	for (i = 0; i < ARRAY_SIZE(buf_status.resv); i++)
		if (buf_status.resv[i])
			return -EINVAL;

	bl = io_buffer_get_list(ctx, buf_status.buf_group);
	if (!bl)
		return -ENOENT;
	if (!bl->is_buf_ring)
		return -EINVAL;

	buf_status.head = bl->head;
	if (copy_to_user(arg, &buf_status, sizeof(buf_status)))
		return -EFAULT;

	return 0;
}

struct io_buffer_list *io_pbuf_get_bl(struct io_ring_ctx *ctx,
				      unsigned long bgid)
{
	struct io_buffer_list *bl;
	bool ret;

	/*
	 * We have to be a bit careful here - we're inside mmap and cannot grab
	 * the uring_lock. This means the buffer_list could be simultaneously
	 * going away, if someone is trying to be sneaky. Look it up under rcu
	 * so we know it's not going away, and attempt to grab a reference to
	 * it. If the ref is already zero, then fail the mapping. If successful,
	 * the caller will call io_put_bl() to drop the the reference at at the
	 * end. This may then safely free the buffer_list (and drop the pages)
	 * at that point, vm_insert_pages() would've already grabbed the
	 * necessary vma references.
	 */
	rcu_read_lock();
	bl = xa_load(&ctx->io_bl_xa, bgid);
	/* must be a mmap'able buffer ring and have pages */
	ret = false;
	if (bl && bl->is_mmap)
		ret = atomic_inc_not_zero(&bl->refs);
	rcu_read_unlock();

	if (ret)
		return bl;

	return ERR_PTR(-EINVAL);
}

int io_pbuf_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct io_ring_ctx *ctx = file->private_data;
	loff_t pgoff = vma->vm_pgoff << PAGE_SHIFT;
	struct io_buffer_list *bl;
	int bgid, ret;

	bgid = (pgoff & ~IORING_OFF_MMAP_MASK) >> IORING_OFF_PBUF_SHIFT;
	bl = io_pbuf_get_bl(ctx, bgid);
	if (IS_ERR(bl))
		return PTR_ERR(bl);

	ret = io_uring_mmap_pages(ctx, vma, bl->buf_pages, bl->buf_nr_pages);
	io_put_bl(ctx, bl);
	return ret;
}
