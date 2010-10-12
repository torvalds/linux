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
#include <linux/migrate.h>

#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs_page.h>
#include <linux/backing-dev.h>

#include <asm/uaccess.h>

#include "delegation.h"
#include "internal.h"
#include "iostat.h"
#include "nfs4_fs.h"
#include "fscache.h"

#define NFSDBG_FACILITY		NFSDBG_PAGECACHE

#define MIN_POOL_WRITE		(32)
#define MIN_POOL_COMMIT		(4)

/*
 * Local function declarations
 */
static void nfs_pageio_init_write(struct nfs_pageio_descriptor *desc,
				  struct inode *inode, int ioflags);
static void nfs_redirty_request(struct nfs_page *req);
static const struct rpc_call_ops nfs_write_partial_ops;
static const struct rpc_call_ops nfs_write_full_ops;
static const struct rpc_call_ops nfs_commit_ops;

static struct kmem_cache *nfs_wdata_cachep;
static mempool_t *nfs_wdata_mempool;
static mempool_t *nfs_commit_mempool;

struct nfs_write_data *nfs_commitdata_alloc(void)
{
	struct nfs_write_data *p = mempool_alloc(nfs_commit_mempool, GFP_NOFS);

	if (p) {
		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&p->pages);
		p->res.seq_res.sr_slotid = NFS4_MAX_SLOT_TABLE;
	}
	return p;
}

void nfs_commit_free(struct nfs_write_data *p)
{
	if (p && (p->pagevec != &p->page_array[0]))
		kfree(p->pagevec);
	mempool_free(p, nfs_commit_mempool);
}

struct nfs_write_data *nfs_writedata_alloc(unsigned int pagecount)
{
	struct nfs_write_data *p = mempool_alloc(nfs_wdata_mempool, GFP_NOFS);

	if (p) {
		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&p->pages);
		p->npages = pagecount;
		p->res.seq_res.sr_slotid = NFS4_MAX_SLOT_TABLE;
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

void nfs_writedata_free(struct nfs_write_data *p)
{
	if (p && (p->pagevec != &p->page_array[0]))
		kfree(p->pagevec);
	mempool_free(p, nfs_wdata_mempool);
}

static void nfs_writedata_release(struct nfs_write_data *wdata)
{
	put_nfs_open_context(wdata->args.context);
	nfs_writedata_free(wdata);
}

static void nfs_context_set_write_error(struct nfs_open_context *ctx, int error)
{
	ctx->error = error;
	smp_wmb();
	set_bit(NFS_CONTEXT_ERROR_WRITE, &ctx->flags);
}

static struct nfs_page *nfs_page_find_request_locked(struct page *page)
{
	struct nfs_page *req = NULL;

	if (PagePrivate(page)) {
		req = (struct nfs_page *)page_private(page);
		if (req != NULL)
			kref_get(&req->wb_kref);
	}
	return req;
}

static struct nfs_page *nfs_page_find_request(struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct nfs_page *req = NULL;

	spin_lock(&inode->i_lock);
	req = nfs_page_find_request_locked(page);
	spin_unlock(&inode->i_lock);
	return req;
}

/* Adjust the file length if we're writing beyond the end */
static void nfs_grow_file(struct page *page, unsigned int offset, unsigned int count)
{
	struct inode *inode = page->mapping->host;
	loff_t end, i_size;
	pgoff_t end_index;

	spin_lock(&inode->i_lock);
	i_size = i_size_read(inode);
	end_index = (i_size - 1) >> PAGE_CACHE_SHIFT;
	if (i_size > 0 && page->index < end_index)
		goto out;
	end = ((loff_t)page->index << PAGE_CACHE_SHIFT) + ((loff_t)offset+count);
	if (i_size >= end)
		goto out;
	i_size_write(inode, end);
	nfs_inc_stats(inode, NFSIOS_EXTENDWRITE);
out:
	spin_unlock(&inode->i_lock);
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
	SetPageUptodate(page);
}

static int wb_priority(struct writeback_control *wbc)
{
	if (wbc->for_reclaim)
		return FLUSH_HIGHPRI | FLUSH_STABLE;
	if (wbc->for_kupdate || wbc->for_background)
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

		page_cache_get(page);
		if (atomic_long_inc_return(&nfss->writeback) >
				NFS_CONGESTION_ON_THRESH) {
			set_bdi_congested(&nfss->backing_dev_info,
						BLK_RW_ASYNC);
		}
	}
	return ret;
}

static void nfs_end_page_writeback(struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct nfs_server *nfss = NFS_SERVER(inode);

	end_page_writeback(page);
	page_cache_release(page);
	if (atomic_long_dec_return(&nfss->writeback) < NFS_CONGESTION_OFF_THRESH)
		clear_bdi_congested(&nfss->backing_dev_info, BLK_RW_ASYNC);
}

