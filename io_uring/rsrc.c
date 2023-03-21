// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/nospec.h>
#include <linux/hugetlb.h>
#include <linux/compat.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "openclose.h"
#include "rsrc.h"

struct io_rsrc_update {
	struct file			*file;
	u64				arg;
	u32				nr_args;
	u32				offset;
};

static int io_sqe_buffer_register(struct io_ring_ctx *ctx, struct iovec *iov,
				  struct io_mapped_ubuf **pimu,
				  struct page **last_hpage);

#define IO_RSRC_REF_BATCH	100

/* only define max */
#define IORING_MAX_FIXED_FILES	(1U << 20)
#define IORING_MAX_REG_BUFFERS	(1U << 14)

void io_rsrc_refs_drop(struct io_ring_ctx *ctx)
	__must_hold(&ctx->uring_lock)
{
	if (ctx->rsrc_cached_refs) {
		io_rsrc_put_node(ctx->rsrc_node, ctx->rsrc_cached_refs);
		ctx->rsrc_cached_refs = 0;
	}
}

int __io_account_mem(struct user_struct *user, unsigned long nr_pages)
{
	unsigned long page_limit, cur_pages, new_pages;

	if (!nr_pages)
		return 0;

	/* Don't allow more pages than we can safely lock */
	page_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;

	cur_pages = atomic_long_read(&user->locked_vm);
	do {
		new_pages = cur_pages + nr_pages;
		if (new_pages > page_limit)
			return -ENOMEM;
	} while (!atomic_long_try_cmpxchg(&user->locked_vm,
					  &cur_pages, new_pages));
	return 0;
}

static void io_unaccount_mem(struct io_ring_ctx *ctx, unsigned long nr_pages)
{
	if (ctx->user)
		__io_unaccount_mem(ctx->user, nr_pages);

	if (ctx->mm_account)
		atomic64_sub(nr_pages, &ctx->mm_account->pinned_vm);
}

static int io_account_mem(struct io_ring_ctx *ctx, unsigned long nr_pages)
{
	int ret;

	if (ctx->user) {
		ret = __io_account_mem(ctx->user, nr_pages);
		if (ret)
			return ret;
	}

	if (ctx->mm_account)
		atomic64_add(nr_pages, &ctx->mm_account->pinned_vm);

	return 0;
}

static int io_copy_iov(struct io_ring_ctx *ctx, struct iovec *dst,
		       void __user *arg, unsigned index)
{
	struct iovec __user *src;

#ifdef CONFIG_COMPAT
	if (ctx->compat) {
		struct compat_iovec __user *ciovs;
		struct compat_iovec ciov;

		ciovs = (struct compat_iovec __user *) arg;
		if (copy_from_user(&ciov, &ciovs[index], sizeof(ciov)))
			return -EFAULT;

		dst->iov_base = u64_to_user_ptr((u64)ciov.iov_base);
		dst->iov_len = ciov.iov_len;
		return 0;
	}
#endif
	src = (struct iovec __user *) arg;
	if (copy_from_user(dst, &src[index], sizeof(*dst)))
		return -EFAULT;
	return 0;
}

static int io_buffer_validate(struct iovec *iov)
{
	unsigned long tmp, acct_len = iov->iov_len + (PAGE_SIZE - 1);

	/*
	 * Don't impose further limits on the size and buffer
	 * constraints here, we'll -EINVAL later when IO is
	 * submitted if they are wrong.
	 */
	if (!iov->iov_base)
		return iov->iov_len ? -EFAULT : 0;
	if (!iov->iov_len)
		return -EFAULT;

	/* arbitrary limit, but we need something */
	if (iov->iov_len > SZ_1G)
		return -EFAULT;

	if (check_add_overflow((unsigned long)iov->iov_base, acct_len, &tmp))
		return -EOVERFLOW;

	return 0;
}

static void io_buffer_unmap(struct io_ring_ctx *ctx, struct io_mapped_ubuf **slot)
{
	struct io_mapped_ubuf *imu = *slot;
	unsigned int i;

	if (imu != ctx->dummy_ubuf) {
		for (i = 0; i < imu->nr_bvecs; i++)
			unpin_user_page(imu->bvec[i].bv_page);
		if (imu->acct_pages)
			io_unaccount_mem(ctx, imu->acct_pages);
		kvfree(imu);
	}
	*slot = NULL;
}

void io_rsrc_refs_refill(struct io_ring_ctx *ctx)
	__must_hold(&ctx->uring_lock)
{
	ctx->rsrc_cached_refs += IO_RSRC_REF_BATCH;
	percpu_ref_get_many(&ctx->rsrc_node->refs, IO_RSRC_REF_BATCH);
}

static void __io_rsrc_put_work(struct io_rsrc_node *ref_node)
{
	struct io_rsrc_data *rsrc_data = ref_node->rsrc_data;
	struct io_ring_ctx *ctx = rsrc_data->ctx;
	struct io_rsrc_put *prsrc, *tmp;

	list_for_each_entry_safe(prsrc, tmp, &ref_node->rsrc_list, list) {
		list_del(&prsrc->list);

		if (prsrc->tag) {
			if (ctx->flags & IORING_SETUP_IOPOLL) {
				mutex_lock(&ctx->uring_lock);
				io_post_aux_cqe(ctx, prsrc->tag, 0, 0, true);
				mutex_unlock(&ctx->uring_lock);
			} else {
				io_post_aux_cqe(ctx, prsrc->tag, 0, 0, true);
			}
		}

		rsrc_data->do_put(ctx, prsrc);
		kfree(prsrc);
	}

	io_rsrc_node_destroy(ref_node);
	if (atomic_dec_and_test(&rsrc_data->refs))
		complete(&rsrc_data->done);
}

