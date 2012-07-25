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
#include <linux/export.h>

#include <asm/uaccess.h>

#include "delegation.h"
#include "internal.h"
#include "iostat.h"
#include "nfs4_fs.h"
#include "fscache.h"
#include "pnfs.h"

#define NFSDBG_FACILITY		NFSDBG_PAGECACHE

#define MIN_POOL_WRITE		(32)
#define MIN_POOL_COMMIT		(4)

/*
 * Local function declarations
 */
static void nfs_redirty_request(struct nfs_page *req);
static const struct rpc_call_ops nfs_write_common_ops;
static const struct rpc_call_ops nfs_commit_ops;
static const struct nfs_pgio_completion_ops nfs_async_write_completion_ops;
static const struct nfs_commit_completion_ops nfs_commit_completion_ops;

static struct kmem_cache *nfs_wdata_cachep;
static mempool_t *nfs_wdata_mempool;
static struct kmem_cache *nfs_cdata_cachep;
static mempool_t *nfs_commit_mempool;

struct nfs_commit_data *nfs_commitdata_alloc(void)
{
	struct nfs_commit_data *p = mempool_alloc(nfs_commit_mempool, GFP_NOFS);

	if (p) {
		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&p->pages);
	}
	return p;
}
EXPORT_SYMBOL_GPL(nfs_commitdata_alloc);

void nfs_commit_free(struct nfs_commit_data *p)
{
	mempool_free(p, nfs_commit_mempool);
}
EXPORT_SYMBOL_GPL(nfs_commit_free);

struct nfs_write_header *nfs_writehdr_alloc(void)
{
	struct nfs_write_header *p = mempool_alloc(nfs_wdata_mempool, GFP_NOFS);

	if (p) {
		struct nfs_pgio_header *hdr = &p->header;

		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&hdr->pages);
		INIT_LIST_HEAD(&hdr->rpc_list);
		spin_lock_init(&hdr->lock);
		atomic_set(&hdr->refcnt, 0);
		hdr->verf = &p->verf;
	}
	return p;
}

static struct nfs_write_data *nfs_writedata_alloc(struct nfs_pgio_header *hdr,
						  unsigned int pagecount)
{
	struct nfs_write_data *data, *prealloc;

	prealloc = &container_of(hdr, struct nfs_write_header, header)->rpc_data;
	if (prealloc->header == NULL)
		data = prealloc;
	else
		data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		goto out;

	if (nfs_pgarray_set(&data->pages, pagecount)) {
		data->header = hdr;
		atomic_inc(&hdr->refcnt);
	} else {
		if (data != prealloc)
			kfree(data);
		data = NULL;
	}
out:
	return data;
}

void nfs_writehdr_free(struct nfs_pgio_header *hdr)
{
	struct nfs_write_header *whdr = container_of(hdr, struct nfs_write_header, header);
	mempool_free(whdr, nfs_wdata_mempool);
}

void nfs_writedata_release(struct nfs_write_data *wdata)
{
	struct nfs_pgio_header *hdr = wdata->header;
	struct nfs_write_header *write_header = container_of(hdr, struct nfs_write_header, header);

	put_nfs_open_context(wdata->args.context);
	if (wdata->pages.pagevec != wdata->pages.page_array)
		kfree(wdata->pages.pagevec);
	if (wdata != &write_header->rpc_data)
		kfree(wdata);
	else
		wdata->header = NULL;
	if (atomic_dec_and_test(&hdr->refcnt))
		hdr->completion_ops->completion(hdr);
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
		return FLUSH_LOWPRI | FLUSH_COND_STABLE;
	return FLUSH_COND_STABLE;
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
		if (nfs_lock_request(req))
			break;
		/* Note: If we hold the page lock, as is the case in nfs_writepage,
		 *	 then the call to nfs_lock_request() will always
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
	ret = nfs_page_async_flush(pgio, page, wbc->sync_mode == WB_SYNC_NONE);
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

	nfs_pageio_init_write(&pgio, page->mapping->host, wb_priority(wbc),
			      &nfs_async_write_completion_ops);
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

	nfs_pageio_init_write(&pgio, inode, wb_priority(wbc),
			      &nfs_async_write_completion_ops);
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
static void nfs_inode_add_request(struct inode *inode, struct nfs_page *req)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	/* Lock the request! */
	nfs_lock_request(req);

	spin_lock(&inode->i_lock);
	if (!nfsi->npages && nfs_have_delegation(inode, FMODE_WRITE))
		inode->i_version++;
	set_bit(PG_MAPPED, &req->wb_flags);
	SetPagePrivate(req->wb_page);
	set_page_private(req->wb_page, (unsigned long)req);
	nfsi->npages++;
	kref_get(&req->wb_kref);
	spin_unlock(&inode->i_lock);
}

/*
 * Remove a write request from an inode
 */
