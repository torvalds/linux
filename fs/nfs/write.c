/*
 * linux/fs/nfs/write.c
 *
 * Write file data over NFS.
 *
 * Copyright (C) 1996, 1997, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/writeback.h>
#include <linux/swap.h>

#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs_page.h>
#include <linux/backing-dev.h>

#include <asm/uaccess.h>

#include "delegation.h"
#include "internal.h"
#include "iostat.h"

#define NFSDBG_FACILITY		NFSDBG_PAGECACHE

#define MIN_POOL_WRITE		(32)
#define MIN_POOL_COMMIT		(4)

/*
 * Local function declarations
 */
static struct nfs_page * nfs_update_request(struct nfs_open_context*,
					    struct page *,
					    unsigned int, unsigned int);
static void nfs_pageio_init_write(struct nfs_pageio_descriptor *desc,
				  struct inode *inode, int ioflags);
static const struct rpc_call_ops nfs_write_partial_ops;
static const struct rpc_call_ops nfs_write_full_ops;
static const struct rpc_call_ops nfs_commit_ops;

static struct kmem_cache *nfs_wdata_cachep;
static mempool_t *nfs_wdata_mempool;
static mempool_t *nfs_commit_mempool;

struct nfs_write_data *nfs_commit_alloc(void)
{
	struct nfs_write_data *p = mempool_alloc(nfs_commit_mempool, GFP_NOFS);

	if (p) {
		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&p->pages);
	}
	return p;
}

void nfs_commit_rcu_free(struct rcu_head *head)
{
	struct nfs_write_data *p = container_of(head, struct nfs_write_data, task.u.tk_rcu);
	if (p && (p->pagevec != &p->page_array[0]))
		kfree(p->pagevec);
	mempool_free(p, nfs_commit_mempool);
}

void nfs_commit_free(struct nfs_write_data *wdata)
{
	call_rcu_bh(&wdata->task.u.tk_rcu, nfs_commit_rcu_free);
}

struct nfs_write_data *nfs_writedata_alloc(unsigned int pagecount)
{
	struct nfs_write_data *p = mempool_alloc(nfs_wdata_mempool, GFP_NOFS);

	if (p) {
		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&p->pages);
		p->npages = pagecount;
		if (pagecount <= ARRAY_SIZE(p->page_array))
			p->pagevec = p->page_array;
		else {
			p->pagevec = kcalloc(pagecount, sizeof(struct page *), GFP_NOFS);
			if (!p->pagevec) {
				mempool_free(p, nfs_wdata_mempool);
				p = NULL;
			}
		}
	}
	return p;
}

static void nfs_writedata_rcu_free(struct rcu_head *head)
{
	struct nfs_write_data *p = container_of(head, struct nfs_write_data, task.u.tk_rcu);
	if (p && (p->pagevec != &p->page_array[0]))
		kfree(p->pagevec);
	mempool_free(p, nfs_wdata_mempool);
}

static void nfs_writedata_free(struct nfs_write_data *wdata)
{
	call_rcu_bh(&wdata->task.u.tk_rcu, nfs_writedata_rcu_free);
}

void nfs_writedata_release(void *wdata)
{
	nfs_writedata_free(wdata);
}

static struct nfs_page *nfs_page_find_request_locked(struct page *page)
{
	struct nfs_page *req = NULL;

	if (PagePrivate(page)) {
		req = (struct nfs_page *)page_private(page);
		if (req != NULL)
			atomic_inc(&req->wb_count);
	}
	return req;
}

static struct nfs_page *nfs_page_find_request(struct page *page)
{
	struct nfs_page *req = NULL;
	spinlock_t *req_lock = &NFS_I(page->mapping->host)->req_lock;

	spin_lock(req_lock);
	req = nfs_page_find_request_locked(page);
	spin_unlock(req_lock);
	return req;
}

/* Adjust the file length if we're writing beyond the end */
static void nfs_grow_file(struct page *page, unsigned int offset, unsigned int count)
{
	struct inode *inode = page->mapping->host;
	loff_t end, i_size = i_size_read(inode);
	pgoff_t end_index = (i_size - 1) >> PAGE_CACHE_SHIFT;

	if (i_size > 0 && page->index < end_index)
		return;
	end = ((loff_t)page->index << PAGE_CACHE_SHIFT) + ((loff_t)offset+count);
	if (i_size >= end)
		return;
	nfs_inc_stats(inode, NFSIOS_EXTENDWRITE);
	i_size_write(inode, end);
}

/* A writeback failed: mark the page as bad, and invalidate the page cache */
static void nfs_set_pageerror(struct page *page)
{
	SetPageError(page);
	nfs_zap_mapping(page->mapping->host, page->mapping);
}

/* We can set the PG_uptodate flag if we see that a write request
 * covers the full page.
 */
static void nfs_mark_uptodate(struct page *page, unsigned int base, unsigned int count)
{
	if (PageUptodate(page))
		return;
	if (base != 0)
		return;
	if (count != nfs_page_length(page))
		return;
	if (count != PAGE_CACHE_SIZE)
		memclear_highpage_flush(page, count, PAGE_CACHE_SIZE - count);
	SetPageUptodate(page);
}

static int nfs_writepage_setup(struct nfs_open_context *ctx, struct page *page,
		unsigned int offset, unsigned int count)
{
	struct nfs_page	*req;
	int ret;

	for (;;) {
		req = nfs_update_request(ctx, page, offset, count);
		if (!IS_ERR(req))
			break;
		ret = PTR_ERR(req);
		if (ret != -EBUSY)
			return ret;
		ret = nfs_wb_page(page->mapping->host, page);
		if (ret != 0)
			return ret;
	}
	/* Update file length */
	nfs_grow_file(page, offset, count);
	/* Set the PG_uptodate flag? */
	nfs_mark_uptodate(page, offset, count);
	nfs_unlock_request(req);
	return 0;
}

