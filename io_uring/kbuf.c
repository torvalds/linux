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

/* BIDs are addressed by a 16-bit field in a CQE */
#define MAX_BIDS_PER_BGID (1 << 16)

struct io_provide_buf {
	struct file			*file;
	__u64				addr;
	__u32				len;
	__u32				bgid;
	__u32				nbufs;
	__u16				bid;
};

static struct io_buffer_list *__io_buffer_get_list(struct io_ring_ctx *ctx,
						   struct io_buffer_list *bl,
						   unsigned int bgid)
{
	if (bl && bgid < BGID_ARRAY)
		return &bl[bgid];

	return xa_load(&ctx->io_bl_xa, bgid);
}

struct io_buf_free {
	struct hlist_node		list;
	void				*mem;
	size_t				size;
	int				inuse;
};

static inline struct io_buffer_list *io_buffer_get_list(struct io_ring_ctx *ctx,
							unsigned int bgid)
{
	lockdep_assert_held(&ctx->uring_lock);

	return __io_buffer_get_list(ctx, ctx->io_bl, bgid);
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
	smp_store_release(&bl->is_ready, 1);

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
	/* mmaped buffers are always contig */
	if (bl->is_mmap || head < IO_BUFFER_LIST_BUF_PER_PAGE) {
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
		if (bl->is_mapped)
			ret = io_ring_buffer_select(req, len, bl, issue_flags);
		else
			ret = io_provided_buffer_select(req, len, bl);
	}
	io_ring_submit_unlock(req->ctx, issue_flags);
	return ret;
}

static __cold int io_init_bl_list(struct io_ring_ctx *ctx)
{
	struct io_buffer_list *bl;
	int i;

	bl = kcalloc(BGID_ARRAY, sizeof(struct io_buffer_list), GFP_KERNEL);
	if (!bl)
		return -ENOMEM;

	for (i = 0; i < BGID_ARRAY; i++) {
		INIT_LIST_HEAD(&bl[i].buf_list);
		bl[i].bgid = i;
	}

	smp_store_release(&ctx->io_bl, bl);
	return 0;
}

/*
 * Mark the given mapped range as free for reuse
 */
static void io_kbuf_mark_free(struct io_ring_ctx *ctx, struct io_buffer_list *bl)
{
	struct io_buf_free *ibf;

	hlist_for_each_entry(ibf, &ctx->io_buf_list, list) {
		if (bl->buf_ring == ibf->mem) {
			ibf->inuse = 0;
			return;
		}
	}

	/* can't happen... */
	WARN_ON_ONCE(1);
}

static int __io_remove_buffers(struct io_ring_ctx *ctx,
			       struct io_buffer_list *bl, unsigned nbufs)
{
	unsigned i = 0;

	/* shouldn't happen */
	if (!nbufs)
		return 0;

	if (bl->is_mapped) {
		i = bl->buf_ring->tail - bl->head;
		if (bl->is_mmap) {
			/*
			 * io_kbuf_list_free() will free the page(s) at
			 * ->release() time.
			 */
			io_kbuf_mark_free(ctx, bl);
			bl->buf_ring = NULL;
			bl->is_mmap = 0;
		} else if (bl->buf_nr_pages) {
			int j;

			for (j = 0; j < bl->buf_nr_pages; j++)
				unpin_user_page(bl->buf_pages[j]);
			kvfree(bl->buf_pages);
			bl->buf_pages = NULL;
			bl->buf_nr_pages = 0;
		}
		/* make sure it's seen as empty */
		INIT_LIST_HEAD(&bl->buf_list);
		bl->is_mapped = 0;
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
		kfree_rcu(bl, rcu);
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
		if (!bl->is_mapped)
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
			/*
			 * Doesn't need rcu free as it was never visible, but
			 * let's keep it consistent throughout. Also can't
			 * be a lower indexed array group, as adding one
			 * where lookup failed cannot happen.
			 */
			if (p->bgid >= BGID_ARRAY)
				kfree_rcu(bl, rcu);
			else
				WARN_ON_ONCE(1);
			goto err;
		}
	}
	/* can't add buffers via this command for a mapped buffer ring */
	if (bl->is_mapped) {
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
	struct io_uring_buf_ring *br;
	struct page **pages;
	int i, nr_pages;

	pages = io_pin_pages(reg->ring_addr,
			     flex_array_size(br, bufs, reg->ring_entries),
			     &nr_pages);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	/*
	 * Apparently some 32-bit boxes (ARM) will return highmem pages,
	 * which then need to be mapped. We could support that, but it'd
	 * complicate the code and slowdown the common cases quite a bit.
	 * So just error out, returning -EINVAL just like we did on kernels
	 * that didn't support mapped buffer rings.
	 */
	for (i = 0; i < nr_pages; i++)
		if (PageHighMem(pages[i]))
			goto error_unpin;

	br = page_address(pages[0]);
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
	if ((reg->ring_addr | (unsigned long) br) & (SHM_COLOUR - 1))
		goto error_unpin;
#endif
	bl->buf_pages = pages;
	bl->buf_nr_pages = nr_pages;
	bl->buf_ring = br;
	bl->is_mapped = 1;
	bl->is_mmap = 0;
	return 0;
error_unpin:
	for (i = 0; i < nr_pages; i++)
		unpin_user_page(pages[i]);
	kvfree(pages);
	return -EINVAL;
}