static void nfs_inode_remove_request(struct nfs_page *req)
{
	struct inode *inode = req->wb_context->dentry->d_inode;
	struct nfs_inode *nfsi = NFS_I(inode);

	BUG_ON (!NFS_WBACK_BUSY(req));

	spin_lock(&inode->i_lock);
	set_page_private(req->wb_page, 0);
	ClearPagePrivate(req->wb_page);
	clear_bit(PG_MAPPED, &req->wb_flags);
	nfsi->npages--;
	spin_unlock(&inode->i_lock);
	nfs_release_request(req);
}

static void
nfs_mark_request_dirty(struct nfs_page *req)
{
	__set_page_dirty_nobuffers(req->wb_page);
}

#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
/**
 * nfs_request_add_commit_list - add request to a commit list
 * @req: pointer to a struct nfs_page
 * @dst: commit list head
 * @cinfo: holds list lock and accounting info
 *
 * This sets the PG_CLEAN bit, updates the cinfo count of
 * number of outstanding requests requiring a commit as well as
 * the MM page stats.
 *
 * The caller must _not_ hold the cinfo->lock, but must be
 * holding the nfs_page lock.
 */
void
nfs_request_add_commit_list(struct nfs_page *req, struct list_head *dst,
			    struct nfs_commit_info *cinfo)
{
	set_bit(PG_CLEAN, &(req)->wb_flags);
	spin_lock(cinfo->lock);
	nfs_list_add_request(req, dst);
	cinfo->mds->ncommit++;
	spin_unlock(cinfo->lock);
	if (!cinfo->dreq) {
		inc_zone_page_state(req->wb_page, NR_UNSTABLE_NFS);
		inc_bdi_stat(req->wb_page->mapping->backing_dev_info,
			     BDI_RECLAIMABLE);
		__mark_inode_dirty(req->wb_context->dentry->d_inode,
				   I_DIRTY_DATASYNC);
	}
}
EXPORT_SYMBOL_GPL(nfs_request_add_commit_list);

/**
 * nfs_request_remove_commit_list - Remove request from a commit list
 * @req: pointer to a nfs_page
 * @cinfo: holds list lock and accounting info
 *
 * This clears the PG_CLEAN bit, and updates the cinfo's count of
 * number of outstanding requests requiring a commit
 * It does not update the MM page stats.
 *
 * The caller _must_ hold the cinfo->lock and the nfs_page lock.
 */
void
nfs_request_remove_commit_list(struct nfs_page *req,
			       struct nfs_commit_info *cinfo)
{
	if (!test_and_clear_bit(PG_CLEAN, &(req)->wb_flags))
		return;
	nfs_list_remove_request(req);
	cinfo->mds->ncommit--;
}
EXPORT_SYMBOL_GPL(nfs_request_remove_commit_list);

static void nfs_init_cinfo_from_inode(struct nfs_commit_info *cinfo,
				      struct inode *inode)
{
	cinfo->lock = &inode->i_lock;
	cinfo->mds = &NFS_I(inode)->commit_info;
	cinfo->ds = pnfs_get_ds_info(inode);
	cinfo->dreq = NULL;
	cinfo->completion_ops = &nfs_commit_completion_ops;
}

void nfs_init_cinfo(struct nfs_commit_info *cinfo,
		    struct inode *inode,
		    struct nfs_direct_req *dreq)
{
	if (dreq)
		nfs_init_cinfo_from_dreq(cinfo, dreq);
	else
		nfs_init_cinfo_from_inode(cinfo, inode);
}
EXPORT_SYMBOL_GPL(nfs_init_cinfo);

/*
 * Add a request to the inode's commit list.
 */
void
nfs_mark_request_commit(struct nfs_page *req, struct pnfs_layout_segment *lseg,
			struct nfs_commit_info *cinfo)
{
	if (pnfs_mark_request_commit(req, lseg, cinfo))
		return;
	nfs_request_add_commit_list(req, &cinfo->mds->list, cinfo);
}

static void
nfs_clear_page_commit(struct page *page)
{
	dec_zone_page_state(page, NR_UNSTABLE_NFS);
	dec_bdi_stat(page->mapping->backing_dev_info, BDI_RECLAIMABLE);
}

static void
nfs_clear_request_commit(struct nfs_page *req)
{
	if (test_bit(PG_CLEAN, &req->wb_flags)) {
		struct inode *inode = req->wb_context->dentry->d_inode;
		struct nfs_commit_info cinfo;

		nfs_init_cinfo_from_inode(&cinfo, inode);
		if (!pnfs_clear_request_commit(req, &cinfo)) {
			spin_lock(cinfo.lock);
			nfs_request_remove_commit_list(req, &cinfo);
			spin_unlock(cinfo.lock);
		}
		nfs_clear_page_commit(req->wb_page);
	}
}

static inline
int nfs_write_need_commit(struct nfs_write_data *data)
{
	if (data->verf.committed == NFS_DATA_SYNC)
		return data->header->lseg == NULL;
	return data->verf.committed != NFS_FILE_SYNC;
}

#else
static void nfs_init_cinfo_from_inode(struct nfs_commit_info *cinfo,
				      struct inode *inode)
{
}