static int wb_priority(struct writeback_control *wbc)
{
	if (wbc->for_reclaim)
		return FLUSH_HIGHPRI | FLUSH_STABLE;
	if (wbc->for_kupdate)
		return FLUSH_LOWPRI;
	return 0;
}

/*
 * NFS congestion control
 */

int nfs_congestion_kb;

#define NFS_CONGESTION_ON_THRESH 	(nfs_congestion_kb >> (PAGE_SHIFT-10))
#define NFS_CONGESTION_OFF_THRESH	\
	(NFS_CONGESTION_ON_THRESH - (NFS_CONGESTION_ON_THRESH >> 2))

static int nfs_set_page_writeback(struct page *page)
{
	int ret = test_set_page_writeback(page);

	if (!ret) {
		struct inode *inode = page->mapping->host;
		struct nfs_server *nfss = NFS_SERVER(inode);

		if (atomic_inc_return(&nfss->writeback) >
				NFS_CONGESTION_ON_THRESH)
			set_bdi_congested(&nfss->backing_dev_info, WRITE);
	}
	return ret;
}

static void nfs_end_page_writeback(struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct nfs_server *nfss = NFS_SERVER(inode);

	end_page_writeback(page);
	if (atomic_dec_return(&nfss->writeback) < NFS_CONGESTION_OFF_THRESH) {
		clear_bdi_congested(&nfss->backing_dev_info, WRITE);
		congestion_end(WRITE);
	}
}

/*
 * Find an associated nfs write request, and prepare to flush it out
 * Returns 1 if there was no write request, or if the request was
 * already tagged by nfs_set_page_dirty.Returns 0 if the request
 * was not tagged.
 * May also return an error if the user signalled nfs_wait_on_request().
 */
static int nfs_page_async_flush(struct nfs_pageio_descriptor *pgio,
				struct page *page)
{
	struct nfs_page *req;
	struct nfs_inode *nfsi = NFS_I(page->mapping->host);
	spinlock_t *req_lock = &nfsi->req_lock;
	int ret;

	spin_lock(req_lock);
	for(;;) {
		req = nfs_page_find_request_locked(page);
		if (req == NULL) {
			spin_unlock(req_lock);
			return 1;
		}
		if (nfs_lock_request_dontget(req))
			break;
		/* Note: If we hold the page lock, as is the case in nfs_writepage,
		 *	 then the call to nfs_lock_request_dontget() will always
		 *	 succeed provided that someone hasn't already marked the
		 *	 request as dirty (in which case we don't care).
		 */
		spin_unlock(req_lock);
		/* Prevent deadlock! */
		nfs_pageio_complete(pgio);
		ret = nfs_wait_on_request(req);
		nfs_release_request(req);
		if (ret != 0)
			return ret;
		spin_lock(req_lock);
	}
	if (test_bit(PG_NEED_COMMIT, &req->wb_flags)) {
		/* This request is marked for commit */
		spin_unlock(req_lock);
		nfs_unlock_request(req);
		nfs_pageio_complete(pgio);
		return 1;
	}
	if (nfs_set_page_writeback(page) != 0) {
		spin_unlock(req_lock);
		BUG();
	}
	radix_tree_tag_set(&nfsi->nfs_page_tree, req->wb_index,
			NFS_PAGE_TAG_WRITEBACK);
	ret = test_bit(PG_NEED_FLUSH, &req->wb_flags);
	spin_unlock(req_lock);
	nfs_pageio_add_request(pgio, req);
	return ret;
}

/*
 * Write an mmapped page to the server.
 */
static int nfs_writepage_locked(struct page *page, struct writeback_control *wbc)
{
	struct nfs_pageio_descriptor mypgio, *pgio;
	struct nfs_open_context *ctx;
	struct inode *inode = page->mapping->host;
	unsigned offset;
	int err;

	nfs_inc_stats(inode, NFSIOS_VFSWRITEPAGE);
	nfs_add_stats(inode, NFSIOS_WRITEPAGES, 1);

	if (wbc->for_writepages)
		pgio = wbc->fs_private;
	else {
		nfs_pageio_init_write(&mypgio, inode, wb_priority(wbc));
		pgio = &mypgio;
	}

	err = nfs_page_async_flush(pgio, page);
	if (err <= 0)
		goto out;
	err = 0;
	offset = nfs_page_length(page);
	if (!offset)
		goto out;

	ctx = nfs_find_open_context(inode, NULL, FMODE_WRITE);
	if (ctx == NULL) {
		err = -EBADF;
		goto out;
	}
	err = nfs_writepage_setup(ctx, page, 0, offset);
	put_nfs_open_context(ctx);
	if (err != 0)
		goto out;
	err = nfs_page_async_flush(pgio, page);
	if (err > 0)
		err = 0;
out:
	if (!wbc->for_writepages)
		nfs_pageio_complete(pgio);
	return err;
}

int nfs_writepage(struct page *page, struct writeback_control *wbc)
{
	int err;

	err = nfs_writepage_locked(page, wbc);
	unlock_page(page);
	return err; 
}

int nfs_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	struct nfs_pageio_descriptor pgio;
	int err;

	nfs_inc_stats(inode, NFSIOS_VFSWRITEPAGES);

	nfs_pageio_init_write(&pgio, inode, wb_priority(wbc));
	wbc->fs_private = &pgio;
	err = generic_writepages(mapping, wbc);
	nfs_pageio_complete(&pgio);
	if (err)
		return err;
	if (pgio.pg_error)
		return pgio.pg_error;
	return 0;
}

/*
 * Insert a write request into an inode
 */