static struct nfs_page *nfs_find_and_lock_request(struct page *page, bool nonblock)
{
	struct inode *inode = page->mapping->host;
	struct nfs_page *req;
	int ret;

	spin_lock(&inode->i_lock);
	for (;;) {
		req = nfs_page_find_request_locked(page);
		if (req == NULL)
			break;
		if (nfs_set_page_tag_locked(req))
			break;
		/* Note: If we hold the page lock, as is the case in nfs_writepage,
		 *	 then the call to nfs_set_page_tag_locked() will always
		 *	 succeed provided that someone hasn't already marked the
		 *	 request as dirty (in which case we don't care).
		 */
		spin_unlock(&inode->i_lock);
		if (!nonblock)
			ret = nfs_wait_on_request(req);
		else
			ret = -EAGAIN;
		nfs_release_request(req);
		if (ret != 0)
			return ERR_PTR(ret);
		spin_lock(&inode->i_lock);
	}
	spin_unlock(&inode->i_lock);
	return req;
}

/*
 * Find an associated nfs write request, and prepare to flush it out
 * May return an error if the user signalled nfs_wait_on_request().
 */
static int nfs_page_async_flush(struct nfs_pageio_descriptor *pgio,
				struct page *page, bool nonblock)
{
	struct nfs_page *req;
	int ret = 0;

	req = nfs_find_and_lock_request(page, nonblock);
	if (!req)
		goto out;
	ret = PTR_ERR(req);
	if (IS_ERR(req))
		goto out;

	ret = nfs_set_page_writeback(page);
	BUG_ON(ret != 0);
	BUG_ON(test_bit(PG_CLEAN, &req->wb_flags));

	if (!nfs_pageio_add_request(pgio, req)) {
		nfs_redirty_request(req);
		ret = pgio->pg_error;
	}
out:
	return ret;
}

static int nfs_do_writepage(struct page *page, struct writeback_control *wbc, struct nfs_pageio_descriptor *pgio)
{
	struct inode *inode = page->mapping->host;
	int ret;

	nfs_inc_stats(inode, NFSIOS_VFSWRITEPAGE);
	nfs_add_stats(inode, NFSIOS_WRITEPAGES, 1);

	nfs_pageio_cond_complete(pgio, page->index);
	ret = nfs_page_async_flush(pgio, page,
			wbc->sync_mode == WB_SYNC_NONE ||
			wbc->nonblocking != 0);
	if (ret == -EAGAIN) {
		redirty_page_for_writepage(wbc, page);
		ret = 0;
	}
	return ret;
}

/*
 * Write an mmapped page to the server.
 */
static int nfs_writepage_locked(struct page *page, struct writeback_control *wbc)
{
	struct nfs_pageio_descriptor pgio;
	int err;

	nfs_pageio_init_write(&pgio, page->mapping->host, wb_priority(wbc));
	err = nfs_do_writepage(page, wbc, &pgio);
	nfs_pageio_complete(&pgio);
	if (err < 0)
		return err;
	if (pgio.pg_error < 0)
		return pgio.pg_error;
	return 0;
}

int nfs_writepage(struct page *page, struct writeback_control *wbc)
{
	int ret;

	ret = nfs_writepage_locked(page, wbc);
	unlock_page(page);
	return ret;
}

static int nfs_writepages_callback(struct page *page, struct writeback_control *wbc, void *data)
{
	int ret;

	ret = nfs_do_writepage(page, wbc, data);
	unlock_page(page);
	return ret;
}

int nfs_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	unsigned long *bitlock = &NFS_I(inode)->flags;
	struct nfs_pageio_descriptor pgio;
	int err;

	/* Stop dirtying of new pages while we sync */
	err = wait_on_bit_lock(bitlock, NFS_INO_FLUSHING,
			nfs_wait_bit_killable, TASK_KILLABLE);
	if (err)
		goto out_err;

	nfs_inc_stats(inode, NFSIOS_VFSWRITEPAGES);

	nfs_pageio_init_write(&pgio, inode, wb_priority(wbc));
	err = write_cache_pages(mapping, wbc, nfs_writepages_callback, &pgio);
	nfs_pageio_complete(&pgio);

	clear_bit_unlock(NFS_INO_FLUSHING, bitlock);
	smp_mb__after_clear_bit();
	wake_up_bit(bitlock, NFS_INO_FLUSHING);

	if (err < 0)
		goto out_err;
	err = pgio.pg_error;
	if (err < 0)
		goto out_err;
	return 0;
out_err:
	return err;
}

/*
 * Insert a write request into an inode
 */