void nfs_init_cinfo(struct nfs_commit_info *cinfo,
		    struct inode *inode,
		    struct nfs_direct_req *dreq)
{
}

void
nfs_mark_request_commit(struct nfs_page *req, struct pnfs_layout_segment *lseg,
			struct nfs_commit_info *cinfo)
{
}

static void
nfs_clear_request_commit(struct nfs_page *req)
{
}

static inline
int nfs_write_need_commit(struct nfs_write_data *data)
{
	return 0;
}

#endif

static void nfs_write_completion(struct nfs_pgio_header *hdr)
{
	struct nfs_commit_info cinfo;
	unsigned long bytes = 0;

	if (test_bit(NFS_IOHDR_REDO, &hdr->flags))
		goto out;
	nfs_init_cinfo_from_inode(&cinfo, hdr->inode);
	while (!list_empty(&hdr->pages)) {
		struct nfs_page *req = nfs_list_entry(hdr->pages.next);

		bytes += req->wb_bytes;
		nfs_list_remove_request(req);
		if (test_bit(NFS_IOHDR_ERROR, &hdr->flags) &&
		    (hdr->good_bytes < bytes)) {
			nfs_set_pageerror(req->wb_page);
			nfs_context_set_write_error(req->wb_context, hdr->error);
			goto remove_req;
		}
		if (test_bit(NFS_IOHDR_NEED_RESCHED, &hdr->flags)) {
			nfs_mark_request_dirty(req);
			goto next;
		}
		if (test_bit(NFS_IOHDR_NEED_COMMIT, &hdr->flags)) {
			memcpy(&req->wb_verf, hdr->verf, sizeof(req->wb_verf));
			nfs_mark_request_commit(req, hdr->lseg, &cinfo);
			goto next;
		}
remove_req:
		nfs_inode_remove_request(req);
next:
		nfs_unlock_request(req);
		nfs_end_page_writeback(req->wb_page);
		nfs_release_request(req);
	}
out:
	hdr->release(hdr);
}

#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
static unsigned long
nfs_reqs_to_commit(struct nfs_commit_info *cinfo)
{
	return cinfo->mds->ncommit;
}

/* cinfo->lock held by caller */
int
nfs_scan_commit_list(struct list_head *src, struct list_head *dst,
		     struct nfs_commit_info *cinfo, int max)
{
	struct nfs_page *req, *tmp;
	int ret = 0;

	list_for_each_entry_safe(req, tmp, src, wb_list) {
		if (!nfs_lock_request(req))
			continue;
		kref_get(&req->wb_kref);
		if (cond_resched_lock(cinfo->lock))
			list_safe_reset_next(req, tmp, wb_list);
		nfs_request_remove_commit_list(req, cinfo);
		nfs_list_add_request(req, dst);
		ret++;
		if ((ret == max) && !cinfo->dreq)
			break;
	}
	return ret;
}

/*
 * nfs_scan_commit - Scan an inode for commit requests
 * @inode: NFS inode to scan
 * @dst: mds destination list
 * @cinfo: mds and ds lists of reqs ready to commit
 *
 * Moves requests from the inode's 'commit' request list.
 * The requests are *not* checked to ensure that they form a contiguous set.
 */
int
nfs_scan_commit(struct inode *inode, struct list_head *dst,
		struct nfs_commit_info *cinfo)
{
	int ret = 0;

	spin_lock(cinfo->lock);
	if (cinfo->mds->ncommit > 0) {
		const int max = INT_MAX;

		ret = nfs_scan_commit_list(&cinfo->mds->list, dst,
					   cinfo, max);
		ret += pnfs_scan_commit_lists(inode, cinfo, max - ret);
	}
	spin_unlock(cinfo->lock);
	return ret;
}

#else
static unsigned long nfs_reqs_to_commit(struct nfs_commit_info *cinfo)
{
	return 0;
}