static int nfs_inode_add_request(struct inode *inode, struct nfs_page *req)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int error;

	error = radix_tree_insert(&nfsi->nfs_page_tree, req->wb_index, req);
	BUG_ON(error == -EEXIST);
	if (error)
		return error;
	if (!nfsi->npages) {
		igrab(inode);
		nfs_begin_data_update(inode);
		if (nfs_have_delegation(inode, FMODE_WRITE))
			nfsi->change_attr++;
	}
	SetPagePrivate(req->wb_page);
	set_page_private(req->wb_page, (unsigned long)req);
	if (PageDirty(req->wb_page))
		set_bit(PG_NEED_FLUSH, &req->wb_flags);
	nfsi->npages++;
	atomic_inc(&req->wb_count);
	return 0;
}

/*
 * Remove a write request from an inode
 */
static void nfs_inode_remove_request(struct nfs_page *req)
{
	struct inode *inode = req->wb_context->dentry->d_inode;
	struct nfs_inode *nfsi = NFS_I(inode);

	BUG_ON (!NFS_WBACK_BUSY(req));

	spin_lock(&nfsi->req_lock);
	set_page_private(req->wb_page, 0);
	ClearPagePrivate(req->wb_page);
	radix_tree_delete(&nfsi->nfs_page_tree, req->wb_index);
	if (test_and_clear_bit(PG_NEED_FLUSH, &req->wb_flags))
		__set_page_dirty_nobuffers(req->wb_page);
	nfsi->npages--;
	if (!nfsi->npages) {
		spin_unlock(&nfsi->req_lock);
		nfs_end_data_update(inode);
		iput(inode);
	} else
		spin_unlock(&nfsi->req_lock);
	nfs_clear_request(req);
	nfs_release_request(req);
}

static void
nfs_redirty_request(struct nfs_page *req)
{
	__set_page_dirty_nobuffers(req->wb_page);
}

/*
 * Check if a request is dirty
 */
static inline int
nfs_dirty_request(struct nfs_page *req)
{
	struct page *page = req->wb_page;

	if (page == NULL || test_bit(PG_NEED_COMMIT, &req->wb_flags))
		return 0;
	return !PageWriteback(req->wb_page);
}

#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
/*
 * Add a request to the inode's commit list.
 */
static void
nfs_mark_request_commit(struct nfs_page *req)
{
	struct inode *inode = req->wb_context->dentry->d_inode;
	struct nfs_inode *nfsi = NFS_I(inode);

	spin_lock(&nfsi->req_lock);
	nfs_list_add_request(req, &nfsi->commit);
	nfsi->ncommit++;
	set_bit(PG_NEED_COMMIT, &(req)->wb_flags);
	spin_unlock(&nfsi->req_lock);
	inc_zone_page_state(req->wb_page, NR_UNSTABLE_NFS);
	__mark_inode_dirty(inode, I_DIRTY_DATASYNC);
}

static inline
int nfs_write_need_commit(struct nfs_write_data *data)
{
	return data->verf.committed != NFS_FILE_SYNC;
}

static inline
int nfs_reschedule_unstable_write(struct nfs_page *req)
{
	if (test_bit(PG_NEED_COMMIT, &req->wb_flags)) {
		nfs_mark_request_commit(req);
		return 1;
	}
	if (test_and_clear_bit(PG_NEED_RESCHED, &req->wb_flags)) {
		nfs_redirty_request(req);
		return 1;
	}
	return 0;
}
#else
static inline void
nfs_mark_request_commit(struct nfs_page *req)
{
}

static inline
int nfs_write_need_commit(struct nfs_write_data *data)
{
	return 0;
}

static inline
int nfs_reschedule_unstable_write(struct nfs_page *req)
{
	return 0;
}
#endif

/*
 * Wait for a request to complete.
 *
 * Interruptible by signals only if mounted with intr flag.
 */
static int nfs_wait_on_requests_locked(struct inode *inode, pgoff_t idx_start, unsigned int npages)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_page *req;
	pgoff_t idx_end, next;
	unsigned int		res = 0;
	int			error;

	if (npages == 0)
		idx_end = ~0;
	else
		idx_end = idx_start + npages - 1;

	next = idx_start;
	while (radix_tree_gang_lookup_tag(&nfsi->nfs_page_tree, (void **)&req, next, 1, NFS_PAGE_TAG_WRITEBACK)) {
		if (req->wb_index > idx_end)
			break;

		next = req->wb_index + 1;
		BUG_ON(!NFS_WBACK_BUSY(req));

		atomic_inc(&req->wb_count);
		spin_unlock(&nfsi->req_lock);
		error = nfs_wait_on_request(req);
		nfs_release_request(req);
		spin_lock(&nfsi->req_lock);
		if (error < 0)
			return error;
		res++;
	}
	return res;
}

static void nfs_cancel_commit_list(struct list_head *head)
{
	struct nfs_page *req;

	while(!list_empty(head)) {
		req = nfs_list_entry(head->next);
		dec_zone_page_state(req->wb_page, NR_UNSTABLE_NFS);
		nfs_list_remove_request(req);
		clear_bit(PG_NEED_COMMIT, &(req)->wb_flags);
		nfs_inode_remove_request(req);
		nfs_unlock_request(req);
	}
}

#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
/*
 * nfs_scan_commit - Scan an inode for commit requests
 * @inode: NFS inode to scan
 * @dst: destination list
 * @idx_start: lower bound of page->index to scan.
 * @npages: idx_start + npages sets the upper bound to scan.
 *
 * Moves requests from the inode's 'commit' request list.
 * The requests are *not* checked to ensure that they form a contiguous set.
 */
static int
nfs_scan_commit(struct inode *inode, struct list_head *dst, pgoff_t idx_start, unsigned int npages)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int res = 0;

	if (nfsi->ncommit != 0) {
		res = nfs_scan_list(nfsi, &nfsi->commit, dst, idx_start, npages);
		nfsi->ncommit -= res;
		if ((nfsi->ncommit == 0) != list_empty(&nfsi->commit))
			printk(KERN_ERR "NFS: desynchronized value of nfs_i.ncommit.\n");
	}
	return res;
}
#else
static inline int nfs_scan_commit(struct inode *inode, struct list_head *dst, pgoff_t idx_start, unsigned int npages)
{
	return 0;
}
#endif