static int nfs_inode_add_request(struct inode *inode, struct nfs_page *req)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int error;

	error = radix_tree_preload(GFP_NOFS);
	if (error != 0)
		goto out;

	/* Lock the request! */
	nfs_lock_request_dontget(req);

	spin_lock(&inode->i_lock);
	error = radix_tree_insert(&nfsi->nfs_page_tree, req->wb_index, req);
	BUG_ON(error);
	if (!nfsi->npages) {
		igrab(inode);
		if (nfs_have_delegation(inode, FMODE_WRITE))
			nfsi->change_attr++;
	}
	SetPagePrivate(req->wb_page);
	set_page_private(req->wb_page, (unsigned long)req);
	nfsi->npages++;
	kref_get(&req->wb_kref);
	radix_tree_tag_set(&nfsi->nfs_page_tree, req->wb_index,
				NFS_PAGE_TAG_LOCKED);
	spin_unlock(&inode->i_lock);
	radix_tree_preload_end();
out:
	return error;
}

/*
 * Remove a write request from an inode
 */
static void nfs_inode_remove_request(struct nfs_page *req)
{
	struct inode *inode = req->wb_context->path.dentry->d_inode;
	struct nfs_inode *nfsi = NFS_I(inode);

	BUG_ON (!NFS_WBACK_BUSY(req));

	spin_lock(&inode->i_lock);
	set_page_private(req->wb_page, 0);
	ClearPagePrivate(req->wb_page);
	radix_tree_delete(&nfsi->nfs_page_tree, req->wb_index);
	nfsi->npages--;
	if (!nfsi->npages) {
		spin_unlock(&inode->i_lock);
		iput(inode);
	} else
		spin_unlock(&inode->i_lock);
	nfs_clear_request(req);
	nfs_release_request(req);
}

static void
nfs_mark_request_dirty(struct nfs_page *req)
{
	__set_page_dirty_nobuffers(req->wb_page);
	__mark_inode_dirty(req->wb_page->mapping->host, I_DIRTY_DATASYNC);
}

#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
/*
 * Add a request to the inode's commit list.
 */
static void
nfs_mark_request_commit(struct nfs_page *req)
{
	struct inode *inode = req->wb_context->path.dentry->d_inode;
	struct nfs_inode *nfsi = NFS_I(inode);

	spin_lock(&inode->i_lock);
	set_bit(PG_CLEAN, &(req)->wb_flags);
	radix_tree_tag_set(&nfsi->nfs_page_tree,
			req->wb_index,
			NFS_PAGE_TAG_COMMIT);
	nfsi->ncommit++;
	spin_unlock(&inode->i_lock);
	inc_zone_page_state(req->wb_page, NR_UNSTABLE_NFS);
	inc_bdi_stat(req->wb_page->mapping->backing_dev_info, BDI_RECLAIMABLE);
	__mark_inode_dirty(inode, I_DIRTY_DATASYNC);
}

static int
nfs_clear_request_commit(struct nfs_page *req)
{
	struct page *page = req->wb_page;

	if (test_and_clear_bit(PG_CLEAN, &(req)->wb_flags)) {
		dec_zone_page_state(page, NR_UNSTABLE_NFS);
		dec_bdi_stat(page->mapping->backing_dev_info, BDI_RECLAIMABLE);
		return 1;
	}
	return 0;
}

static inline
int nfs_write_need_commit(struct nfs_write_data *data)
{
	return data->verf.committed != NFS_FILE_SYNC;
}

static inline
int nfs_reschedule_unstable_write(struct nfs_page *req)
{
	if (test_and_clear_bit(PG_NEED_COMMIT, &req->wb_flags)) {
		nfs_mark_request_commit(req);
		return 1;
	}
	if (test_and_clear_bit(PG_NEED_RESCHED, &req->wb_flags)) {
		nfs_mark_request_dirty(req);
		return 1;
	}
	return 0;
}
#else
static inline void
nfs_mark_request_commit(struct nfs_page *req)
{
}

static inline int
nfs_clear_request_commit(struct nfs_page *req)
{
	return 0;
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

#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
static int
nfs_need_commit(struct nfs_inode *nfsi)
{
	return radix_tree_tagged(&nfsi->nfs_page_tree, NFS_PAGE_TAG_COMMIT);
}

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
	int ret;

	if (!nfs_need_commit(nfsi))
		return 0;

	ret = nfs_scan_list(nfsi, dst, idx_start, npages, NFS_PAGE_TAG_COMMIT);
	if (ret > 0)
		nfsi->ncommit -= ret;
	if (nfs_need_commit(NFS_I(inode)))
		__mark_inode_dirty(inode, I_DIRTY_DATASYNC);
	return ret;
}
#else
static inline int nfs_need_commit(struct nfs_inode *nfsi)
{
	return 0;
}

static inline int nfs_scan_commit(struct inode *inode, struct list_head *dst, pgoff_t idx_start, unsigned int npages)
{
	return 0;
}
#endif

/*
 * Search for an existing write request, and attempt to update
 * it to reflect a new dirty region on a given page.
 *
 * If the attempt fails, then the existing request is flushed out
 * to disk.
 */