int nfs_scan_commit(struct inode *inode, struct list_head *dst,
		    struct nfs_commit_info *cinfo)
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

		if (nfs_lock_request(req))
			break;

		/* The request is locked, so wait and then retry */
		spin_unlock(&inode->i_lock);
		error = nfs_wait_on_request(req);
		nfs_release_request(req);
		if (error != 0)
			goto out_err;
		spin_lock(&inode->i_lock);
	}

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
	if (req)
		nfs_clear_request_commit(req);
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

	req = nfs_try_to_update_request(inode, page, offset, bytes);
	if (req != NULL)
		goto out;
	req = nfs_create_request(ctx, inode, page, offset, bytes);
	if (IS_ERR(req))
		goto out;
	nfs_inode_add_request(inode, req);
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
	/* Update file length */
	nfs_grow_file(page, offset, count);
	nfs_mark_uptodate(page, req->wb_pgbase, req->wb_bytes);
	nfs_mark_request_dirty(req);
	nfs_unlock_and_release_request(req);
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
static bool nfs_write_pageuptodate(struct page *page, struct inode *inode)
{
	if (nfs_have_delegated_attributes(inode))
		goto out;
	if (NFS_I(inode)->cache_validity & NFS_INO_REVAL_PAGECACHE)
		return false;
out:
	return PageUptodate(page) != 0;
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
	else
		__set_page_dirty_nobuffers(page);

	dprintk("NFS:       nfs_updatepage returns %d (isize %lld)\n",
			status, (long long)i_size_read(inode));
	return status;
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

int nfs_initiate_write(struct rpc_clnt *clnt,
		       struct nfs_write_data *data,
		       const struct rpc_call_ops *call_ops,
		       int how, int flags)
{
	struct inode *inode = data->header->inode;
	int priority = flush_task_priority(how);
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->header->cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = clnt,
		.task = &data->task,
		.rpc_message = &msg,
		.callback_ops = call_ops,
		.callback_data = data,
		.workqueue = nfsiod_workqueue,
		.flags = RPC_TASK_ASYNC | flags,
		.priority = priority,
	};
	int ret = 0;

	/* Set up the initial task struct.  */
	NFS_PROTO(inode)->write_setup(data, &msg);

	dprintk("NFS: %5u initiated write call "
		"(req %s/%lld, %u bytes @ offset %llu)\n",
		data->task.tk_pid,
		inode->i_sb->s_id,
		(long long)NFS_FILEID(inode),
		data->args.count,
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
EXPORT_SYMBOL_GPL(nfs_initiate_write);

/*
 * Set up the argument/result storage required for the RPC call.
 */
static void nfs_write_rpcsetup(struct nfs_write_data *data,
		unsigned int count, unsigned int offset,
		int how, struct nfs_commit_info *cinfo)
{
	struct nfs_page *req = data->header->req;

	/* Set up the RPC argument and reply structs
	 * NB: take care not to mess about with data->commit et al. */

	data->args.fh     = NFS_FH(data->header->inode);
	data->args.offset = req_offset(req) + offset;
	/* pnfs_set_layoutcommit needs this */
	data->mds_offset = data->args.offset;
	data->args.pgbase = req->wb_pgbase + offset;
	data->args.pages  = data->pages.pagevec;
	data->args.count  = count;
	data->args.context = get_nfs_open_context(req->wb_context);
	data->args.lock_context = req->wb_lock_context;
	data->args.stable  = NFS_UNSTABLE;
	switch (how & (FLUSH_STABLE | FLUSH_COND_STABLE)) {
	case 0:
		break;
	case FLUSH_COND_STABLE:
		if (nfs_reqs_to_commit(cinfo))
			break;
	default:
		data->args.stable = NFS_FILE_SYNC;
	}

	data->res.fattr   = &data->fattr;
	data->res.count   = count;
	data->res.verf    = &data->verf;
	nfs_fattr_init(&data->fattr);
}

static int nfs_do_write(struct nfs_write_data *data,
		const struct rpc_call_ops *call_ops,
		int how)
{
	struct inode *inode = data->header->inode;

	return nfs_initiate_write(NFS_CLIENT(inode), data, call_ops, how, 0);
}

static int nfs_do_multiple_writes(struct list_head *head,
		const struct rpc_call_ops *call_ops,
		int how)
{
	struct nfs_write_data *data;
	int ret = 0;

	while (!list_empty(head)) {
		int ret2;

		data = list_first_entry(head, struct nfs_write_data, list);
		list_del_init(&data->list);
		
		ret2 = nfs_do_write(data, call_ops, how);
		 if (ret == 0)
			 ret = ret2;
	}
	return ret;
}

/* If a nfs_flush_* function fails, it should remove reqs from @head and
 * call this on each, which will prepare them to be retried on next
 * writeback using standard nfs.
 */
static void nfs_redirty_request(struct nfs_page *req)
{
	nfs_mark_request_dirty(req);
	nfs_unlock_request(req);
	nfs_end_page_writeback(req->wb_page);
	nfs_release_request(req);
}

static void nfs_async_write_error(struct list_head *head)
{
	struct nfs_page	*req;

	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_redirty_request(req);
	}
}

static const struct nfs_pgio_completion_ops nfs_async_write_completion_ops = {
	.error_cleanup = nfs_async_write_error,
	.completion = nfs_write_completion,
};

static void nfs_flush_error(struct nfs_pageio_descriptor *desc,
		struct nfs_pgio_header *hdr)
{
	set_bit(NFS_IOHDR_REDO, &hdr->flags);
	while (!list_empty(&hdr->rpc_list)) {
		struct nfs_write_data *data = list_first_entry(&hdr->rpc_list,
				struct nfs_write_data, list);
		list_del(&data->list);
		nfs_writedata_release(data);
	}
	desc->pg_completion_ops->error_cleanup(&desc->pg_list);
}

/*
 * Generate multiple small requests to write out a single
 * contiguous dirty area on one page.
 */
static int nfs_flush_multi(struct nfs_pageio_descriptor *desc,
			   struct nfs_pgio_header *hdr)
{
	struct nfs_page *req = hdr->req;
	struct page *page = req->wb_page;
	struct nfs_write_data *data;
	size_t wsize = desc->pg_bsize, nbytes;
	unsigned int offset;
	int requests = 0;
	struct nfs_commit_info cinfo;

	nfs_init_cinfo(&cinfo, desc->pg_inode, desc->pg_dreq);

	if ((desc->pg_ioflags & FLUSH_COND_STABLE) &&
	    (desc->pg_moreio || nfs_reqs_to_commit(&cinfo) ||
	     desc->pg_count > wsize))
		desc->pg_ioflags &= ~FLUSH_COND_STABLE;


	offset = 0;
	nbytes = desc->pg_count;
	do {
		size_t len = min(nbytes, wsize);

		data = nfs_writedata_alloc(hdr, 1);
		if (!data) {
			nfs_flush_error(desc, hdr);
			return -ENOMEM;
		}
		data->pages.pagevec[0] = page;
		nfs_write_rpcsetup(data, len, offset, desc->pg_ioflags, &cinfo);
		list_add(&data->list, &hdr->rpc_list);
		requests++;
		nbytes -= len;
		offset += len;
	} while (nbytes != 0);
	nfs_list_remove_request(req);
	nfs_list_add_request(req, &hdr->pages);
	desc->pg_rpc_callops = &nfs_write_common_ops;
	return 0;
}

/*
 * Create an RPC task for the given write request and kick it.
 * The page must have been locked by the caller.
 *
 * It may happen that the page we're passed is not marked dirty.
 * This is the case if nfs_updatepage detects a conflicting request
 * that has been written but not committed.
 */
static int nfs_flush_one(struct nfs_pageio_descriptor *desc,
			 struct nfs_pgio_header *hdr)
{
	struct nfs_page		*req;
	struct page		**pages;
	struct nfs_write_data	*data;
	struct list_head *head = &desc->pg_list;
	struct nfs_commit_info cinfo;

	data = nfs_writedata_alloc(hdr, nfs_page_array_len(desc->pg_base,
							   desc->pg_count));
	if (!data) {
		nfs_flush_error(desc, hdr);
		return -ENOMEM;
	}

	nfs_init_cinfo(&cinfo, desc->pg_inode, desc->pg_dreq);
	pages = data->pages.pagevec;
	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_list_add_request(req, &hdr->pages);
		*pages++ = req->wb_page;
	}

	if ((desc->pg_ioflags & FLUSH_COND_STABLE) &&
	    (desc->pg_moreio || nfs_reqs_to_commit(&cinfo)))
		desc->pg_ioflags &= ~FLUSH_COND_STABLE;

	/* Set up the argument struct */
	nfs_write_rpcsetup(data, desc->pg_count, 0, desc->pg_ioflags, &cinfo);
	list_add(&data->list, &hdr->rpc_list);
	desc->pg_rpc_callops = &nfs_write_common_ops;
	return 0;
}