void io_rsrc_put_work(struct work_struct *work)
{
	struct io_ring_ctx *ctx;
	struct llist_node *node;

	ctx = container_of(work, struct io_ring_ctx, rsrc_put_work.work);
	node = llist_del_all(&ctx->rsrc_put_llist);

	while (node) {
		struct io_rsrc_node *ref_node;
		struct llist_node *next = node->next;

		ref_node = llist_entry(node, struct io_rsrc_node, llist);
		__io_rsrc_put_work(ref_node);
		node = next;
	}
}

void io_wait_rsrc_data(struct io_rsrc_data *data)
{
	if (data && !atomic_dec_and_test(&data->refs))
		wait_for_completion(&data->done);
}

void io_rsrc_node_destroy(struct io_rsrc_node *ref_node)
{
	percpu_ref_exit(&ref_node->refs);
	kfree(ref_node);
}

static __cold void io_rsrc_node_ref_zero(struct percpu_ref *ref)
{
	struct io_rsrc_node *node = container_of(ref, struct io_rsrc_node, refs);
	struct io_ring_ctx *ctx = node->rsrc_data->ctx;
	unsigned long flags;
	bool first_add = false;
	unsigned long delay = HZ;

	spin_lock_irqsave(&ctx->rsrc_ref_lock, flags);
	node->done = true;

	/* if we are mid-quiesce then do not delay */
	if (node->rsrc_data->quiesce)
		delay = 0;

	while (!list_empty(&ctx->rsrc_ref_list)) {
		node = list_first_entry(&ctx->rsrc_ref_list,
					    struct io_rsrc_node, node);
		/* recycle ref nodes in order */
		if (!node->done)
			break;
		list_del(&node->node);
		first_add |= llist_add(&node->llist, &ctx->rsrc_put_llist);
	}
	spin_unlock_irqrestore(&ctx->rsrc_ref_lock, flags);

	if (first_add)
		mod_delayed_work(system_wq, &ctx->rsrc_put_work, delay);
}

static struct io_rsrc_node *io_rsrc_node_alloc(void)
{
	struct io_rsrc_node *ref_node;

	ref_node = kzalloc(sizeof(*ref_node), GFP_KERNEL);
	if (!ref_node)
		return NULL;

	if (percpu_ref_init(&ref_node->refs, io_rsrc_node_ref_zero,
			    0, GFP_KERNEL)) {
		kfree(ref_node);
		return NULL;
	}
	INIT_LIST_HEAD(&ref_node->node);
	INIT_LIST_HEAD(&ref_node->rsrc_list);
	ref_node->done = false;
	return ref_node;
}

void io_rsrc_node_switch(struct io_ring_ctx *ctx,
			 struct io_rsrc_data *data_to_kill)
	__must_hold(&ctx->uring_lock)
{
	WARN_ON_ONCE(!ctx->rsrc_backup_node);
	WARN_ON_ONCE(data_to_kill && !ctx->rsrc_node);

	io_rsrc_refs_drop(ctx);

	if (data_to_kill) {
		struct io_rsrc_node *rsrc_node = ctx->rsrc_node;

		rsrc_node->rsrc_data = data_to_kill;
		spin_lock_irq(&ctx->rsrc_ref_lock);
		list_add_tail(&rsrc_node->node, &ctx->rsrc_ref_list);
		spin_unlock_irq(&ctx->rsrc_ref_lock);

		atomic_inc(&data_to_kill->refs);
		percpu_ref_kill(&rsrc_node->refs);
		ctx->rsrc_node = NULL;
	}

	if (!ctx->rsrc_node) {
		ctx->rsrc_node = ctx->rsrc_backup_node;
		ctx->rsrc_backup_node = NULL;
	}
}

int io_rsrc_node_switch_start(struct io_ring_ctx *ctx)
{
	if (ctx->rsrc_backup_node)
		return 0;
	ctx->rsrc_backup_node = io_rsrc_node_alloc();
	return ctx->rsrc_backup_node ? 0 : -ENOMEM;
}

__cold static int io_rsrc_ref_quiesce(struct io_rsrc_data *data,
				      struct io_ring_ctx *ctx)
{
	int ret;

	/* As we may drop ->uring_lock, other task may have started quiesce */
	if (data->quiesce)
		return -ENXIO;

	data->quiesce = true;
	do {
		ret = io_rsrc_node_switch_start(ctx);
		if (ret)
			break;
		io_rsrc_node_switch(ctx, data);

		/* kill initial ref, already quiesced if zero */
		if (atomic_dec_and_test(&data->refs))
			break;
		mutex_unlock(&ctx->uring_lock);
		flush_delayed_work(&ctx->rsrc_put_work);
		ret = wait_for_completion_interruptible(&data->done);
		if (!ret) {
			mutex_lock(&ctx->uring_lock);
			if (atomic_read(&data->refs) > 0) {
				/*
				 * it has been revived by another thread while
				 * we were unlocked
				 */
				mutex_unlock(&ctx->uring_lock);
			} else {
				break;
			}
		}

		atomic_inc(&data->refs);
		/* wait for all works potentially completing data->done */
		flush_delayed_work(&ctx->rsrc_put_work);
		reinit_completion(&data->done);

		ret = io_run_task_work_sig(ctx);
		mutex_lock(&ctx->uring_lock);
	} while (ret >= 0);
	data->quiesce = false;