static struct nfs_page *nfs_try_to_update_request(struct inode *inode,
		struct page *page,
		unsigned int offset,
		unsigned int bytes)
{
	struct nfs_page *req;
	unsigned int rqend;
	unsigned int end;
	int error;

	if (!PagePrivate(page))
		return NULL;

	end = offset + bytes;
	spin_lock(&inode->i_lock);

	for (;;) {
		req = nfs_page_find_request_locked(page);
		if (req == NULL)
			goto out_unlock;

		rqend = req->wb_offset + req->wb_bytes;
		/*
		 * Tell the caller to flush out the request if
		 * the offsets are non-contiguous.
		 * Note: nfs_flush_incompatible() will already
		 * have flushed out requests having wrong owners.
		 */
		if (offset > rqend
		    || end < req->wb_offset)
			goto out_flushme;

		if (nfs_set_page_tag_locked(req))
			break;

		/* The request is locked, so wait and then retry */
		spin_unlock(&inode->i_lock);
		error = nfs_wait_on_request(req);
		nfs_release_request(req);
		if (error != 0)
			goto out_err;
		spin_lock(&inode->i_lock);
	}

	if (nfs_clear_request_commit(req) &&
			radix_tree_tag_clear(&NFS_I(inode)->nfs_page_tree,
				req->wb_index, NFS_PAGE_TAG_COMMIT) != NULL)
		NFS_I(inode)->ncommit--;

	/* Okay, the request matches. Update the region */
	if (offset < req->wb_offset) {
		req->wb_offset = offset;
		req->wb_pgbase = offset;
	}
	if (end > rqend)
		req->wb_bytes = end - req->wb_offset;
	else
		req->wb_bytes = rqend - req->wb_offset;
out_unlock:
	spin_unlock(&inode->i_lock);
	return req;
out_flushme:
	spin_unlock(&inode->i_lock);
	nfs_release_request(req);
	error = nfs_wb_page(inode, page);
out_err:
	return ERR_PTR(error);
}

/*
 * Try to update an existing write request, or create one if there is none.
 *
 * Note: Should always be called with the Page Lock held to prevent races
 * if we have to add a new request. Also assumes that the caller has
 * already called nfs_flush_incompatible() if necessary.
 */
static struct nfs_page * nfs_setup_write_request(struct nfs_open_context* ctx,
		struct page *page, unsigned int offset, unsigned int bytes)
{
	struct inode *inode = page->mapping->host;
	struct nfs_page	*req;
	int error;

	req = nfs_try_to_update_request(inode, page, offset, bytes);
	if (req != NULL)
		goto out;
	req = nfs_create_request(ctx, inode, page, offset, bytes);
	if (IS_ERR(req))
		goto out;
	error = nfs_inode_add_request(inode, req);
	if (error != 0) {
		nfs_release_request(req);
		req = ERR_PTR(error);
	}
out:
	return req;
}

static int nfs_writepage_setup(struct nfs_open_context *ctx, struct page *page,
		unsigned int offset, unsigned int count)
{
	struct nfs_page	*req;

	req = nfs_setup_write_request(ctx, page, offset, count);
	if (IS_ERR(req))
		return PTR_ERR(req);
	nfs_mark_request_dirty(req);
	/* Update file length */
	nfs_grow_file(page, offset, count);
	nfs_mark_uptodate(page, req->wb_pgbase, req->wb_bytes);
	nfs_mark_request_dirty(req);
	nfs_clear_page_tag_locked(req);
	return 0;
}

int nfs_flush_incompatible(struct file *file, struct page *page)
{
	struct nfs_open_context *ctx = nfs_file_open_context(file);
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
		do_flush = req->wb_page != page || req->wb_context != ctx ||
			req->wb_lock_context->lockowner != current->files ||
			req->wb_lock_context->pid != current->tgid;
		nfs_release_request(req);
		if (!do_flush)
			return 0;
		status = nfs_wb_page(page->mapping->host, page);
	} while (status == 0);
	return status;
}

/*
 * If the page cache is marked as unsafe or invalid, then we can't rely on
 * the PageUptodate() flag. In this case, we will need to turn off
 * write optimisations that depend on the page contents being correct.
 */
static int nfs_write_pageuptodate(struct page *page, struct inode *inode)
{
	return PageUptodate(page) &&
		!(NFS_I(inode)->cache_validity & (NFS_INO_REVAL_PAGECACHE|NFS_INO_INVALID_DATA));
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
	struct nfs_open_context *ctx = nfs_file_open_context(file);
	struct inode	*inode = page->mapping->host;
	int		status = 0;

	nfs_inc_stats(inode, NFSIOS_VFSUPDATEPAGE);

	dprintk("NFS:       nfs_updatepage(%s/%s %d@%lld)\n",
		file->f_path.dentry->d_parent->d_name.name,
		file->f_path.dentry->d_name.name, count,
		(long long)(page_offset(page) + offset));

	/* If we're not using byte range locks, and we know the page
	 * is up to date, it may be more efficient to extend the write
	 * to cover the entire page in order to avoid fragmentation
	 * inefficiencies.
	 */
	if (nfs_write_pageuptodate(page, inode) &&
			inode->i_flock == NULL &&
			!(file->f_flags & O_DSYNC)) {
		count = max(count + offset, nfs_page_length(page));
		offset = 0;
	}

	status = nfs_writepage_setup(ctx, page, offset, count);
	if (status < 0)
		nfs_set_pageerror(page);

	dprintk("NFS:       nfs_updatepage returns %d (isize %lld)\n",
			status, (long long)i_size_read(inode));
	return status;
}