int nfs_generic_flush(struct nfs_pageio_descriptor *desc,
		      struct nfs_pgio_header *hdr)
{
	if (desc->pg_bsize < PAGE_CACHE_SIZE)
		return nfs_flush_multi(desc, hdr);
	return nfs_flush_one(desc, hdr);
}

static int nfs_generic_pg_writepages(struct nfs_pageio_descriptor *desc)
{
	struct nfs_write_header *whdr;
	struct nfs_pgio_header *hdr;
	int ret;

	whdr = nfs_writehdr_alloc();
	if (!whdr) {
		desc->pg_completion_ops->error_cleanup(&desc->pg_list);
		return -ENOMEM;
	}
	hdr = &whdr->header;
	nfs_pgheader_init(desc, hdr, nfs_writehdr_free);
	atomic_inc(&hdr->refcnt);
	ret = nfs_generic_flush(desc, hdr);
	if (ret == 0)
		ret = nfs_do_multiple_writes(&hdr->rpc_list,
					     desc->pg_rpc_callops,
					     desc->pg_ioflags);
	if (atomic_dec_and_test(&hdr->refcnt))
		hdr->completion_ops->completion(hdr);
	return ret;
}

static const struct nfs_pageio_ops nfs_pageio_write_ops = {
	.pg_test = nfs_generic_pg_test,
	.pg_doio = nfs_generic_pg_writepages,
};

void nfs_pageio_init_write_mds(struct nfs_pageio_descriptor *pgio,
			       struct inode *inode, int ioflags,
			       const struct nfs_pgio_completion_ops *compl_ops)
{
	nfs_pageio_init(pgio, inode, &nfs_pageio_write_ops, compl_ops,
				NFS_SERVER(inode)->wsize, ioflags);
}

void nfs_pageio_reset_write_mds(struct nfs_pageio_descriptor *pgio)
{
	pgio->pg_ops = &nfs_pageio_write_ops;
	pgio->pg_bsize = NFS_SERVER(pgio->pg_inode)->wsize;
}
EXPORT_SYMBOL_GPL(nfs_pageio_reset_write_mds);