	return ret;
}

static void io_free_page_table(void **table, size_t size)
{
	unsigned i, nr_tables = DIV_ROUND_UP(size, PAGE_SIZE);

	for (i = 0; i < nr_tables; i++)
		kfree(table[i]);
	kfree(table);
}

static void io_rsrc_data_free(struct io_rsrc_data *data)
{
	size_t size = data->nr * sizeof(data->tags[0][0]);

	if (data->tags)
		io_free_page_table((void **)data->tags, size);
	kfree(data);
}

static __cold void **io_alloc_page_table(size_t size)
{
	unsigned i, nr_tables = DIV_ROUND_UP(size, PAGE_SIZE);
	size_t init_size = size;
	void **table;

	table = kcalloc(nr_tables, sizeof(*table), GFP_KERNEL_ACCOUNT);
	if (!table)
		return NULL;

	for (i = 0; i < nr_tables; i++) {
		unsigned int this_size = min_t(size_t, size, PAGE_SIZE);

		table[i] = kzalloc(this_size, GFP_KERNEL_ACCOUNT);
		if (!table[i]) {
			io_free_page_table(table, init_size);
			return NULL;
		}
		size -= this_size;
	}
	return table;
}

__cold static int io_rsrc_data_alloc(struct io_ring_ctx *ctx,
				     rsrc_put_fn *do_put, u64 __user *utags,
				     unsigned nr, struct io_rsrc_data **pdata)
{
	struct io_rsrc_data *data;
	int ret = -ENOMEM;
	unsigned i;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->tags = (u64 **)io_alloc_page_table(nr * sizeof(data->tags[0][0]));
	if (!data->tags) {
		kfree(data);
		return -ENOMEM;
	}

	data->nr = nr;
	data->ctx = ctx;
	data->do_put = do_put;
	if (utags) {
		ret = -EFAULT;
		for (i = 0; i < nr; i++) {
			u64 *tag_slot = io_get_tag_slot(data, i);

			if (copy_from_user(tag_slot, &utags[i],
					   sizeof(*tag_slot)))
				goto fail;
		}
	}

	atomic_set(&data->refs, 1);
	init_completion(&data->done);
	*pdata = data;
	return 0;
fail:
	io_rsrc_data_free(data);
	return ret;
}

static int __io_sqe_files_update(struct io_ring_ctx *ctx,
				 struct io_uring_rsrc_update2 *up,
				 unsigned nr_args)
{
	u64 __user *tags = u64_to_user_ptr(up->tags);
	__s32 __user *fds = u64_to_user_ptr(up->data);
	struct io_rsrc_data *data = ctx->file_data;
	struct io_fixed_file *file_slot;
	struct file *file;
	int fd, i, err = 0;
	unsigned int done;
	bool needs_switch = false;

	if (!ctx->file_data)
		return -ENXIO;
	if (up->offset + nr_args > ctx->nr_user_files)
		return -EINVAL;

	for (done = 0; done < nr_args; done++) {
		u64 tag = 0;

		if ((tags && copy_from_user(&tag, &tags[done], sizeof(tag))) ||
		    copy_from_user(&fd, &fds[done], sizeof(fd))) {
			err = -EFAULT;
			break;
		}
		if ((fd == IORING_REGISTER_FILES_SKIP || fd == -1) && tag) {
			err = -EINVAL;
			break;
		}
		if (fd == IORING_REGISTER_FILES_SKIP)
			continue;

		i = array_index_nospec(up->offset + done, ctx->nr_user_files);
		file_slot = io_fixed_file_slot(&ctx->file_table, i);

		if (file_slot->file_ptr) {
			file = (struct file *)(file_slot->file_ptr & FFS_MASK);
			err = io_queue_rsrc_removal(data, i, ctx->rsrc_node, file);
			if (err)
				break;
			file_slot->file_ptr = 0;
			io_file_bitmap_clear(&ctx->file_table, i);
			needs_switch = true;
		}
		if (fd != -1) {
			file = fget(fd);
			if (!file) {
				err = -EBADF;
				break;
			}
			/*
			 * Don't allow io_uring instances to be registered. If
			 * UNIX isn't enabled, then this causes a reference
			 * cycle and this instance can never get freed. If UNIX
			 * is enabled we'll handle it just fine, but there's
			 * still no point in allowing a ring fd as it doesn't
			 * support regular read/write anyway.
			 */
			if (io_is_uring_fops(file)) {
				fput(file);
				err = -EBADF;
				break;
			}
			err = io_scm_file_account(ctx, file);
			if (err) {
				fput(file);
				break;
			}
			*io_get_tag_slot(data, i) = tag;
			io_fixed_file_set(file_slot, file);
			io_file_bitmap_set(&ctx->file_table, i);
		}
	}

	if (needs_switch)
		io_rsrc_node_switch(ctx, data);
	return done ? done : err;
}