static void nfs_writepage_release(struct nfs_page *req)
{
	struct page *page = req->wb_page;

	if (PageError(req->wb_page) || !nfs_reschedule_unstable_write(req))
		nfs_inode_remove_request(req);
	nfs_clear_page_tag_locked(req);
	nfs_end_page_writeback(page);
}

static int flush_task_priority(int how)
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
static int nfs_write_rpcsetup(struct nfs_page *req,
		struct nfs_write_data *data,
		const struct rpc_call_ops *call_ops,
		unsigned int count, unsigned int offset,
		int how)
{
	struct inode *inode = req->wb_context->path.dentry->d_inode;
	int priority = flush_task_priority(how);
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = req->wb_context->cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = NFS_CLIENT(inode),
		.task = &data->task,
		.rpc_message = &msg,
		.callback_ops = call_ops,
		.callback_data = data,
		.workqueue = nfsiod_workqueue,
		.flags = RPC_TASK_ASYNC,
		.priority = priority,
	};
	int ret = 0;

	/* Set up the RPC argument and reply structs
	 * NB: take care not to mess about with data->commit et al. */

	data->req = req;
	data->inode = inode = req->wb_context->path.dentry->d_inode;
	data->cred = msg.rpc_cred;

	data->args.fh     = NFS_FH(inode);
	data->args.offset = req_offset(req) + offset;
	data->args.pgbase = req->wb_pgbase + offset;
	data->args.pages  = data->pagevec;
	data->args.count  = count;
	data->args.context = get_nfs_open_context(req->wb_context);
	data->args.lock_context = req->wb_lock_context;
	data->args.stable  = NFS_UNSTABLE;
	if (how & FLUSH_STABLE) {
		data->args.stable = NFS_DATA_SYNC;
		if (!nfs_need_commit(NFS_I(inode)))
			data->args.stable = NFS_FILE_SYNC;
	}

	data->res.fattr   = &data->fattr;
	data->res.count   = count;
	data->res.verf    = &data->verf;
	nfs_fattr_init(&data->fattr);

	/* Set up the initial task struct.  */
	NFS_PROTO(inode)->write_setup(data, &msg);

	dprintk("NFS: %5u initiated write call "
		"(req %s/%lld, %u bytes @ offset %llu)\n",
		data->task.tk_pid,
		inode->i_sb->s_id,
		(long long)NFS_FILEID(inode),
		count,
		(unsigned long long)data->args.offset);

	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task)) {
		ret = PTR_ERR(task);
		goto out;
	}
	if (how & FLUSH_SYNC) {
		ret = rpc_wait_for_completion_task(task);
		if (ret == 0)
			ret = task->tk_status;
	}
	rpc_put_task(task);
out:
	return ret;
}

/* If a nfs_flush_* function fails, it should remove reqs from @head and
 * call this on each, which will prepare them to be retried on next
 * writeback using standard nfs.
 */
static void nfs_redirty_request(struct nfs_page *req)
{
	struct page *page = req->wb_page;

	nfs_mark_request_dirty(req);
	nfs_clear_page_tag_locked(req);
	nfs_end_page_writeback(page);
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
	int ret = 0;
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
		int ret2;

		data = list_entry(list.next, struct nfs_write_data, pages);
		list_del_init(&data->pages);

		data->pagevec[0] = page;

		if (nbytes < wsize)
			wsize = nbytes;
		ret2 = nfs_write_rpcsetup(req, data, &nfs_write_partial_ops,
				   wsize, offset, how);
		if (ret == 0)
			ret = ret2;
		offset += wsize;
		nbytes -= wsize;
	} while (nbytes != 0);

	return ret;

out_bad:
	while (!list_empty(&list)) {
		data = list_entry(list.next, struct nfs_write_data, pages);
		list_del(&data->pages);
		nfs_writedata_release(data);
	}
	nfs_redirty_request(req);
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
	return nfs_write_rpcsetup(req, data, &nfs_write_full_ops, count, 0, how);
 out_bad:
	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_redirty_request(req);
	}
	return -ENOMEM;
}

static void nfs_pageio_init_write(struct nfs_pageio_descriptor *pgio,
				  struct inode *inode, int ioflags)
{
	size_t wsize = NFS_SERVER(inode)->wsize;

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