void nfs_pageio_init_write(struct nfs_pageio_descriptor *pgio,
			   struct inode *inode, int ioflags,
			   const struct nfs_pgio_completion_ops *compl_ops)
{
	if (!pnfs_pageio_init_write(pgio, inode, ioflags, compl_ops))
		nfs_pageio_init_write_mds(pgio, inode, ioflags, compl_ops);
}

void nfs_write_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs_write_data *data = calldata;
	NFS_PROTO(data->header->inode)->write_rpc_prepare(task, data);
}

void nfs_commit_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs_commit_data *data = calldata;

	NFS_PROTO(data->inode)->commit_rpc_prepare(task, data);
}

/*
 * Handle a write reply that flushes a whole page.
 *
 * FIXME: There is an inherent race with invalidate_inode_pages and
 *	  writebacks since the page->count is kept > 1 for as long
 *	  as the page has a write request pending.
 */
static void nfs_writeback_done_common(struct rpc_task *task, void *calldata)
{
	struct nfs_write_data	*data = calldata;

	nfs_writeback_done(task, data);
}

static void nfs_writeback_release_common(void *calldata)
{
	struct nfs_write_data	*data = calldata;
	struct nfs_pgio_header *hdr = data->header;
	int status = data->task.tk_status;

	if ((status >= 0) && nfs_write_need_commit(data)) {
		spin_lock(&hdr->lock);
		if (test_bit(NFS_IOHDR_NEED_RESCHED, &hdr->flags))
			; /* Do nothing */
		else if (!test_and_set_bit(NFS_IOHDR_NEED_COMMIT, &hdr->flags))
			memcpy(hdr->verf, &data->verf, sizeof(*hdr->verf));
		else if (memcmp(hdr->verf, &data->verf, sizeof(*hdr->verf)))
			set_bit(NFS_IOHDR_NEED_RESCHED, &hdr->flags);
		spin_unlock(&hdr->lock);
	}
	nfs_writedata_release(data);
}

static const struct rpc_call_ops nfs_write_common_ops = {
	.rpc_call_prepare = nfs_write_prepare,
	.rpc_call_done = nfs_writeback_done_common,
	.rpc_release = nfs_writeback_release_common,
};


/*
 * This function is called when the WRITE call is complete.
 */
void nfs_writeback_done(struct rpc_task *task, struct nfs_write_data *data)
{
	struct nfs_writeargs	*argp = &data->args;
	struct nfs_writeres	*resp = &data->res;
	struct inode		*inode = data->header->inode;
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
	status = NFS_PROTO(inode)->write_done(task, data);
	if (status != 0)
		return;
	nfs_add_stats(inode, NFSIOS_SERVERWRITTENBYTES, resp->count);

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

		/* Note this will print the MDS for a DS write */
		if (time_before(complain, jiffies)) {
			dprintk("NFS:       faulty NFS server %s:"
				" (committed = %d) != (stable = %d)\n",
				NFS_SERVER(inode)->nfs_client->cl_hostname,
				resp->verf->committed, argp->stable);
			complain = jiffies + 300 * HZ;
		}
	}
#endif
	if (task->tk_status < 0)
		nfs_set_pgio_error(data->header, task->tk_status, argp->offset);
	else if (resp->count < argp->count) {
		static unsigned long    complain;

		/* This a short write! */
		nfs_inc_stats(inode, NFSIOS_SHORTWRITE);

		/* Has the server at least made some progress? */
		if (resp->count == 0) {
			if (time_before(complain, jiffies)) {
				printk(KERN_WARNING
				       "NFS: Server wrote zero bytes, expected %u.\n",
				       argp->count);
				complain = jiffies + 300 * HZ;
			}
			nfs_set_pgio_error(data->header, -EIO, argp->offset);
			task->tk_status = -EIO;
			return;
		}
		/* Was this an NFSv2 write or an NFSv3 stable write? */
		if (resp->verf->committed != NFS_UNSTABLE) {
			/* Resend from where the server left off */
			data->mds_offset += resp->count;
			argp->offset += resp->count;
			argp->pgbase += resp->count;
			argp->count -= resp->count;
		} else {
			/* Resend as a stable write in order to avoid
			 * headaches in the case of a server crash.
			 */
			argp->stable = NFS_FILE_SYNC;
		}
		rpc_restart_call_prepare(task);
	}
}


#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
static int nfs_commit_set_lock(struct nfs_inode *nfsi, int may_wait)
{
	int ret;

	if (!test_and_set_bit(NFS_INO_COMMIT, &nfsi->flags))
		return 1;
	if (!may_wait)
		return 0;
	ret = out_of_line_wait_on_bit_lock(&nfsi->flags,
				NFS_INO_COMMIT,
				nfs_wait_bit_killable,
				TASK_KILLABLE);
	return (ret < 0) ? ret : 1;
}