static int __io_sqe_buffers_update(struct io_ring_ctx *ctx,
				   struct io_uring_rsrc_update2 *up,
				   unsigned int nr_args)
{
	u64 __user *tags = u64_to_user_ptr(up->tags);
	struct iovec iov, __user *iovs = u64_to_user_ptr(up->data);
	struct page *last_hpage = NULL;
	bool needs_switch = false;
	__u32 done;
	int i, err;

	if (!ctx->buf_data)
		return -ENXIO;
	if (up->offset + nr_args > ctx->nr_user_bufs)
		return -EINVAL;

	for (done = 0; done < nr_args; done++) {
		struct io_mapped_ubuf *imu;
		int offset = up->offset + done;
		u64 tag = 0;

		err = io_copy_iov(ctx, &iov, iovs, done);
		if (err)
			break;
		if (tags && copy_from_user(&tag, &tags[done], sizeof(tag))) {
			err = -EFAULT;
			break;
		}
		err = io_buffer_validate(&iov);
		if (err)
			break;
		if (!iov.iov_base && tag) {
			err = -EINVAL;
			break;
		}
		err = io_sqe_buffer_register(ctx, &iov, &imu, &last_hpage);
		if (err)
			break;

		i = array_index_nospec(offset, ctx->nr_user_bufs);
		if (ctx->user_bufs[i] != ctx->dummy_ubuf) {
			err = io_queue_rsrc_removal(ctx->buf_data, i,
						    ctx->rsrc_node, ctx->user_bufs[i]);
			if (unlikely(err)) {
				io_buffer_unmap(ctx, &imu);
				break;
			}
			ctx->user_bufs[i] = ctx->dummy_ubuf;
			needs_switch = true;
		}

		ctx->user_bufs[i] = imu;
		*io_get_tag_slot(ctx->buf_data, offset) = tag;
	}

	if (needs_switch)
		io_rsrc_node_switch(ctx, ctx->buf_data);
	return done ? done : err;
}

static int __io_register_rsrc_update(struct io_ring_ctx *ctx, unsigned type,
				     struct io_uring_rsrc_update2 *up,
				     unsigned nr_args)
{
	__u32 tmp;
	int err;

	if (check_add_overflow(up->offset, nr_args, &tmp))
		return -EOVERFLOW;
	err = io_rsrc_node_switch_start(ctx);
	if (err)
		return err;

	switch (type) {
	case IORING_RSRC_FILE:
		return __io_sqe_files_update(ctx, up, nr_args);
	case IORING_RSRC_BUFFER:
		return __io_sqe_buffers_update(ctx, up, nr_args);
	}
	return -EINVAL;
}

int io_register_files_update(struct io_ring_ctx *ctx, void __user *arg,
			     unsigned nr_args)
{
	struct io_uring_rsrc_update2 up;

	if (!nr_args)
		return -EINVAL;
	memset(&up, 0, sizeof(up));
	if (copy_from_user(&up, arg, sizeof(struct io_uring_rsrc_update)))
		return -EFAULT;
	if (up.resv || up.resv2)
		return -EINVAL;
	return __io_register_rsrc_update(ctx, IORING_RSRC_FILE, &up, nr_args);
}

int io_register_rsrc_update(struct io_ring_ctx *ctx, void __user *arg,
			    unsigned size, unsigned type)
{
	struct io_uring_rsrc_update2 up;

	if (size != sizeof(up))
		return -EINVAL;
	if (copy_from_user(&up, arg, sizeof(up)))
		return -EFAULT;
	if (!up.nr || up.resv || up.resv2)
		return -EINVAL;
	return __io_register_rsrc_update(ctx, type, &up, up.nr);
}

__cold int io_register_rsrc(struct io_ring_ctx *ctx, void __user *arg,
			    unsigned int size, unsigned int type)
{
	struct io_uring_rsrc_register rr;

	/* keep it extendible */
	if (size != sizeof(rr))
		return -EINVAL;

	memset(&rr, 0, sizeof(rr));
	if (copy_from_user(&rr, arg, size))
		return -EFAULT;
	if (!rr.nr || rr.resv2)
		return -EINVAL;
	if (rr.flags & ~IORING_RSRC_REGISTER_SPARSE)
		return -EINVAL;

	switch (type) {
	case IORING_RSRC_FILE:
		if (rr.flags & IORING_RSRC_REGISTER_SPARSE && rr.data)
			break;
		return io_sqe_files_register(ctx, u64_to_user_ptr(rr.data),
					     rr.nr, u64_to_user_ptr(rr.tags));
	case IORING_RSRC_BUFFER:
		if (rr.flags & IORING_RSRC_REGISTER_SPARSE && rr.data)
			break;
		return io_sqe_buffers_register(ctx, u64_to_user_ptr(rr.data),
					       rr.nr, u64_to_user_ptr(rr.tags));
	}
	return -EINVAL;
}

int io_files_update_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_rsrc_update *up = io_kiocb_to_cmd(req, struct io_rsrc_update);

	if (unlikely(req->flags & (REQ_F_FIXED_FILE | REQ_F_BUFFER_SELECT)))
		return -EINVAL;
	if (sqe->rw_flags || sqe->splice_fd_in)
		return -EINVAL;

	up->offset = READ_ONCE(sqe->off);
	up->nr_args = READ_ONCE(sqe->len);
	if (!up->nr_args)
		return -EINVAL;
	up->arg = READ_ONCE(sqe->addr);
	return 0;
}