/*
 * Try to update any existing write request, or create one if there is none.
 * In order to match, the request's credentials must match those of
 * the calling process.
 *
 * Note: Should always be called with the Page Lock held!
 */
static struct nfs_page * nfs_update_request(struct nfs_open_context* ctx,
		struct page *page, unsigned int offset, unsigned int bytes)
{
	struct address_space *mapping = page->mapping;
	struct inode *inode = mapping->host;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_page		*req, *new = NULL;
	pgoff_t		rqend, end;

	end = offset + bytes;

	for (;;) {
		/* Loop over all inode entries and see if we find
		 * A request for the page we wish to update
		 */
		spin_lock(&nfsi->req_lock);
		req = nfs_page_find_request_locked(page);
		if (req) {
			if (!nfs_lock_request_dontget(req)) {
				int error;

				spin_unlock(&nfsi->req_lock);
				error = nfs_wait_on_request(req);
				nfs_release_request(req);
				if (error < 0) {
					if (new)
						nfs_release_request(new);
					return ERR_PTR(error);
				}
				continue;
			}
			spin_unlock(&nfsi->req_lock);
			if (new)
				nfs_release_request(new);
			break;
		}

		if (new) {
			int error;
			nfs_lock_request_dontget(new);
			error = nfs_inode_add_request(inode, new);
			if (error) {
				spin_unlock(&nfsi->req_lock);
				nfs_unlock_request(new);
				return ERR_PTR(error);
			}
			spin_unlock(&nfsi->req_lock);
			return new;
		}
		spin_unlock(&nfsi->req_lock);

		new = nfs_create_request(ctx, inode, page, offset, bytes);
		if (IS_ERR(new))
			return new;
	}

	/* We have a request for our page.
	 * If the creds don't match, or the
	 * page addresses don't match,
	 * tell the caller to wait on the conflicting
	 * request.
	 */
	rqend = req->wb_offset + req->wb_bytes;
	if (req->wb_context != ctx
	    || req->wb_page != page
	    || !nfs_dirty_request(req)
	    || offset > rqend || end < req->wb_offset) {
		nfs_unlock_request(req);
		return ERR_PTR(-EBUSY);
	}

	/* Okay, the request matches. Update the region */
	if (offset < req->wb_offset) {
		req->wb_offset = offset;
		req->wb_pgbase = offset;
		req->wb_bytes = rqend - req->wb_offset;
	}

	if (end > rqend)
		req->wb_bytes = end - req->wb_offset;

	return req;
}

int nfs_flush_incompatible(struct file *file, struct page *page)
{
	struct nfs_open_context *ctx = (struct nfs_open_context *)file->private_data;
	struct nfs_page	*req;
	int do_flush, status;
	/*
	 * Look for a request corresponding to this page. If there
	 * is one, and it belongs to another file, we flush it out
	 * before we try to copy anything into the page. Do this
	 * due to the lack of an ACCESS-type call in NFSv2.
	 * Also do the same if we find a request from an existing
	 * dropped page.
	 */
	do {
		req = nfs_page_find_request(page);
		if (req == NULL)
			return 0;
		do_flush = req->wb_page != page || req->wb_context != ctx
			|| !nfs_dirty_request(req);
		nfs_release_request(req);
		if (!do_flush)
			return 0;
		status = nfs_wb_page(page->mapping->host, page);
	} while (status == 0);
	return status;
}

/*
 * Update and possibly write a cached page of an NFS file.
 *
 * XXX: Keep an eye on generic_file_read to make sure it doesn't do bad
 * things with a page scheduled for an RPC call (e.g. invalidate it).
 */
int nfs_updatepage(struct file *file, struct page *page,
		unsigned int offset, unsigned int count)
{
	struct nfs_open_context *ctx = (struct nfs_open_context *)file->private_data;
	struct inode	*inode = page->mapping->host;
	int		status = 0;

	nfs_inc_stats(inode, NFSIOS_VFSUPDATEPAGE);

	dprintk("NFS:      nfs_updatepage(%s/%s %d@%Ld)\n",
		file->f_path.dentry->d_parent->d_name.name,
		file->f_path.dentry->d_name.name, count,
		(long long)(page_offset(page) +offset));

	/* If we're not using byte range locks, and we know the page
	 * is entirely in cache, it may be more efficient to avoid
	 * fragmenting write requests.
	 */
	if (PageUptodate(page) && inode->i_flock == NULL && !(file->f_mode & O_SYNC)) {
		count = max(count + offset, nfs_page_length(page));
		offset = 0;
	}

	status = nfs_writepage_setup(ctx, page, offset, count);
	__set_page_dirty_nobuffers(page);

        dprintk("NFS:      nfs_updatepage returns %d (isize %Ld)\n",
			status, (long long)i_size_read(inode));
	if (status < 0)
		nfs_set_pageerror(page);
	return status;
}

static void nfs_writepage_release(struct nfs_page *req)
{

	if (PageError(req->wb_page) || !nfs_reschedule_unstable_write(req)) {
		nfs_end_page_writeback(req->wb_page);
		nfs_inode_remove_request(req);
	} else
		nfs_end_page_writeback(req->wb_page);
	nfs_clear_page_writeback(req);
}

static inline int flush_task_priority(int how)
{
	switch (how & (FLUSH_HIGHPRI|FLUSH_LOWPRI)) {
		case FLUSH_HIGHPRI:
			return RPC_PRIORITY_HIGH;
		case FLUSH_LOWPRI:
			return RPC_PRIORITY_LOW;
	}
	return RPC_PRIORITY_NORMAL;
}