static void nfs_commit_clear_lock(struct nfs_inode *nfsi)
{
	clear_bit(NFS_INO_COMMIT, &nfsi->flags);
	smp_mb__after_clear_bit();
	wake_up_bit(&nfsi->flags, NFS_INO_COMMIT);
}

void nfs_commitdata_release(struct nfs_commit_data *data)
{
	put_nfs_open_context(data->context);
	nfs_commit_free(data);
}
EXPORT_SYMBOL_GPL(nfs_commitdata_release);

int nfs_initiate_commit(struct rpc_clnt *clnt, struct nfs_commit_data *data,
			const struct rpc_call_ops *call_ops,
			int how, int flags)
{
	struct rpc_task *task;
	int priority = flush_task_priority(how);
	struct rpc_message msg = {
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->cred,
	};
	struct rpc_task_setup task_setup_data = {
		.task = &data->task,
		.rpc_client = clnt,
		.rpc_message = &msg,
		.callback_ops = call_ops,
		.callback_data = data,
		.workqueue = nfsiod_workqueue,
		.flags = RPC_TASK_ASYNC | flags,
		.priority = priority,
	};
	/* Set up the initial task struct.  */
	NFS_PROTO(data->inode)->commit_setup(data, &msg);

	dprintk("NFS: %5u initiated commit call\n", data->task.tk_pid);

	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	if (how & FLUSH_SYNC)
		rpc_wait_for_completion_task(task);
	rpc_put_task(task);
	return 0;
}
EXPORT_SYMBOL_GPL(nfs_initiate_commit);

/*
 * Set up the argument/result storage required for the RPC call.
 */
void nfs_init_commit(struct nfs_commit_data *data,
		     struct list_head *head,
		     struct pnfs_layout_segment *lseg,
		     struct nfs_commit_info *cinfo)
{
	struct nfs_page *first = nfs_list_entry(head->next);
	struct inode *inode = first->wb_context->dentry->d_inode;

	/* Set up the RPC argument and reply structs
	 * NB: take care not to mess about with data->commit et al. */

	list_splice_init(head, &data->pages);

	data->inode	  = inode;
	data->cred	  = first->wb_context->cred;
	data->lseg	  = lseg; /* reference transferred */
	data->mds_ops     = &nfs_commit_ops;
	data->completion_ops = cinfo->completion_ops;
	data->dreq	  = cinfo->dreq;

	data->args.fh     = NFS_FH(data->inode);
	/* Note: we always request a commit of the entire inode */
	data->args.offset = 0;
	data->args.count  = 0;
	data->context     = get_nfs_open_context(first->wb_context);
	data->res.fattr   = &data->fattr;
	data->res.verf    = &data->verf;
	nfs_fattr_init(&data->fattr);
}
EXPORT_SYMBOL_GPL(nfs_init_commit);

void nfs_retry_commit(struct list_head *page_list,
		      struct pnfs_layout_segment *lseg,
		      struct nfs_commit_info *cinfo)
{
	struct nfs_page *req;

	while (!list_empty(page_list)) {
		req = nfs_list_entry(page_list->next);
		nfs_list_remove_request(req);
		nfs_mark_request_commit(req, lseg, cinfo);
		if (!cinfo->dreq) {
			dec_zone_page_state(req->wb_page, NR_UNSTABLE_NFS);
			dec_bdi_stat(req->wb_page->mapping->backing_dev_info,
				     BDI_RECLAIMABLE);
		}
		nfs_unlock_and_release_request(req);
	}
}
EXPORT_SYMBOL_GPL(nfs_retry_commit);

/*
 * Commit dirty pages
 */
static int
nfs_commit_list(struct inode *inode, struct list_head *head, int how,
		struct nfs_commit_info *cinfo)
{
	struct nfs_commit_data	*data;

	data = nfs_commitdata_alloc();

	if (!data)
		goto out_bad;

	/* Set up the argument struct */
	nfs_init_commit(data, head, NULL, cinfo);
	atomic_inc(&cinfo->mds->rpcs_out);
	return nfs_initiate_commit(NFS_CLIENT(inode), data, data->mds_ops,
				   how, 0);
 out_bad:
	nfs_retry_commit(head, NULL, cinfo);
	cinfo->completion_ops->error_cleanup(NFS_I(inode));
	return -ENOMEM;
}

/*
 * COMMIT call returned
 */
static void nfs_commit_done(struct rpc_task *task, void *calldata)
{
	struct nfs_commit_data	*data = calldata;

        dprintk("NFS: %5u nfs_commit_done (status %d)\n",
                                task->tk_pid, task->tk_status);

	/* Call the NFS version-specific code */
	NFS_PROTO(data->inode)->commit_done(task, data);
}