static int io_files_update_with_index_alloc(struct io_kiocb *req,
					    unsigned int issue_flags)
{
	struct io_rsrc_update *up = io_kiocb_to_cmd(req, struct io_rsrc_update);
	__s32 __user *fds = u64_to_user_ptr(up->arg);
	unsigned int done;
	struct file *file;
	int ret, fd;

	if (!req->ctx->file_data)
		return -ENXIO;

	for (done = 0; done < up->nr_args; done++) {
		if (copy_from_user(&fd, &fds[done], sizeof(fd))) {
			ret = -EFAULT;
			break;
		}

		file = fget(fd);
		if (!file) {
			ret = -EBADF;
			break;
		}
		ret = io_fixed_fd_install(req, issue_flags, file,
					  IORING_FILE_INDEX_ALLOC);
		if (ret < 0)
			break;
		if (copy_to_user(&fds[done], &ret, sizeof(ret))) {
			__io_close_fixed(req->ctx, issue_flags, ret);
			ret = -EFAULT;
			break;
		}
	}

	if (done)
		return done;
	return ret;
}

int io_files_update(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_rsrc_update *up = io_kiocb_to_cmd(req, struct io_rsrc_update);
	struct io_ring_ctx *ctx = req->ctx;
	struct io_uring_rsrc_update2 up2;
	int ret;

	up2.offset = up->offset;
	up2.data = up->arg;
	up2.nr = 0;
	up2.tags = 0;
	up2.resv = 0;
	up2.resv2 = 0;

	if (up->offset == IORING_FILE_INDEX_ALLOC) {
		ret = io_files_update_with_index_alloc(req, issue_flags);
	} else {
		io_ring_submit_lock(ctx, issue_flags);
		ret = __io_register_rsrc_update(ctx, IORING_RSRC_FILE,
						&up2, up->nr_args);
		io_ring_submit_unlock(ctx, issue_flags);
	}

	if (ret < 0)
		req_set_fail(req);
	io_req_set_res(req, ret, 0);
	return IOU_OK;
}

int io_queue_rsrc_removal(struct io_rsrc_data *data, unsigned idx,
			  struct io_rsrc_node *node, void *rsrc)
{
	u64 *tag_slot = io_get_tag_slot(data, idx);
	struct io_rsrc_put *prsrc;

	prsrc = kzalloc(sizeof(*prsrc), GFP_KERNEL);
	if (!prsrc)
		return -ENOMEM;

	prsrc->tag = *tag_slot;
	*tag_slot = 0;
	prsrc->rsrc = rsrc;
	list_add(&prsrc->list, &node->rsrc_list);
	return 0;
}

void __io_sqe_files_unregister(struct io_ring_ctx *ctx)
{
	int i;

	for (i = 0; i < ctx->nr_user_files; i++) {
		struct file *file = io_file_from_index(&ctx->file_table, i);

		/* skip scm accounted files, they'll be freed by ->ring_sock */
		if (!file || io_file_need_scm(file))
			continue;
		io_file_bitmap_clear(&ctx->file_table, i);
		fput(file);
	}

#if defined(CONFIG_UNIX)
	if (ctx->ring_sock) {
		struct sock *sock = ctx->ring_sock->sk;
		struct sk_buff *skb;

		while ((skb = skb_dequeue(&sock->sk_receive_queue)) != NULL)
			kfree_skb(skb);
	}
#endif
	io_free_file_tables(&ctx->file_table);
	io_file_table_set_alloc_range(ctx, 0, 0);
	io_rsrc_data_free(ctx->file_data);
	ctx->file_data = NULL;
	ctx->nr_user_files = 0;
}

int io_sqe_files_unregister(struct io_ring_ctx *ctx)
{
	unsigned nr = ctx->nr_user_files;
	int ret;

	if (!ctx->file_data)
		return -ENXIO;

	/*
	 * Quiesce may unlock ->uring_lock, and while it's not held
	 * prevent new requests using the table.
	 */
	ctx->nr_user_files = 0;
	ret = io_rsrc_ref_quiesce(ctx->file_data, ctx);
	ctx->nr_user_files = nr;
	if (!ret)
		__io_sqe_files_unregister(ctx);
	return ret;
}

/*
 * Ensure the UNIX gc is aware of our file set, so we are certain that
 * the io_uring can be safely unregistered on process exit, even if we have
 * loops in the file referencing. We account only files that can hold other
 * files because otherwise they can't form a loop and so are not interesting
 * for GC.
 */
int __io_scm_file_account(struct io_ring_ctx *ctx, struct file *file)
{
#if defined(CONFIG_UNIX)
	struct sock *sk = ctx->ring_sock->sk;
	struct sk_buff_head *head = &sk->sk_receive_queue;
	struct scm_fp_list *fpl;
	struct sk_buff *skb;

	if (likely(!io_file_need_scm(file)))
		return 0;

	/*
	 * See if we can merge this file into an existing skb SCM_RIGHTS
	 * file set. If there's no room, fall back to allocating a new skb
	 * and filling it in.
	 */
	spin_lock_irq(&head->lock);
	skb = skb_peek(head);
	if (skb && UNIXCB(skb).fp->count < SCM_MAX_FD)
		__skb_unlink(skb, head);
	else
		skb = NULL;
	spin_unlock_irq(&head->lock);

	if (!skb) {
		fpl = kzalloc(sizeof(*fpl), GFP_KERNEL);
		if (!fpl)
			return -ENOMEM;

		skb = alloc_skb(0, GFP_KERNEL);
		if (!skb) {
			kfree(fpl);
			return -ENOMEM;
		}

		fpl->user = get_uid(current_user());
		fpl->max = SCM_MAX_FD;
		fpl->count = 0;

		UNIXCB(skb).fp = fpl;
		skb->sk = sk;
		skb->scm_io_uring = 1;
		skb->destructor = unix_destruct_scm;
		refcount_add(skb->truesize, &sk->sk_wmem_alloc);
	}

	fpl = UNIXCB(skb).fp;
	fpl->fp[fpl->count++] = get_file(file);
	unix_inflight(fpl->user, file);
	skb_queue_head(head, skb);
	fput(file);
#endif
	return 0;
}