/*
 * See if we have a suitable region that we can reuse, rather than allocate
 * both a new io_buf_free and mem region again. We leave it on the list as
 * even a reused entry will need freeing at ring release.
 */
static struct io_buf_free *io_lookup_buf_free_entry(struct io_ring_ctx *ctx,
						    size_t ring_size)
{
	struct io_buf_free *ibf, *best = NULL;
	size_t best_dist;

	hlist_for_each_entry(ibf, &ctx->io_buf_list, list) {
		size_t dist;

		if (ibf->inuse || ibf->size < ring_size)
			continue;
		dist = ibf->size - ring_size;
		if (!best || dist < best_dist) {
			best = ibf;
			if (!dist)
				break;
			best_dist = dist;
		}
	}

	return best;
}

static int io_alloc_pbuf_ring(struct io_ring_ctx *ctx,
			      struct io_uring_buf_reg *reg,
			      struct io_buffer_list *bl)
{
	struct io_buf_free *ibf;
	size_t ring_size;
	void *ptr;

	ring_size = reg->ring_entries * sizeof(struct io_uring_buf_ring);

	/* Reuse existing entry, if we can */
	ibf = io_lookup_buf_free_entry(ctx, ring_size);
	if (!ibf) {
		ptr = io_mem_alloc(ring_size);
		if (IS_ERR(ptr))
			return PTR_ERR(ptr);

		/* Allocate and store deferred free entry */
		ibf = kmalloc(sizeof(*ibf), GFP_KERNEL_ACCOUNT);
		if (!ibf) {
			io_mem_free(ptr);
			return -ENOMEM;
		}
		ibf->mem = ptr;
		ibf->size = ring_size;
		hlist_add_head(&ibf->list, &ctx->io_buf_list);
	}
	ibf->inuse = 1;
	bl->buf_ring = ibf->mem;
	bl->is_mapped = 1;
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

	if (unlikely(reg.bgid < BGID_ARRAY && !ctx->io_bl)) {
		int ret = io_init_bl_list(ctx);
		if (ret)
			return ret;
	}

	bl = io_buffer_get_list(ctx, reg.bgid);
	if (bl) {
		/* if mapped buffer ring OR classic exists, don't allow */
		if (bl->is_mapped || !list_empty(&bl->buf_list))
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
	if (!bl->is_mapped)
		return -EINVAL;

	__io_remove_buffers(ctx, bl, -1U);
	if (bl->bgid >= BGID_ARRAY) {
		xa_erase(&ctx->io_bl_xa, bl->bgid);
		kfree_rcu(bl, rcu);
	}
	return 0;
}

void *io_pbuf_get_address(struct io_ring_ctx *ctx, unsigned long bgid)
{
	struct io_buffer_list *bl;

	bl = __io_buffer_get_list(ctx, smp_load_acquire(&ctx->io_bl), bgid);

	if (!bl || !bl->is_mmap)
		return NULL;
	/*
	 * Ensure the list is fully setup. Only strictly needed for RCU lookup
	 * via mmap, and in that case only for the array indexed groups. For
	 * the xarray lookups, it's either visible and ready, or not at all.
	 */
	if (!smp_load_acquire(&bl->is_ready))
		return NULL;

	return bl->buf_ring;
}

/*
 * Called at or after ->release(), free the mmap'ed buffers that we used
 * for memory mapped provided buffer rings.
 */
void io_kbuf_mmap_list_free(struct io_ring_ctx *ctx)
{
	struct io_buf_free *ibf;
	struct hlist_node *tmp;

	hlist_for_each_entry_safe(ibf, tmp, &ctx->io_buf_list, list) {
		hlist_del(&ibf->list);
		io_mem_free(ibf->mem);
		kfree(ibf);
	}
}