/*
 * Set up the argument/result storage required for the RPC call.
 */
static void nfs_write_rpcsetup(struct nfs_page *req,
		struct nfs_write_data *data,
		const struct rpc_call_ops *call_ops,
		unsigned int count, unsigned int offset,
		int how)
{
	struct inode		*inode;
	int flags;

	/* Set up the RPC argument and reply structs
	 * NB: take care not to mess about with data->commit et al. */

	data->req = req;
	data->inode = inode = req->wb_context->dentry->d_inode;
	data->cred = req->wb_context->cred;

	data->args.fh     = NFS_FH(inode);
	data->args.offset = req_offset(req) + offset;
	data->args.pgbase = req->wb_pgbase + offset;
	data->args.pages  = data->pagevec;
	data->args.count  = count;
	data->args.context = req->wb_context;

	data->res.fattr   = &data->fattr;
	data->res.count   = count;
	data->res.verf    = &data->verf;
	nfs_fattr_init(&data->fattr);

	/* Set up the initial task struct.  */
	flags = (how & FLUSH_SYNC) ? 0 : RPC_TASK_ASYNC;
	rpc_init_task(&data->task, NFS_CLIENT(inode), flags, call_ops, data);
	NFS_PROTO(inode)->write_setup(data, how);

	data->task.tk_priority = flush_task_priority(how);
	data->task.tk_cookie = (unsigned long)inode;

	dprintk("NFS: %5u initiated write call "
		"(req %s/%Ld, %u bytes @ offset %Lu)\n",
		data->task.tk_pid,
		inode->i_sb->s_id,
		(long long)NFS_FILEID(inode),
		count,
		(unsigned long long)data->args.offset);
}

static void nfs_execute_write(struct nfs_write_data *data)
{
	struct rpc_clnt *clnt = NFS_CLIENT(data->inode);
	sigset_t oldset;

	rpc_clnt_sigmask(clnt, &oldset);
	rpc_execute(&data->task);
	rpc_clnt_sigunmask(clnt, &oldset);
}

/*
 * Generate multiple small requests to write out a single
 * contiguous dirty area on one page.
 */
static int nfs_flush_multi(struct inode *inode, struct list_head *head, unsigned int npages, size_t count, int how)
{
	struct nfs_page *req = nfs_list_entry(head->next);
	struct page *page = req->wb_page;
	struct nfs_write_data *data;
	size_t wsize = NFS_SERVER(inode)->wsize, nbytes;
	unsigned int offset;
	int requests = 0;
	LIST_HEAD(list);

	nfs_list_remove_request(req);

	nbytes = count;
	do {
		size_t len = min(nbytes, wsize);

		data = nfs_writedata_alloc(1);
		if (!data)
			goto out_bad;
		list_add(&data->pages, &list);
		requests++;
		nbytes -= len;
	} while (nbytes != 0);
	atomic_set(&req->wb_complete, requests);

	ClearPageError(page);
	offset = 0;
	nbytes = count;
	do {
		data = list_entry(list.next, struct nfs_write_data, pages);
		list_del_init(&data->pages);

		data->pagevec[0] = page;

		if (nbytes < wsize)
			wsize = nbytes;
		nfs_write_rpcsetup(req, data, &nfs_write_partial_ops,
				   wsize, offset, how);
		offset += wsize;
		nbytes -= wsize;
		nfs_execute_write(data);
	} while (nbytes != 0);

	return 0;

out_bad:
	while (!list_empty(&list)) {
		data = list_entry(list.next, struct nfs_write_data, pages);
		list_del(&data->pages);
		nfs_writedata_release(data);
	}
	nfs_redirty_request(req);
	nfs_end_page_writeback(req->wb_page);
	nfs_clear_page_writeback(req);
	return -ENOMEM;
}

/*
 * Create an RPC task for the given write request and kick it.
 * The page must have been locked by the caller.
 *
 * It may happen that the page we're passed is not marked dirty.
 * This is the case if nfs_updatepage detects a conflicting request
 * that has been written but not committed.
 */
static int nfs_flush_one(struct inode *inode, struct list_head *head, unsigned int npages, size_t count, int how)
{
	struct nfs_page		*req;
	struct page		**pages;
	struct nfs_write_data	*data;

	data = nfs_writedata_alloc(npages);
	if (!data)
		goto out_bad;

	pages = data->pagevec;
	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_list_add_request(req, &data->pages);
		ClearPageError(req->wb_page);
		*pages++ = req->wb_page;
	}
	req = nfs_list_entry(data->pages.next);

	/* Set up the argument struct */
	nfs_write_rpcsetup(req, data, &nfs_write_full_ops, count, 0, how);

	nfs_execute_write(data);
	return 0;
 out_bad:
	while (!list_empty(head)) {
		struct nfs_page *req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_redirty_request(req);
		nfs_end_page_writeback(req->wb_page);
		nfs_clear_page_writeback(req);
	}
	return -ENOMEM;
}

static void nfs_pageio_init_write(struct nfs_pageio_descriptor *pgio,
				  struct inode *inode, int ioflags)
{
	int wsize = NFS_SERVER(inode)->wsize;

	if (wsize < PAGE_CACHE_SIZE)
		nfs_pageio_init(pgio, inode, nfs_flush_multi, wsize, ioflags);
	else
		nfs_pageio_init(pgio, inode, nfs_flush_one, wsize, ioflags);
}

/*
 * Handle a write reply that flushed part of a page.
 */