static void io_rsrc_file_put(struct io_ring_ctx *ctx, struct io_rsrc_put *prsrc)
{
	struct file *file = prsrc->file;
#if defined(CONFIG_UNIX)
	struct sock *sock = ctx->ring_sock->sk;
	struct sk_buff_head list, *head = &sock->sk_receive_queue;
	struct sk_buff *skb;
	int i;

	if (!io_file_need_scm(file)) {
		fput(file);
		return;
	}

	__skb_queue_head_init(&list);

	/*
	 * Find the skb that holds this file in its SCM_RIGHTS. When found,
	 * remove this entry and rearrange the file array.
	 */
	skb = skb_dequeue(head);
	while (skb) {
		struct scm_fp_list *fp;

		fp = UNIXCB(skb).fp;
		for (i = 0; i < fp->count; i++) {
			int left;

			if (fp->fp[i] != file)
				continue;

			unix_notinflight(fp->user, fp->fp[i]);
			left = fp->count - 1 - i;
			if (left) {
				memmove(&fp->fp[i], &fp->fp[i + 1],
						left * sizeof(struct file *));
			}
			fp->count--;
			if (!fp->count) {
				kfree_skb(skb);
				skb = NULL;
			} else {
				__skb_queue_tail(&list, skb);
			}
			fput(file);
			file = NULL;
			break;
		}

		if (!file)
			break;

		__skb_queue_tail(&list, skb);

		skb = skb_dequeue(head);
	}

	if (skb_peek(&list)) {
		spin_lock_irq(&head->lock);
		while ((skb = __skb_dequeue(&list)) != NULL)
			__skb_queue_tail(head, skb);
		spin_unlock_irq(&head->lock);
	}
#else
	fput(file);
#endif
}

int io_sqe_files_register(struct io_ring_ctx *ctx, void __user *arg,
			  unsigned nr_args, u64 __user *tags)
{
	__s32 __user *fds = (__s32 __user *) arg;
	struct file *file;
	int fd, ret;
	unsigned i;

	if (ctx->file_data)
		return -EBUSY;
	if (!nr_args)
		return -EINVAL;
	if (nr_args > IORING_MAX_FIXED_FILES)
		return -EMFILE;
	if (nr_args > rlimit(RLIMIT_NOFILE))
		return -EMFILE;
	ret = io_rsrc_node_switch_start(ctx);
	if (ret)
		return ret;
	ret = io_rsrc_data_alloc(ctx, io_rsrc_file_put, tags, nr_args,
				 &ctx->file_data);
	if (ret)
		return ret;

	if (!io_alloc_file_tables(&ctx->file_table, nr_args)) {
		io_rsrc_data_free(ctx->file_data);
		ctx->file_data = NULL;
		return -ENOMEM;
	}

	for (i = 0; i < nr_args; i++, ctx->nr_user_files++) {
		struct io_fixed_file *file_slot;

		if (fds && copy_from_user(&fd, &fds[i], sizeof(fd))) {
			ret = -EFAULT;
			goto fail;
		}
		/* allow sparse sets */
		if (!fds || fd == -1) {
			ret = -EINVAL;
			if (unlikely(*io_get_tag_slot(ctx->file_data, i)))
				goto fail;
			continue;
		}

		file = fget(fd);
		ret = -EBADF;
		if (unlikely(!file))
			goto fail;

		/*
		 * Don't allow io_uring instances to be registered. If UNIX
		 * isn't enabled, then this causes a reference cycle and this
		 * instance can never get freed. If UNIX is enabled we'll
		 * handle it just fine, but there's still no point in allowing
		 * a ring fd as it doesn't support regular read/write anyway.
		 */
		if (io_is_uring_fops(file)) {
			fput(file);
			goto fail;
		}
		ret = io_scm_file_account(ctx, file);
		if (ret) {
			fput(file);
			goto fail;
		}
		file_slot = io_fixed_file_slot(&ctx->file_table, i);
		io_fixed_file_set(file_slot, file);
		io_file_bitmap_set(&ctx->file_table, i);
	}

	/* default it to the whole table */
	io_file_table_set_alloc_range(ctx, 0, ctx->nr_user_files);
	io_rsrc_node_switch(ctx, NULL);
	return 0;
fail:
	__io_sqe_files_unregister(ctx);
	return ret;
}

static void io_rsrc_buf_put(struct io_ring_ctx *ctx, struct io_rsrc_put *prsrc)
{
	io_buffer_unmap(ctx, &prsrc->buf);
	prsrc->buf = NULL;
}