static void nfs_commit_release_pages(struct nfs_commit_data *data)
{
	struct nfs_page	*req;
	int status = data->task.tk_status;
	struct nfs_commit_info cinfo;

	while (!list_empty(&data->pages)) {
		req = nfs_list_entry(data->pages.next);
		nfs_list_remove_request(req);
		nfs_clear_page_commit(req->wb_page);

		dprintk("NFS:       commit (%s/%lld %d@%lld)",
			req->wb_context->dentry->d_sb->s_id,
			(long long)NFS_FILEID(req->wb_context->dentry->d_inode),
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
		nfs_unlock_and_release_request(req);
	}
	nfs_init_cinfo(&cinfo, data->inode, data->dreq);
	if (atomic_dec_and_test(&cinfo.mds->rpcs_out))
		nfs_commit_clear_lock(NFS_I(data->inode));
}

static void nfs_commit_release(void *calldata)
{
	struct nfs_commit_data *data = calldata;

	data->completion_ops->completion(data);
	nfs_commitdata_release(calldata);
}

static const struct rpc_call_ops nfs_commit_ops = {
	.rpc_call_prepare = nfs_commit_prepare,
	.rpc_call_done = nfs_commit_done,
	.rpc_release = nfs_commit_release,
};

static const struct nfs_commit_completion_ops nfs_commit_completion_ops = {
	.completion = nfs_commit_release_pages,
	.error_cleanup = nfs_commit_clear_lock,
};

int nfs_generic_commit_list(struct inode *inode, struct list_head *head,
			    int how, struct nfs_commit_info *cinfo)
{
	int status;

	status = pnfs_commit_list(inode, head, how, cinfo);
	if (status == PNFS_NOT_ATTEMPTED)
		status = nfs_commit_list(inode, head, how, cinfo);
	return status;
}

int nfs_commit_inode(struct inode *inode, int how)
{
	LIST_HEAD(head);
	struct nfs_commit_info cinfo;
	int may_wait = how & FLUSH_SYNC;
	int res;

	res = nfs_commit_set_lock(NFS_I(inode), may_wait);
	if (res <= 0)
		goto out_mark_dirty;
	nfs_init_cinfo_from_inode(&cinfo, inode);
	res = nfs_scan_commit(inode, &head, &cinfo);
	if (res) {
		int error;

		error = nfs_generic_commit_list(inode, &head, how, &cinfo);
		if (error < 0)
			return error;
		if (!may_wait)
			goto out_mark_dirty;
		error = wait_on_bit(&NFS_I(inode)->flags,
				NFS_INO_COMMIT,
				nfs_wait_bit_killable,
				TASK_KILLABLE);
		if (error < 0)
			return error;
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

	/* no commits means nothing needs to be done */
	if (!nfsi->commit_info.ncommit)
		return ret;

	if (wbc->sync_mode == WB_SYNC_NONE) {
		/* Don't commit yet if this is a non-blocking flush and there
		 * are a lot of outstanding writes for this mapping.
		 */
		if (nfsi->commit_info.ncommit <= (nfsi->npages >> 1))
			goto out_mark_dirty;

		/* don't wait for the COMMIT response */
		flags = 0;
	}

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
	int ret;

	ret = nfs_commit_unstable_pages(inode, wbc);
	if (ret >= 0 && test_bit(NFS_INO_LAYOUTCOMMIT, &NFS_I(inode)->flags)) {
		int status;
		bool sync = true;

		if (wbc->sync_mode == WB_SYNC_NONE)
			sync = false;

		status = pnfs_layoutcommit_inode(inode, sync);
		if (status < 0)
			return status;
	}
	return ret;
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
		if (nfs_lock_request(req)) {
			nfs_clear_request_commit(req);
			nfs_inode_remove_request(req);
			/*
			 * In case nfs_inode_remove_request has marked the
			 * page as being dirty
			 */
			cancel_dirty_page(page, PAGE_CACHE_SIZE);
			nfs_unlock_and_release_request(req);
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
		struct page *page, enum migrate_mode mode)
{
	/*
	 * If PagePrivate is set, then the page is currently associated with
	 * an in-progress read or write request. Don't try to migrate it.
	 *
	 * FIXME: we could do this in principle, but we'll need a way to ensure
	 *        that we can safely release the inode reference while holding
	 *        the page lock.
	 */
	if (PagePrivate(page))
		return -EBUSY;

	nfs_fscache_release_page(page, GFP_KERNEL);

	return migrate_page(mapping, newpage, page, mode);
}
#endif

int __init nfs_init_writepagecache(void)
{
	nfs_wdata_cachep = kmem_cache_create("nfs_write_data",
					     sizeof(struct nfs_write_header),
					     0, SLAB_HWCACHE_ALIGN,
					     NULL);
	if (nfs_wdata_cachep == NULL)
		return -ENOMEM;

	nfs_wdata_mempool = mempool_create_slab_pool(MIN_POOL_WRITE,
						     nfs_wdata_cachep);
	if (nfs_wdata_mempool == NULL)
		return -ENOMEM;

	nfs_cdata_cachep = kmem_cache_create("nfs_commit_data",
					     sizeof(struct nfs_commit_data),
					     0, SLAB_HWCACHE_ALIGN,
					     NULL);
	if (nfs_cdata_cachep == NULL)
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