static void nfs_writeback_done_partial(struct rpc_task *task, void *calldata)
{
	struct nfs_write_data	*data = calldata;
	struct nfs_page		*req = data->req;
	struct page		*page = req->wb_page;

	dprintk("NFS: write (%s/%Ld %d@%Ld)",
		req->wb_context->dentry->d_inode->i_sb->s_id,
		(long long)NFS_FILEID(req->wb_context->dentry->d_inode),
		req->wb_bytes,
		(long long)req_offset(req));

	if (nfs_writeback_done(task, data) != 0)
		return;

	if (task->tk_status < 0) {
		nfs_set_pageerror(page);
		req->wb_context->error = task->tk_status;
		dprintk(", error = %d\n", task->tk_status);
		goto out;
	}

	if (nfs_write_need_commit(data)) {
		spinlock_t *req_lock = &NFS_I(page->mapping->host)->req_lock;

		spin_lock(req_lock);
		if (test_bit(PG_NEED_RESCHED, &req->wb_flags)) {
			/* Do nothing we need to resend the writes */
		} else if (!test_and_set_bit(PG_NEED_COMMIT, &req->wb_flags)) {
			memcpy(&req->wb_verf, &data->verf, sizeof(req->wb_verf));
			dprintk(" defer commit\n");
		} else if (memcmp(&req->wb_verf, &data->verf, sizeof(req->wb_verf))) {
			set_bit(PG_NEED_RESCHED, &req->wb_flags);
			clear_bit(PG_NEED_COMMIT, &req->wb_flags);
			dprintk(" server reboot detected\n");
		}
		spin_unlock(req_lock);
	} else
		dprintk(" OK\n");

out:
	if (atomic_dec_and_test(&req->wb_complete))
		nfs_writepage_release(req);
}

static const struct rpc_call_ops nfs_write_partial_ops = {
	.rpc_call_done = nfs_writeback_done_partial,
	.rpc_release = nfs_writedata_release,
};

/*
 * Handle a write reply that flushes a whole page.
 *
 * FIXME: There is an inherent race with invalidate_inode_pages and
 *	  writebacks since the page->count is kept > 1 for as long
 *	  as the page has a write request pending.
 */
static void nfs_writeback_done_full(struct rpc_task *task, void *calldata)
{
	struct nfs_write_data	*data = calldata;
	struct nfs_page		*req;
	struct page		*page;

	if (nfs_writeback_done(task, data) != 0)
		return;

	/* Update attributes as result of writeback. */
	while (!list_empty(&data->pages)) {
		req = nfs_list_entry(data->pages.next);
		nfs_list_remove_request(req);
		page = req->wb_page;

		dprintk("NFS: write (%s/%Ld %d@%Ld)",
			req->wb_context->dentry->d_inode->i_sb->s_id,
			(long long)NFS_FILEID(req->wb_context->dentry->d_inode),
			req->wb_bytes,
			(long long)req_offset(req));

		if (task->tk_status < 0) {
			nfs_set_pageerror(page);
			req->wb_context->error = task->tk_status;
			dprintk(", error = %d\n", task->tk_status);
			goto remove_request;
		}

		if (nfs_write_need_commit(data)) {
			memcpy(&req->wb_verf, &data->verf, sizeof(req->wb_verf));
			nfs_mark_request_commit(req);
			nfs_end_page_writeback(page);
			dprintk(" marked for commit\n");
			goto next;
		}
		dprintk(" OK\n");
remove_request:
		nfs_end_page_writeback(page);
		nfs_inode_remove_request(req);
	next:
		nfs_clear_page_writeback(req);
	}
}

static const struct rpc_call_ops nfs_write_full_ops = {
	.rpc_call_done = nfs_writeback_done_full,
	.rpc_release = nfs_writedata_release,
};


/*
 * This function is called when the WRITE call is complete.
 */
int nfs_writeback_done(struct rpc_task *task, struct nfs_write_data *data)
{
	struct nfs_writeargs	*argp = &data->args;
	struct nfs_writeres	*resp = &data->res;
	int status;

	dprintk("NFS: %5u nfs_writeback_done (status %d)\n",
		task->tk_pid, task->tk_status);

	/*
	 * ->write_done will attempt to use post-op attributes to detect
	 * conflicting writes by other clients.  A strict interpretation
	 * of close-to-open would allow us to continue caching even if
	 * another writer had changed the file, but some applications
	 * depend on tighter cache coherency when writing.
	 */
	status = NFS_PROTO(data->inode)->write_done(task, data);
	if (status != 0)
		return status;
	nfs_add_stats(data->inode, NFSIOS_SERVERWRITTENBYTES, resp->count);

#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
	if (resp->verf->committed < argp->stable && task->tk_status >= 0) {
		/* We tried a write call, but the server did not
		 * commit data to stable storage even though we
		 * requested it.
		 * Note: There is a known bug in Tru64 < 5.0 in which
		 *	 the server reports NFS_DATA_SYNC, but performs
		 *	 NFS_FILE_SYNC. We therefore implement this checking
		 *	 as a dprintk() in order to avoid filling syslog.
		 */
		static unsigned long    complain;

		if (time_before(complain, jiffies)) {
			dprintk("NFS: faulty NFS server %s:"
				" (committed = %d) != (stable = %d)\n",
				NFS_SERVER(data->inode)->nfs_client->cl_hostname,
				resp->verf->committed, argp->stable);
			complain = jiffies + 300 * HZ;
		}
	}
#endif
	/* Is this a short write? */
	if (task->tk_status >= 0 && resp->count < argp->count) {
		static unsigned long    complain;

		nfs_inc_stats(data->inode, NFSIOS_SHORTWRITE);

		/* Has the server at least made some progress? */
		if (resp->count != 0) {
			/* Was this an NFSv2 write or an NFSv3 stable write? */
			if (resp->verf->committed != NFS_UNSTABLE) {
				/* Resend from where the server left off */
				argp->offset += resp->count;
				argp->pgbase += resp->count;
				argp->count -= resp->count;
			} else {
				/* Resend as a stable write in order to avoid
				 * headaches in the case of a server crash.
				 */
				argp->stable = NFS_FILE_SYNC;
			}
			rpc_restart_call(task);
			return -EAGAIN;
		}
		if (time_before(complain, jiffies)) {
			printk(KERN_WARNING
			       "NFS: Server wrote zero bytes, expected %u.\n",
					argp->count);
			complain = jiffies + 300 * HZ;
		}
		/* Can't do anything about it except throw an error. */
		task->tk_status = -EIO;
	}
	return 0;
}