void __io_sqe_buffers_unregister(struct io_ring_ctx *ctx)
{
	unsigned int i;

	for (i = 0; i < ctx->nr_user_bufs; i++)
		io_buffer_unmap(ctx, &ctx->user_bufs[i]);
	kfree(ctx->user_bufs);
	io_rsrc_data_free(ctx->buf_data);
	ctx->user_bufs = NULL;
	ctx->buf_data = NULL;
	ctx->nr_user_bufs = 0;
}

int io_sqe_buffers_unregister(struct io_ring_ctx *ctx)
{
	unsigned nr = ctx->nr_user_bufs;
	int ret;

	if (!ctx->buf_data)
		return -ENXIO;

	/*
	 * Quiesce may unlock ->uring_lock, and while it's not held
	 * prevent new requests using the table.
	 */
	ctx->nr_user_bufs = 0;
	ret = io_rsrc_ref_quiesce(ctx->buf_data, ctx);
	ctx->nr_user_bufs = nr;
	if (!ret)
		__io_sqe_buffers_unregister(ctx);
	return ret;
}

/*
 * Not super efficient, but this is just a registration time. And we do cache
 * the last compound head, so generally we'll only do a full search if we don't
 * match that one.
 *
 * We check if the given compound head page has already been accounted, to
 * avoid double accounting it. This allows us to account the full size of the
 * page, not just the constituent pages of a huge page.
 */
static bool headpage_already_acct(struct io_ring_ctx *ctx, struct page **pages,
				  int nr_pages, struct page *hpage)
{
	int i, j;

	/* check current page array */
	for (i = 0; i < nr_pages; i++) {
		if (!PageCompound(pages[i]))
			continue;
		if (compound_head(pages[i]) == hpage)
			return true;
	}

	/* check previously registered pages */
	for (i = 0; i < ctx->nr_user_bufs; i++) {
		struct io_mapped_ubuf *imu = ctx->user_bufs[i];

		for (j = 0; j < imu->nr_bvecs; j++) {
			if (!PageCompound(imu->bvec[j].bv_page))
				continue;
			if (compound_head(imu->bvec[j].bv_page) == hpage)
				return true;
		}
	}

	return false;
}

static int io_buffer_account_pin(struct io_ring_ctx *ctx, struct page **pages,
				 int nr_pages, struct io_mapped_ubuf *imu,
				 struct page **last_hpage)
{
	int i, ret;

	imu->acct_pages = 0;
	for (i = 0; i < nr_pages; i++) {
		if (!PageCompound(pages[i])) {
			imu->acct_pages++;
		} else {
			struct page *hpage;

			hpage = compound_head(pages[i]);
			if (hpage == *last_hpage)
				continue;
			*last_hpage = hpage;
			if (headpage_already_acct(ctx, pages, i, hpage))
				continue;
			imu->acct_pages += page_size(hpage) >> PAGE_SHIFT;
		}
	}

	if (!imu->acct_pages)
		return 0;

	ret = io_account_mem(ctx, imu->acct_pages);
	if (ret)
		imu->acct_pages = 0;
	return ret;
}

struct page **io_pin_pages(unsigned long ubuf, unsigned long len, int *npages)
{
	unsigned long start, end, nr_pages;
	struct vm_area_struct **vmas = NULL;
	struct page **pages = NULL;
	int i, pret, ret = -ENOMEM;