	dprintk("NFS: %5u write(%s/%lld %d@%lld)",
		task->tk_pid,
		data->req->wb_context->path.dentry->d_inode->i_sb->s_id,
		(long long)
		  NFS_FILEID(data->req->wb_context->path.dentry->d_inode),
		data->req->wb_bytes, (long long)req_offset(data->req));

	nfs_writeback_done(task, data);
}

static void nfs_writeback_release_partial(void *calldata)
{
	struct nfs_write_data	*data = calldata;
	struct nfs_page		*req = data->req;
	struct page		*page = req->wb_page;
	int status = data->task.tk_status;

	if (status < 0) {
		nfs_set_pageerror(page);
		nfs_context_set_write_error(req->wb_context, status);
		dprintk(", error = %d\n", status);
		goto out;
	}

	if (nfs_write_need_commit(data)) {
		struct inode *inode = page->mapping->host;

		spin_lock(&inode->i_lock);
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
		spin_unlock(&inode->i_lock);
	} else
		dprintk(" OK\n");

out:
	if (atomic_dec_and_test(&req->wb_complete))
		nfs_writepage_release(req);
	nfs_writedata_release(calldata);
}

#if defined(CONFIG_NFS_V4_1)
void nfs_write_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs_write_data *data = calldata;

	if (nfs4_setup_sequence(NFS_SERVER(data->inode),
				&data->args.seq_args,
				&data->res.seq_res, 1, task))
		return;
	rpc_call_start(task);
}
#endif /* CONFIG_NFS_V4_1 */

static const struct rpc_call_ops nfs_write_partial_ops = {
#if defined(CONFIG_NFS_V4_1)
	.rpc_call_prepare = nfs_write_prepare,
#endif /* CONFIG_NFS_V4_1 */
	.rpc_call_done = nfs_writeback_done_partial,
	.rpc_release = nfs_writeback_release_partial,
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

	nfs_writeback_done(task, data);
}

static void nfs_writeback_release_full(void *calldata)
{
	struct nfs_write_data	*data = calldata;
	int status = data->task.tk_status;

	/* Update attributes as result of writeback. */
	while (!list_empty(&data->pages)) {
		struct nfs_page *req = nfs_list_entry(data->pages.next);
		struct page *page = req->wb_page;

		nfs_list_remove_request(req);

		dprintk("NFS: %5u write (%s/%lld %d@%lld)",
			data->task.tk_pid,
			req->wb_context->path.dentry->d_inode->i_sb->s_id,
			(long long)NFS_FILEID(req->wb_context->path.dentry->d_inode),
			req->wb_bytes,
			(long long)req_offset(req));

		if (status < 0) {
			nfs_set_pageerror(page);
			nfs_context_set_write_error(req->wb_context, status);
			dprintk(", error = %d\n", status);
			goto remove_request;
		}

		if (nfs_write_need_commit(data)) {
			memcpy(&req->wb_verf, &data->verf, sizeof(req->wb_verf));
			nfs_mark_request_commit(req);
			dprintk(" marked for commit\n");
			goto next;
		}
		dprintk(" OK\n");
remove_request:
		nfs_inode_remove_request(req);
	next:
		nfs_clear_page_tag_locked(req);
		nfs_end_page_writeback(page);
	}
	nfs_writedata_release(calldata);
}

static const struct rpc_call_ops nfs_write_full_ops = {
#if defined(CONFIG_NFS_V4_1)
	.rpc_call_prepare = nfs_write_prepare,
#endif /* CONFIG_NFS_V4_1 */
	.rpc_call_done = nfs_writeback_done_full,
	.rpc_release = nfs_writeback_release_full,
};


/*
 * This function is called when the WRITE call is complete.
 */