#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
void nfs_commit_release(void *wdata)
{
	nfs_commit_free(wdata);
}

/*
 * Set up the argument/result storage required for the RPC call.
 */
static void nfs_commit_rpcsetup(struct list_head *head,
		struct nfs_write_data *data,
		int how)
{
	struct nfs_page		*first;
	struct inode		*inode;
	int flags;

	/* Set up the RPC argument and reply structs
	 * NB: take care not to mess about with data->commit et al. */

	list_splice_init(head, &data->pages);
	first = nfs_list_entry(data->pages.next);
	inode = first->wb_context->dentry->d_inode;

	data->inode	  = inode;
	data->cred	  = first->wb_context->cred;

	data->args.fh     = NFS_FH(data->inode);
	/* Note: we always request a commit of the entire inode */
	data->args.offset = 0;
	data->args.count  = 0;
	data->res.count   = 0;
	data->res.fattr   = &data->fattr;
	data->res.verf    = &data->verf;
	nfs_fattr_init(&data->fattr);

	/* Set up the initial task struct.  */
	flags = (how & FLUSH_SYNC) ? 0 : RPC_TASK_ASYNC;
	rpc_init_task(&data->task, NFS_CLIENT(inode), flags, &nfs_commit_ops, data);
	NFS_PROTO(inode)->commit_setup(data, how);

	data->task.tk_priority = flush_task_priority(how);
	data->task.tk_cookie = (unsigned long)inode;
	
	dprintk("NFS: %5u initiated commit call\n", data->task.tk_pid);
}

/*
 * Commit dirty pages
 */
static int
nfs_commit_list(struct inode *inode, struct list_head *head, int how)
{
	struct nfs_write_data	*data;
	struct nfs_page         *req;

	data = nfs_commit_alloc();

	if (!data)
		goto out_bad;

	/* Set up the argument struct */
	nfs_commit_rpcsetup(head, data, how);

	nfs_execute_write(data);
	return 0;
 out_bad:
	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_mark_request_commit(req);
		dec_zone_page_state(req->wb_page, NR_UNSTABLE_NFS);
		nfs_clear_page_writeback(req);
	}
	return -ENOMEM;
}

/*
 * COMMIT call returned
 */
static void nfs_commit_done(struct rpc_task *task, void *calldata)
{
	struct nfs_write_data	*data = calldata;
	struct nfs_page		*req;

        dprintk("NFS: %5u nfs_commit_done (status %d)\n",
                                task->tk_pid, task->tk_status);

	/* Call the NFS version-specific code */
	if (NFS_PROTO(data->inode)->commit_done(task, data) != 0)
		return;

	while (!list_empty(&data->pages)) {
		req = nfs_list_entry(data->pages.next);
		nfs_list_remove_request(req);
		clear_bit(PG_NEED_COMMIT, &(req)->wb_flags);
		dec_zone_page_state(req->wb_page, NR_UNSTABLE_NFS);

		dprintk("NFS: commit (%s/%Ld %d@%Ld)",
			req->wb_context->dentry->d_inode->i_sb->s_id,
			(long long)NFS_FILEID(req->wb_context->dentry->d_inode),
			req->wb_bytes,
			(long long)req_offset(req));
		if (task->tk_status < 0) {
			req->wb_context->error = task->tk_status;
			nfs_inode_remove_request(req);
			dprintk(", error = %d\n", task->tk_status);
			goto next;
		}

		/* Okay, COMMIT succeeded, apparently. Check the verifier
		 * returned by the server against all stored verfs. */
		if (!memcmp(req->wb_verf.verifier, data->verf.verifier, sizeof(data->verf.verifier))) {
			/* We have a match */
			nfs_inode_remove_request(req);
			dprintk(" OK\n");
			goto next;
		}
		/* We have a mismatch. Write the page again */
		dprintk(" mismatch\n");
		nfs_redirty_request(req);
	next:
		nfs_clear_page_writeback(req);
	}
}

static const struct rpc_call_ops nfs_commit_ops = {
	.rpc_call_done = nfs_commit_done,
	.rpc_release = nfs_commit_release,
};

int nfs_commit_inode(struct inode *inode, int how)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	LIST_HEAD(head);
	int res;

	spin_lock(&nfsi->req_lock);
	res = nfs_scan_commit(inode, &head, 0, 0);
	spin_unlock(&nfsi->req_lock);
	if (res) {
		int error = nfs_commit_list(inode, &head, how);
		if (error < 0)
			return error;
	}
	return res;
}
#else
static inline int nfs_commit_list(struct inode *inode, struct list_head *head, int how)
{
	return 0;
}
#endif