	end = (ubuf + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	start = ubuf >> PAGE_SHIFT;
	nr_pages = end - start;

	pages = kvmalloc_array(nr_pages, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		goto done;

	vmas = kvmalloc_array(nr_pages, sizeof(struct vm_area_struct *),
			      GFP_KERNEL);
	if (!vmas)
		goto done;

	ret = 0;
	mmap_read_lock(current->mm);
	pret = pin_user_pages(ubuf, nr_pages, FOLL_WRITE | FOLL_LONGTERM,
			      pages, vmas);
	if (pret == nr_pages) {
		struct file *file = vmas[0]->vm_file;

		/* don't support file backed memory */
		for (i = 0; i < nr_pages; i++) {
			if (vmas[i]->vm_file != file) {
				ret = -EINVAL;
				break;
			}
			if (!file)
				continue;
			if (!vma_is_shmem(vmas[i]) && !is_file_hugepages(file)) {
				ret = -EOPNOTSUPP;
				break;
			}
		}
		*npages = nr_pages;
	} else {
		ret = pret < 0 ? pret : -EFAULT;
	}
	mmap_read_unlock(current->mm);
	if (ret) {
		/*
		 * if we did partial map, or found file backed vmas,
		 * release any pages we did get
		 */
		if (pret > 0)
			unpin_user_pages(pages, pret);
		goto done;
	}
	ret = 0;
done:
	kvfree(vmas);
	if (ret < 0) {
		kvfree(pages);
		pages = ERR_PTR(ret);
	}
	return pages;
}

static int io_sqe_buffer_register(struct io_ring_ctx *ctx, struct iovec *iov,
				  struct io_mapped_ubuf **pimu,
				  struct page **last_hpage)
{
	struct io_mapped_ubuf *imu = NULL;
	struct page **pages = NULL;
	unsigned long off;
	size_t size;
	int ret, nr_pages, i;

	*pimu = ctx->dummy_ubuf;
	if (!iov->iov_base)
		return 0;

	ret = -ENOMEM;
	pages = io_pin_pages((unsigned long) iov->iov_base, iov->iov_len,
				&nr_pages);
	if (IS_ERR(pages)) {
		ret = PTR_ERR(pages);
		pages = NULL;
		goto done;
	}

	imu = kvmalloc(struct_size(imu, bvec, nr_pages), GFP_KERNEL);
	if (!imu)
		goto done;

	ret = io_buffer_account_pin(ctx, pages, nr_pages, imu, last_hpage);
	if (ret) {
		unpin_user_pages(pages, nr_pages);
		goto done;
	}

	off = (unsigned long) iov->iov_base & ~PAGE_MASK;
	size = iov->iov_len;
	for (i = 0; i < nr_pages; i++) {
		size_t vec_len;

		vec_len = min_t(size_t, size, PAGE_SIZE - off);
		imu->bvec[i].bv_page = pages[i];
		imu->bvec[i].bv_len = vec_len;
		imu->bvec[i].bv_offset = off;
		off = 0;
		size -= vec_len;
	}
	/* store original address for later verification */
	imu->ubuf = (unsigned long) iov->iov_base;
	imu->ubuf_end = imu->ubuf + iov->iov_len;
	imu->nr_bvecs = nr_pages;
	*pimu = imu;
	ret = 0;
done:
	if (ret)
		kvfree(imu);
	kvfree(pages);
	return ret;
}

static int io_buffers_map_alloc(struct io_ring_ctx *ctx, unsigned int nr_args)
{
	ctx->user_bufs = kcalloc(nr_args, sizeof(*ctx->user_bufs), GFP_KERNEL);
	return ctx->user_bufs ? 0 : -ENOMEM;
}

int io_sqe_buffers_register(struct io_ring_ctx *ctx, void __user *arg,
			    unsigned int nr_args, u64 __user *tags)
{
	struct page *last_hpage = NULL;
	struct io_rsrc_data *data;
	int i, ret;
	struct iovec iov;

	BUILD_BUG_ON(IORING_MAX_REG_BUFFERS >= (1u << 16));

	if (ctx->user_bufs)
		return -EBUSY;
	if (!nr_args || nr_args > IORING_MAX_REG_BUFFERS)
		return -EINVAL;
	ret = io_rsrc_node_switch_start(ctx);
	if (ret)
		return ret;
	ret = io_rsrc_data_alloc(ctx, io_rsrc_buf_put, tags, nr_args, &data);
	if (ret)
		return ret;
	ret = io_buffers_map_alloc(ctx, nr_args);
	if (ret) {
		io_rsrc_data_free(data);
		return ret;
	}

	for (i = 0; i < nr_args; i++, ctx->nr_user_bufs++) {
		if (arg) {
			ret = io_copy_iov(ctx, &iov, arg, i);
			if (ret)
				break;
			ret = io_buffer_validate(&iov);
			if (ret)
				break;
		} else {
			memset(&iov, 0, sizeof(iov));
		}

		if (!iov.iov_base && *io_get_tag_slot(data, i)) {
			ret = -EINVAL;
			break;
		}

		ret = io_sqe_buffer_register(ctx, &iov, &ctx->user_bufs[i],
					     &last_hpage);
		if (ret)
			break;
	}

	WARN_ON_ONCE(ctx->buf_data);

	ctx->buf_data = data;
	if (ret)
		__io_sqe_buffers_unregister(ctx);
	else
		io_rsrc_node_switch(ctx, NULL);
	return ret;
}

int io_import_fixed(int ddir, struct iov_iter *iter,
			   struct io_mapped_ubuf *imu,
			   u64 buf_addr, size_t len)
{
	u64 buf_end;
	size_t offset;

	if (WARN_ON_ONCE(!imu))
		return -EFAULT;
	if (unlikely(check_add_overflow(buf_addr, (u64)len, &buf_end)))
		return -EFAULT;
	/* not inside the mapped region */
	if (unlikely(buf_addr < imu->ubuf || buf_end > imu->ubuf_end))
		return -EFAULT;

	/*
	 * May not be a start of buffer, set size appropriately
	 * and advance us to the beginning.
	 */
	offset = buf_addr - imu->ubuf;
	iov_iter_bvec(iter, ddir, imu->bvec, imu->nr_bvecs, offset + len);

	if (offset) {
		/*
		 * Don't use iov_iter_advance() here, as it's really slow for
		 * using the latter parts of a big fixed buffer - it iterates
		 * over each segment manually. We can cheat a bit here, because
		 * we know that:
		 *
		 * 1) it's a BVEC iter, we set it up
		 * 2) all bvecs are PAGE_SIZE in size, except potentially the
		 *    first and last bvec
		 *
		 * So just find our index, and adjust the iterator afterwards.
		 * If the offset is within the first bvec (or the whole first
		 * bvec, just use iov_iter_advance(). This makes it easier
		 * since we can just skip the first segment, which may not
		 * be PAGE_SIZE aligned.
		 */
		const struct bio_vec *bvec = imu->bvec;

		if (offset <= bvec->bv_len) {
			iov_iter_advance(iter, offset);
		} else {
			unsigned long seg_skip;

			/* skip first vec */
			offset -= bvec->bv_len;
			seg_skip = 1 + (offset >> PAGE_SHIFT);

			iter->bvec = bvec + seg_skip;
			iter->nr_segs -= seg_skip;
			iter->count -= bvec->bv_len + offset;
			iter->iov_offset = offset & ~PAGE_MASK;
		}
	}

	return 0;
}