int nfs_writeback_done(struct rpc_task *task, struct nfs_write_data *data)
{
	struct nfs_writeargs	*argp = &data->args;
	struct nfs_writeres	*resp = &data->res;
	struct nfs_server	*server = NFS_SERVER(data->inode);
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
			dprintk("NFS:       faulty NFS server %s:"
				" (committed = %d) != (stable = %d)\n",
				server->nfs_client->cl_hostname,
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
			nfs_restart_rpc(task, server->nfs_client);
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
static int nfs_commit_set_lock(struct nfs_inode *nfsi, int may_wait)
{
	if (!test_and_set_bit(NFS_INO_COMMIT, &nfsi->flags))
		return 1;
	if (may_wait && !out_of_line_wait_on_bit_lock(&nfsi->flags,
				NFS_INO_COMMIT, nfs_wait_bit_killable,
				TASK_KILLABLE))
		return 1;
	return 0;
}

static void nfs_commit_clear_lock(struct nfs_inode *nfsi)
{
	clear_bit(NFS_INO_COMMIT, &nfsi->flags);
	smp_mb__after_clear_bit();
	wake_up_bit(&nfsi->flags, NFS_INO_COMMIT);
}


static void nfs_commitdata_release(void *data)
{
	struct nfs_write_data *wdata = data;

	put_nfs_open_context(wdata->args.context);
	nfs_commit_free(wdata);
}

/*
 * Set up the argument/result storage required for the RPC call.
 */
static int nfs_commit_rpcsetup(struct list_head *head,
		struct nfs_write_data *data,
		int how)
{
	struct nfs_page *first = nfs_list_entry(head->next);
	struct inode *inode = first->wb_context->path.dentry->d_inode;
	int priority = flush_task_priority(how);
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = first->wb_context->cred,
	};
	struct rpc_task_setup task_setup_data = {
		.task = &data->task,
		.rpc_client = NFS_CLIENT(inode),
		.rpc_message = &msg,
		.callback_ops = &nfs_commit_ops,
		.callback_data = data,
		.workqueue = nfsiod_workqueue,
		.flags = RPC_TASK_ASYNC,
		.priority = priority,
	};

	/* Set up the RPC argument and reply structs
	 * NB: take care not to mess about with data->commit et al. */

	list_splice_init(head, &data->pages);

	data->inode	  = inode;
	data->cred	  = msg.rpc_cred;

	data->args.fh     = NFS_FH(data->inode);
	/* Note: we always request a commit of the entire inode */
	data->args.offset = 0;
	data->args.count  = 0;
	data->args.context = get_nfs_open_context(first->wb_context);
	data->res.count   = 0;
	data->res.fattr   = &data->fattr;
	data->res.verf    = &data->verf;
	nfs_fattr_init(&data->fattr);

	/* Set up the initial task struct.  */
	NFS_PROTO(inode)->commit_setup(data, &msg);

	dprintk("NFS: %5u initiated commit call\n", data->task.tk_pid);

	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	rpc_put_task(task);
	return 0;
}

/*
 * Commit dirty pages
 */
static int
nfs_commit_list(struct inode *inode, struct list_head *head, int how)
{
	struct nfs_write_data	*data;
	struct nfs_page         *req;

	data = nfs_commitdata_alloc();

	if (!data)
		goto out_bad;

	/* Set up the argument struct */
	return nfs_commit_rpcsetup(head, data, how);
 out_bad:
	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_mark_request_commit(req);
		dec_zone_page_state(req->wb_page, NR_UNSTABLE_NFS);
		dec_bdi_stat(req->wb_page->mapping->backing_dev_info,
				BDI_RECLAIMABLE);
		nfs_clear_page_tag_locked(req);
	}
	nfs_commit_clear_lock(NFS_I(inode));
	return -ENOMEM;
}

/*
 * COMMIT call returned
 */
static void nfs_commit_done(struct rpc_task *task, void *calldata)
{
	struct nfs_write_data	*data = calldata;

        dprintk("NFS: %5u nfs_commit_done (status %d)\n",
                                task->tk_pid, task->tk_status);

	/* Call the NFS version-specific code */
	if (NFS_PROTO(data->inode)->commit_done(task, data) != 0)
		return;
}

static void nfs_commit_release(void *calldata)
{
	struct nfs_write_data	*data = calldata;
	struct nfs_page		*req;
	int status = data->task.tk_status;

	while (!list_empty(&data->pages)) {
		req = nfs_list_entry(data->pages.next);
		nfs_list_remove_request(req);
		nfs_clear_request_commit(req);

		dprintk("NFS:       commit (%s/%lld %d@%lld)",
			req->wb_context->path.dentry->d_inode->i_sb->s_id,
			(long long)NFS_FILEID(req->wb_context->path.dentry->d_inode),
			req->wb_bytes,
			(long long)req_offset(req));
		if (status < 0) {
			nfs_context_set_write_error(req->wb_context, status);
			nfs_inode_remove_request(req);
			dprintk(", error = %d\n", status);
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
		nfs_mark_request_dirty(req);
	next:
		nfs_clear_page_tag_locked(req);
	}
	nfs_commit_clear_lock(NFS_I(data->inode));
	nfs_commitdata_release(calldata);
}

static const struct rpc_call_ops nfs_commit_ops = {
#if defined(CONFIG_NFS_V4_1)
	.rpc_call_prepare = nfs_write_prepare,
#endif /* CONFIG_NFS_V4_1 */
	.rpc_call_done = nfs_commit_done,
	.rpc_release = nfs_commit_release,
};

int nfs_commit_inode(struct inode *inode, int how)
{
	LIST_HEAD(head);
	int may_wait = how & FLUSH_SYNC;
	int res = 0;

	if (!nfs_commit_set_lock(NFS_I(inode), may_wait))
		goto out_mark_dirty;
	spin_lock(&inode->i_lock);
	res = nfs_scan_commit(inode, &head, 0, 0);
	spin_unlock(&inode->i_lock);
	if (res) {
		int error = nfs_commit_list(inode, &head, how);
		if (error < 0)
			return error;
		if (may_wait)
			wait_on_bit(&NFS_I(inode)->flags, NFS_INO_COMMIT,
					nfs_wait_bit_killable,
					TASK_KILLABLE);
		else
			goto out_mark_dirty;
	} else
		nfs_commit_clear_lock(NFS_I(inode));
	return res;
	/* Note: If we exit without ensuring that the commit is complete,
	 * we must mark the inode as dirty. Otherwise, future calls to
	 * sync_inode() with the WB_SYNC_ALL flag set will fail to ensure
	 * that the data is on the disk.
	 */
out_mark_dirty:
	__mark_inode_dirty(inode, I_DIRTY_DATASYNC);
	return res;
}

static int nfs_commit_unstable_pages(struct inode *inode, struct writeback_control *wbc)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int flags = FLUSH_SYNC;
	int ret = 0;

	/* Don't commit yet if this is a non-blocking flush and there are
	 * lots of outstanding writes for this mapping.
	 */
	if (wbc->sync_mode == WB_SYNC_NONE &&
	    nfsi->ncommit <= (nfsi->npages >> 1))
		goto out_mark_dirty;

	if (wbc->nonblocking || wbc->for_background)
		flags = 0;
	ret = nfs_commit_inode(inode, flags);
	if (ret >= 0) {
		if (wbc->sync_mode == WB_SYNC_NONE) {
			if (ret < wbc->nr_to_write)
				wbc->nr_to_write -= ret;
			else
				wbc->nr_to_write = 0;
		}
		return 0;
	}
out_mark_dirty:
	__mark_inode_dirty(inode, I_DIRTY_DATASYNC);
	return ret;
}
#else
static int nfs_commit_unstable_pages(struct inode *inode, struct writeback_control *wbc)
{
	return 0;
}
#endif

int nfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	return nfs_commit_unstable_pages(inode, wbc);
}

/*
 * flush the inode to disk.
 */
int nfs_wb_all(struct inode *inode)
{
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = LONG_MAX,
		.range_start = 0,
		.range_end = LLONG_MAX,
	};

	return sync_inode(inode, &wbc);
}

int nfs_wb_page_cancel(struct inode *inode, struct page *page)
{
	struct nfs_page *req;
	int ret = 0;

	BUG_ON(!PageLocked(page));
	for (;;) {
		wait_on_page_writeback(page);
		req = nfs_page_find_request(page);
		if (req == NULL)
			break;
		if (nfs_lock_request_dontget(req)) {
			nfs_inode_remove_request(req);
			/*
			 * In case nfs_inode_remove_request has marked the
			 * page as being dirty
			 */
			cancel_dirty_page(page, PAGE_CACHE_SIZE);
			nfs_unlock_request(req);
			break;
		}
		ret = nfs_wait_on_request(req);
		nfs_release_request(req);
		if (ret < 0)
			break;
	}
	return ret;
}

/*
 * Write back all requests on one page - we do this before reading it.
 */
int nfs_wb_page(struct inode *inode, struct page *page)
{
	loff_t range_start = page_offset(page);
	loff_t range_end = range_start + (loff_t)(PAGE_CACHE_SIZE - 1);
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = 0,
		.range_start = range_start,
		.range_end = range_end,
	};
	int ret;

	for (;;) {
		wait_on_page_writeback(page);
		if (clear_page_dirty_for_io(page)) {
			ret = nfs_writepage_locked(page, &wbc);
			if (ret < 0)
				goto out_error;
			continue;
		}
		if (!PagePrivate(page))
			break;
		ret = nfs_commit_inode(inode, FLUSH_SYNC);
		if (ret < 0)
			goto out_error;
	}
	return 0;
out_error:
	return ret;
}

#ifdef CONFIG_MIGRATION
int nfs_migrate_page(struct address_space *mapping, struct page *newpage,
		struct page *page)
{
	struct nfs_page *req;
	int ret;

	nfs_fscache_release_page(page, GFP_KERNEL);

	req = nfs_find_and_lock_request(page, false);
	ret = PTR_ERR(req);
	if (IS_ERR(req))
		goto out;

	ret = migrate_page(mapping, newpage, page);
	if (!req)
		goto out;
	if (ret)
		goto out_unlock;
	page_cache_get(newpage);
	spin_lock(&mapping->host->i_lock);
	req->wb_page = newpage;
	SetPagePrivate(newpage);
	set_page_private(newpage, (unsigned long)req);
	ClearPagePrivate(page);
	set_page_private(page, 0);
	spin_unlock(&mapping->host->i_lock);
	page_cache_release(page);
out_unlock:
	nfs_clear_page_tag_locked(req);
out:
	return ret;
}
#endif

int __init nfs_init_writepagecache(void)
{
	nfs_wdata_cachep = kmem_cache_create("nfs_write_data",
					     sizeof(struct nfs_write_data),
					     0, SLAB_HWCACHE_ALIGN,
					     NULL);
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