long nfs_sync_mapping_wait(struct address_space *mapping, struct writeback_control *wbc, int how)
{
	struct inode *inode = mapping->host;
	struct nfs_inode *nfsi = NFS_I(inode);
	pgoff_t idx_start, idx_end;
	unsigned int npages = 0;
	LIST_HEAD(head);
	int nocommit = how & FLUSH_NOCOMMIT;
	long pages, ret;

	/* FIXME */
	if (wbc->range_cyclic)
		idx_start = 0;
	else {
		idx_start = wbc->range_start >> PAGE_CACHE_SHIFT;
		idx_end = wbc->range_end >> PAGE_CACHE_SHIFT;
		if (idx_end > idx_start) {
			pgoff_t l_npages = 1 + idx_end - idx_start;
			npages = l_npages;
			if (sizeof(npages) != sizeof(l_npages) &&
					(pgoff_t)npages != l_npages)
				npages = 0;
		}
	}
	how &= ~FLUSH_NOCOMMIT;
	spin_lock(&nfsi->req_lock);
	do {
		ret = nfs_wait_on_requests_locked(inode, idx_start, npages);
		if (ret != 0)
			continue;
		if (nocommit)
			break;
		pages = nfs_scan_commit(inode, &head, idx_start, npages);
		if (pages == 0)
			break;
		if (how & FLUSH_INVALIDATE) {
			spin_unlock(&nfsi->req_lock);
			nfs_cancel_commit_list(&head);
			ret = pages;
			spin_lock(&nfsi->req_lock);
			continue;
		}
		pages += nfs_scan_commit(inode, &head, 0, 0);
		spin_unlock(&nfsi->req_lock);
		ret = nfs_commit_list(inode, &head, how);
		spin_lock(&nfsi->req_lock);
	} while (ret >= 0);
	spin_unlock(&nfsi->req_lock);
	return ret;
}

/*
 * flush the inode to disk.
 */
int nfs_wb_all(struct inode *inode)
{
	struct address_space *mapping = inode->i_mapping;
	struct writeback_control wbc = {
		.bdi = mapping->backing_dev_info,
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = LONG_MAX,
		.for_writepages = 1,
		.range_cyclic = 1,
	};
	int ret;

	ret = nfs_writepages(mapping, &wbc);
	if (ret < 0)
		goto out;
	ret = nfs_sync_mapping_wait(mapping, &wbc, 0);
	if (ret >= 0)
		return 0;
out:
	__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);
	return ret;
}

int nfs_sync_mapping_range(struct address_space *mapping, loff_t range_start, loff_t range_end, int how)
{
	struct writeback_control wbc = {
		.bdi = mapping->backing_dev_info,
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = LONG_MAX,
		.range_start = range_start,
		.range_end = range_end,
		.for_writepages = 1,
	};
	int ret;

	ret = nfs_writepages(mapping, &wbc);
	if (ret < 0)
		goto out;
	ret = nfs_sync_mapping_wait(mapping, &wbc, how);
	if (ret >= 0)
		return 0;
out:
	__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);
	return ret;
}

int nfs_wb_page_priority(struct inode *inode, struct page *page, int how)
{
	loff_t range_start = page_offset(page);
	loff_t range_end = range_start + (loff_t)(PAGE_CACHE_SIZE - 1);
	struct writeback_control wbc = {
		.bdi = page->mapping->backing_dev_info,
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = LONG_MAX,
		.range_start = range_start,
		.range_end = range_end,
	};
	int ret;

	BUG_ON(!PageLocked(page));
	if (clear_page_dirty_for_io(page)) {
		ret = nfs_writepage_locked(page, &wbc);
		if (ret < 0)
			goto out;
	}
	if (!PagePrivate(page))
		return 0;
	ret = nfs_sync_mapping_wait(page->mapping, &wbc, how);
	if (ret >= 0)
		return 0;
out:
	__mark_inode_dirty(inode, I_DIRTY_PAGES);
	return ret;
}

/*
 * Write back all requests on one page - we do this before reading it.
 */
int nfs_wb_page(struct inode *inode, struct page* page)
{
	return nfs_wb_page_priority(inode, page, FLUSH_STABLE);
}

int nfs_set_page_dirty(struct page *page)
{
	struct address_space *mapping = page->mapping;
	struct inode *inode;
	spinlock_t *req_lock;
	struct nfs_page *req;
	int ret;

	if (!mapping)
		goto out_raced;
	inode = mapping->host;
	if (!inode)
		goto out_raced;
	req_lock = &NFS_I(inode)->req_lock;
	spin_lock(req_lock);
	req = nfs_page_find_request_locked(page);
	if (req != NULL) {
		/* Mark any existing write requests for flushing */
		ret = !test_and_set_bit(PG_NEED_FLUSH, &req->wb_flags);
		spin_unlock(req_lock);
		nfs_release_request(req);
		return ret;
	}
	ret = __set_page_dirty_nobuffers(page);
	spin_unlock(req_lock);
	return ret;
out_raced:
	return !TestSetPageDirty(page);
}


int __init nfs_init_writepagecache(void)
{
	nfs_wdata_cachep = kmem_cache_create("nfs_write_data",
					     sizeof(struct nfs_write_data),
					     0, SLAB_HWCACHE_ALIGN,
					     NULL, NULL);
	if (nfs_wdata_cachep == NULL)
		return -ENOMEM;

	nfs_wdata_mempool = mempool_create_slab_pool(MIN_POOL_WRITE,
						     nfs_wdata_cachep);
	if (nfs_wdata_mempool == NULL)
		return -ENOMEM;

	nfs_commit_mempool = mempool_create_slab_pool(MIN_POOL_COMMIT,
						      nfs_wdata_cachep);
	if (nfs_commit_mempool == NULL)
		return -ENOMEM;

	/*
	 * NFS congestion size, scale with available memory.
	 *
	 *  64MB:    8192k
	 * 128MB:   11585k
	 * 256MB:   16384k
	 * 512MB:   23170k
	 *   1GB:   32768k
	 *   2GB:   46340k
	 *   4GB:   65536k
	 *   8GB:   92681k
	 *  16GB:  131072k
	 *
	 * This allows larger machines to have larger/more transfers.
	 * Limit the default to 256M
	 */
	nfs_congestion_kb = (16*int_sqrt(totalram_pages)) << (PAGE_SHIFT-10);
	if (nfs_congestion_kb > 256*1024)
		nfs_congestion_kb = 256*1024;

	return 0;
}

void nfs_destroy_writepagecache(void)
{
	mempool_destroy(nfs_commit_mempool);
	mempool_destroy(nfs_wdata_mempool);
	kmem_cache_destroy(nfs_wdata_cachep);
}

