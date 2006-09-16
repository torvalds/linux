/*
 * linux/fs/nfs/write.c
 *
 * Writing file data over NFS.
 *
 * We do it like this: When a (user) process wishes to write data to an
 * NFS file, a write request is allocated that contains the RPC task data
 * plus some info on the page to be written, and added to the inode's
 * write chain. If the process writes past the end of the page, an async
 * RPC call to write the page is scheduled immediately; otherwise, the call
 * is delayed for a few seconds.
 *
 * Just like readahead, no async I/O is performed if wsize < PAGE_SIZE.
 *
 * Write requests are kept on the inode's writeback list. Each entry in
 * that list references the page (portion) to be written. When the
 * cache timeout has expired, the RPC task is woken up, and tries to
 * lock the page. As soon as it manages to do so, the request is moved
 * from the writeback list to the writelock list.
 *
 * Note: we must make sure never to confuse the inode passed in the
 * write_page request with the one in page->inode. As far as I understand
 * it, these are different when doing a swap-out.
 *
 * To understand everything that goes on here and in the NFS read code,
 * one should be aware that a page is locked in exactly one of the following
 * cases:
 *
 *  -	A write request is in progress.
 *  -	A user process is in generic_file_write/nfs_update_page
 *  -	A user process is in generic_file_read
 *
 * Also note that because of the way pages are invalidated in
 * nfs_revalidate_inode, the following assertions hold:
 *
 *  -	If a page is dirty, there will be no read requests (a page will
 *	not be re-read unless invalidated by nfs_revalidate_inode).
 *  -	If the page is not uptodate, there will be no pending write
 *	requests, and no process will be in nfs_update_page.
 *
 * FIXME: Interaction with the vmscan routines is not optimal yet.
 * Either vmscan must be made nfs-savvy, or we need a different page
 * reclaim concept that supports something like FS-independent
 * buffer_heads with a b_ops-> field.
 *
 * Copyright (C) 1996, 1997, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/writeback.h>

#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs_page.h>
#include <linux/backing-dev.h>

#include <asm/uaccess.h>
#include <linux/smp_lock.h>

#include "delegation.h"
#include "iostat.h"

#define NFSDBG_FACILITY		NFSDBG_PAGECACHE

#define MIN_POOL_WRITE		(32)
#define MIN_POOL_COMMIT		(4)

/*
 * Local function declarations
 */
static struct nfs_page * nfs_update_request(struct nfs_open_context*,
					    struct inode *,
					    struct page *,
					    unsigned int, unsigned int);
static int nfs_wait_on_write_congestion(struct address_space *, int);
static int nfs_wait_on_requests(struct inode *, unsigned long, unsigned int);
static int nfs_flush_mapping(struct address_space *mapping, struct writeback_control *wbc, int how);
static const struct rpc_call_ops nfs_write_partial_ops;
static const struct rpc_call_ops nfs_write_full_ops;
static const struct rpc_call_ops nfs_commit_ops;

static kmem_cache_t *nfs_wdata_cachep;
static mempool_t *nfs_wdata_mempool;
static mempool_t *nfs_commit_mempool;

static DECLARE_WAIT_QUEUE_HEAD(nfs_write_congestion);

struct nfs_write_data *nfs_commit_alloc(void)
{
	struct nfs_write_data *p = mempool_alloc(nfs_commit_mempool, SLAB_NOFS);

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

struct nfs_write_data *nfs_writedata_alloc(size_t len)
{
	unsigned int pagecount = (len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	struct nfs_write_data *p = mempool_alloc(nfs_wdata_mempool, SLAB_NOFS);

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

/* Adjust the file length if we're writing beyond the end */
static void nfs_grow_file(struct page *page, unsigned int offset, unsigned int count)
{
	struct inode *inode = page->mapping->host;
	loff_t end, i_size = i_size_read(inode);
	unsigned long end_index = (i_size - 1) >> PAGE_CACHE_SHIFT;

	if (i_size > 0 && page->index < end_index)
		return;
	end = ((loff_t)page->index << PAGE_CACHE_SHIFT) + ((loff_t)offset+count);
	if (i_size >= end)
		return;
	nfs_inc_stats(inode, NFSIOS_EXTENDWRITE);
	i_size_write(inode, end);
}

/* We can set the PG_uptodate flag if we see that a write request
 * covers the full page.
 */
static void nfs_mark_uptodate(struct page *page, unsigned int base, unsigned int count)
{
	loff_t end_offs;

	if (PageUptodate(page))
		return;
	if (base != 0)
		return;
	if (count == PAGE_CACHE_SIZE) {
		SetPageUptodate(page);
		return;
	}

	end_offs = i_size_read(page->mapping->host) - 1;
	if (end_offs < 0)
		return;
	/* Is this the last page? */
	if (page->index != (unsigned long)(end_offs >> PAGE_CACHE_SHIFT))
		return;
	/* This is the last page: set PG_uptodate if we cover the entire
	 * extent of the data, then zero the rest of the page.
	 */
	if (count == (unsigned int)(end_offs & (PAGE_CACHE_SIZE - 1)) + 1) {
		memclear_highpage_flush(page, count, PAGE_CACHE_SIZE - count);
		SetPageUptodate(page);
	}
}

/*
 * Write a page synchronously.
 * Offset is the data offset within the page.
 */
static int nfs_writepage_sync(struct nfs_open_context *ctx, struct inode *inode,
		struct page *page, unsigned int offset, unsigned int count,
		int how)
{
	unsigned int	wsize = NFS_SERVER(inode)->wsize;
	int		result, written = 0;
	struct nfs_write_data *wdata;

	wdata = nfs_writedata_alloc(wsize);
	if (!wdata)
		return -ENOMEM;

	wdata->flags = how;
	wdata->cred = ctx->cred;
	wdata->inode = inode;
	wdata->args.fh = NFS_FH(inode);
	wdata->args.context = ctx;
	wdata->args.pages = &page;
	wdata->args.stable = NFS_FILE_SYNC;
	wdata->args.pgbase = offset;
	wdata->args.count = wsize;
	wdata->res.fattr = &wdata->fattr;
	wdata->res.verf = &wdata->verf;

	dprintk("NFS:      nfs_writepage_sync(%s/%Ld %d@%Ld)\n",
		inode->i_sb->s_id,
		(long long)NFS_FILEID(inode),
		count, (long long)(page_offset(page) + offset));

	set_page_writeback(page);
	nfs_begin_data_update(inode);
	do {
		if (count < wsize)
			wdata->args.count = count;
		wdata->args.offset = page_offset(page) + wdata->args.pgbase;

		result = NFS_PROTO(inode)->write(wdata);

		if (result < 0) {
			/* Must mark the page invalid after I/O error */
			ClearPageUptodate(page);
			goto io_error;
		}
		if (result < wdata->args.count)
			printk(KERN_WARNING "NFS: short write, count=%u, result=%d\n",
					wdata->args.count, result);

		wdata->args.offset += result;
	        wdata->args.pgbase += result;
		written += result;
		count -= result;
		nfs_add_stats(inode, NFSIOS_SERVERWRITTENBYTES, result);
	} while (count);
	/* Update file length */
	nfs_grow_file(page, offset, written);
	/* Set the PG_uptodate flag? */
	nfs_mark_uptodate(page, offset, written);

	if (PageError(page))
		ClearPageError(page);

io_error:
	nfs_end_data_update(inode);
	end_page_writeback(page);
	nfs_writedata_release(wdata);
	return written ? written : result;
}

static int nfs_writepage_async(struct nfs_open_context *ctx,
		struct inode *inode, struct page *page,
		unsigned int offset, unsigned int count)
{
	struct nfs_page	*req;

	req = nfs_update_request(ctx, inode, page, offset, count);
	if (IS_ERR(req))
		return PTR_ERR(req);
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
		return FLUSH_HIGHPRI;
	if (wbc->for_kupdate)
		return FLUSH_LOWPRI;
	return 0;
}

/*
 * Write an mmapped page to the server.
 */
int nfs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct nfs_open_context *ctx;
	struct inode *inode = page->mapping->host;
	unsigned long end_index;
	unsigned offset = PAGE_CACHE_SIZE;
	loff_t i_size = i_size_read(inode);
	int inode_referenced = 0;
	int priority = wb_priority(wbc);
	int err;

	nfs_inc_stats(inode, NFSIOS_VFSWRITEPAGE);
	nfs_add_stats(inode, NFSIOS_WRITEPAGES, 1);

	/*
	 * Note: We need to ensure that we have a reference to the inode
	 *       if we are to do asynchronous writes. If not, waiting
	 *       in nfs_wait_on_request() may deadlock with clear_inode().
	 *
	 *       If igrab() fails here, then it is in any case safe to
	 *       call nfs_wb_page(), since there will be no pending writes.
	 */
	if (igrab(inode) != 0)
		inode_referenced = 1;
	end_index = i_size >> PAGE_CACHE_SHIFT;

	/* Ensure we've flushed out any previous writes */
	nfs_wb_page_priority(inode, page, priority);

	/* easy case */
	if (page->index < end_index)
		goto do_it;
	/* things got complicated... */
	offset = i_size & (PAGE_CACHE_SIZE-1);

	/* OK, are we completely out? */
	err = 0; /* potential race with truncate - ignore */
	if (page->index >= end_index+1 || !offset)
		goto out;
do_it:
	ctx = nfs_find_open_context(inode, NULL, FMODE_WRITE);
	if (ctx == NULL) {
		err = -EBADF;
		goto out;
	}
	lock_kernel();
	if (!IS_SYNC(inode) && inode_referenced) {
		err = nfs_writepage_async(ctx, inode, page, 0, offset);
		if (!wbc->for_writepages)
			nfs_flush_mapping(page->mapping, wbc, wb_priority(wbc));
	} else {
		err = nfs_writepage_sync(ctx, inode, page, 0,
						offset, priority);
		if (err >= 0) {
			if (err != offset)
				redirty_page_for_writepage(wbc, page);
			err = 0;
		}
	}
	unlock_kernel();
	put_nfs_open_context(ctx);
out:
	unlock_page(page);
	if (inode_referenced)
		iput(inode);
	return err; 
}

/*
 * Note: causes nfs_update_request() to block on the assumption
 * 	 that the writeback is generated due to memory pressure.
 */
int nfs_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	struct backing_dev_info *bdi = mapping->backing_dev_info;
	struct inode *inode = mapping->host;
	int err;

	nfs_inc_stats(inode, NFSIOS_VFSWRITEPAGES);

	err = generic_writepages(mapping, wbc);
	if (err)
		return err;
	while (test_and_set_bit(BDI_write_congested, &bdi->state) != 0) {
		if (wbc->nonblocking)
			return 0;
		nfs_wait_on_write_congestion(mapping, 0);
	}
	err = nfs_flush_mapping(mapping, wbc, wb_priority(wbc));
	if (err < 0)
		goto out;
	nfs_add_stats(inode, NFSIOS_WRITEPAGES, err);
	if (!wbc->nonblocking && wbc->sync_mode == WB_SYNC_ALL) {
		err = nfs_wait_on_requests(inode, 0, 0);
		if (err < 0)
			goto out;
	}
	err = nfs_commit_inode(inode, wb_priority(wbc));
	if (err > 0) {
		wbc->nr_to_write -= err;
		err = 0;
	}
out:
	clear_bit(BDI_write_congested, &bdi->state);
	wake_up_all(&nfs_write_congestion);
	congestion_end(WRITE);
	return err;
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
	nfsi->npages++;
	atomic_inc(&req->wb_count);
	return 0;
}

/*
 * Insert a write request into an inode
 */
static void nfs_inode_remove_request(struct nfs_page *req)
{
	struct inode *inode = req->wb_context->dentry->d_inode;
	struct nfs_inode *nfsi = NFS_I(inode);

	BUG_ON (!NFS_WBACK_BUSY(req));

	spin_lock(&nfsi->req_lock);
	ClearPagePrivate(req->wb_page);
	radix_tree_delete(&nfsi->nfs_page_tree, req->wb_index);
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

/*
 * Find a request
 */
static inline struct nfs_page *
_nfs_find_request(struct inode *inode, unsigned long index)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_page *req;

	req = (struct nfs_page*)radix_tree_lookup(&nfsi->nfs_page_tree, index);
	if (req)
		atomic_inc(&req->wb_count);
	return req;
}

static struct nfs_page *
nfs_find_request(struct inode *inode, unsigned long index)
{
	struct nfs_page		*req;
	struct nfs_inode	*nfsi = NFS_I(inode);

	spin_lock(&nfsi->req_lock);
	req = _nfs_find_request(inode, index);
	spin_unlock(&nfsi->req_lock);
	return req;
}

/*
 * Add a request to the inode's dirty list.
 */
static void
nfs_mark_request_dirty(struct nfs_page *req)
{
	struct inode *inode = req->wb_context->dentry->d_inode;
	struct nfs_inode *nfsi = NFS_I(inode);

	spin_lock(&nfsi->req_lock);
	radix_tree_tag_set(&nfsi->nfs_page_tree,
			req->wb_index, NFS_PAGE_TAG_DIRTY);
	nfs_list_add_request(req, &nfsi->dirty);
	nfsi->ndirty++;
	spin_unlock(&nfsi->req_lock);
	inc_zone_page_state(req->wb_page, NR_FILE_DIRTY);
	mark_inode_dirty(inode);
}

/*
 * Check if a request is dirty
 */
static inline int
nfs_dirty_request(struct nfs_page *req)
{
	struct nfs_inode *nfsi = NFS_I(req->wb_context->dentry->d_inode);
	return !list_empty(&req->wb_list) && req->wb_list_head == &nfsi->dirty;
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
	spin_unlock(&nfsi->req_lock);
	inc_zone_page_state(req->wb_page, NR_UNSTABLE_NFS);
	mark_inode_dirty(inode);
}
#endif

/*
 * Wait for a request to complete.
 *
 * Interruptible by signals only if mounted with intr flag.
 */
static int nfs_wait_on_requests_locked(struct inode *inode, unsigned long idx_start, unsigned int npages)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_page *req;
	unsigned long		idx_end, next;
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

static int nfs_wait_on_requests(struct inode *inode, unsigned long idx_start, unsigned int npages)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int ret;

	spin_lock(&nfsi->req_lock);
	ret = nfs_wait_on_requests_locked(inode, idx_start, npages);
	spin_unlock(&nfsi->req_lock);
	return ret;
}

static void nfs_cancel_dirty_list(struct list_head *head)
{
	struct nfs_page *req;
	while(!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_inode_remove_request(req);
		nfs_clear_page_writeback(req);
	}
}

static void nfs_cancel_commit_list(struct list_head *head)
{
	struct nfs_page *req;

	while(!list_empty(head)) {
		req = nfs_list_entry(head->next);
		dec_zone_page_state(req->wb_page, NR_UNSTABLE_NFS);
		nfs_list_remove_request(req);
		nfs_inode_remove_request(req);
		nfs_unlock_request(req);
	}
}

/*
 * nfs_scan_dirty - Scan an inode for dirty requests
 * @inode: NFS inode to scan
 * @dst: destination list
 * @idx_start: lower bound of page->index to scan.
 * @npages: idx_start + npages sets the upper bound to scan.
 *
 * Moves requests from the inode's dirty page list.
 * The requests are *not* checked to ensure that they form a contiguous set.
 */
static int
nfs_scan_dirty(struct inode *inode, struct list_head *dst, unsigned long idx_start, unsigned int npages)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int res = 0;

	if (nfsi->ndirty != 0) {
		res = nfs_scan_lock_dirty(nfsi, dst, idx_start, npages);
		nfsi->ndirty -= res;
		if ((nfsi->ndirty == 0) != list_empty(&nfsi->dirty))
			printk(KERN_ERR "NFS: desynchronized value of nfs_i.ndirty.\n");
	}
	return res;
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
nfs_scan_commit(struct inode *inode, struct list_head *dst, unsigned long idx_start, unsigned int npages)
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
static inline int nfs_scan_commit(struct inode *inode, struct list_head *dst, unsigned long idx_start, unsigned int npages)
{
	return 0;
}
#endif

static int nfs_wait_on_write_congestion(struct address_space *mapping, int intr)
{
	struct backing_dev_info *bdi = mapping->backing_dev_info;
	DEFINE_WAIT(wait);
	int ret = 0;

	might_sleep();

	if (!bdi_write_congested(bdi))
		return 0;

	nfs_inc_stats(mapping->host, NFSIOS_CONGESTIONWAIT);

	if (intr) {
		struct rpc_clnt *clnt = NFS_CLIENT(mapping->host);
		sigset_t oldset;

		rpc_clnt_sigmask(clnt, &oldset);
		prepare_to_wait(&nfs_write_congestion, &wait, TASK_INTERRUPTIBLE);
		if (bdi_write_congested(bdi)) {
			if (signalled())
				ret = -ERESTARTSYS;
			else
				schedule();
		}
		rpc_clnt_sigunmask(clnt, &oldset);
	} else {
		prepare_to_wait(&nfs_write_congestion, &wait, TASK_UNINTERRUPTIBLE);
		if (bdi_write_congested(bdi))
			schedule();
	}
	finish_wait(&nfs_write_congestion, &wait);
	return ret;
}


/*
 * Try to update any existing write request, or create one if there is none.
 * In order to match, the request's credentials must match those of
 * the calling process.
 *
 * Note: Should always be called with the Page Lock held!
 */
static struct nfs_page * nfs_update_request(struct nfs_open_context* ctx,
		struct inode *inode, struct page *page,
		unsigned int offset, unsigned int bytes)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_page		*req, *new = NULL;
	unsigned long		rqend, end;

	end = offset + bytes;

	if (nfs_wait_on_write_congestion(page->mapping, server->flags & NFS_MOUNT_INTR))
		return ERR_PTR(-ERESTARTSYS);
	for (;;) {
		/* Loop over all inode entries and see if we find
		 * A request for the page we wish to update
		 */
		spin_lock(&nfsi->req_lock);
		req = _nfs_find_request(inode, page->index);
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
			nfs_mark_request_dirty(new);
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
	struct inode	*inode = page->mapping->host;
	struct nfs_page	*req;
	int		status = 0;
	/*
	 * Look for a request corresponding to this page. If there
	 * is one, and it belongs to another file, we flush it out
	 * before we try to copy anything into the page. Do this
	 * due to the lack of an ACCESS-type call in NFSv2.
	 * Also do the same if we find a request from an existing
	 * dropped page.
	 */
	req = nfs_find_request(inode, page->index);
	if (req) {
		if (req->wb_page != page || ctx != req->wb_context)
			status = nfs_wb_page(inode, page);
		nfs_release_request(req);
	}
	return (status < 0) ? status : 0;
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
	struct nfs_page	*req;
	int		status = 0;

	nfs_inc_stats(inode, NFSIOS_VFSUPDATEPAGE);

	dprintk("NFS:      nfs_updatepage(%s/%s %d@%Ld)\n",
		file->f_dentry->d_parent->d_name.name,
		file->f_dentry->d_name.name, count,
		(long long)(page_offset(page) +offset));

	if (IS_SYNC(inode)) {
		status = nfs_writepage_sync(ctx, inode, page, offset, count, 0);
		if (status > 0) {
			if (offset == 0 && status == PAGE_CACHE_SIZE)
				SetPageUptodate(page);
			return 0;
		}
		return status;
	}

	/* If we're not using byte range locks, and we know the page
	 * is entirely in cache, it may be more efficient to avoid
	 * fragmenting write requests.
	 */
	if (PageUptodate(page) && inode->i_flock == NULL && !(file->f_mode & O_SYNC)) {
		loff_t end_offs = i_size_read(inode) - 1;
		unsigned long end_index = end_offs >> PAGE_CACHE_SHIFT;

		count += offset;
		offset = 0;
		if (unlikely(end_offs < 0)) {
			/* Do nothing */
		} else if (page->index == end_index) {
			unsigned int pglen;
			pglen = (unsigned int)(end_offs & (PAGE_CACHE_SIZE-1)) + 1;
			if (count < pglen)
				count = pglen;
		} else if (page->index < end_index)
			count = PAGE_CACHE_SIZE;
	}

	/*
	 * Try to find an NFS request corresponding to this page
	 * and update it.
	 * If the existing request cannot be updated, we must flush
	 * it out now.
	 */
	do {
		req = nfs_update_request(ctx, inode, page, offset, count);
		status = (IS_ERR(req)) ? PTR_ERR(req) : 0;
		if (status != -EBUSY)
			break;
		/* Request could not be updated. Flush it out and try again */
		status = nfs_wb_page(inode, page);
	} while (status >= 0);
	if (status < 0)
		goto done;

	status = 0;

	/* Update file length */
	nfs_grow_file(page, offset, count);
	/* Set the PG_uptodate flag? */
	nfs_mark_uptodate(page, req->wb_pgbase, req->wb_bytes);
	nfs_unlock_request(req);
done:
        dprintk("NFS:      nfs_updatepage returns %d (isize %Ld)\n",
			status, (long long)i_size_read(inode));
	if (status < 0)
		ClearPageUptodate(page);
	return status;
}

static void nfs_writepage_release(struct nfs_page *req)
{
	end_page_writeback(req->wb_page);

#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
	if (!PageError(req->wb_page)) {
		if (NFS_NEED_RESCHED(req)) {
			nfs_mark_request_dirty(req);
			goto out;
		} else if (NFS_NEED_COMMIT(req)) {
			nfs_mark_request_commit(req);
			goto out;
		}
	}
	nfs_inode_remove_request(req);

out:
	nfs_clear_commit(req);
	nfs_clear_reschedule(req);
#else
	nfs_inode_remove_request(req);
#endif
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

	dprintk("NFS: %4d initiated write call (req %s/%Ld, %u bytes @ offset %Lu)\n",
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
static int nfs_flush_multi(struct inode *inode, struct list_head *head, int how)
{
	struct nfs_page *req = nfs_list_entry(head->next);
	struct page *page = req->wb_page;
	struct nfs_write_data *data;
	size_t wsize = NFS_SERVER(inode)->wsize, nbytes;
	unsigned int offset;
	int requests = 0;
	LIST_HEAD(list);

	nfs_list_remove_request(req);

	nbytes = req->wb_bytes;
	do {
		size_t len = min(nbytes, wsize);

		data = nfs_writedata_alloc(len);
		if (!data)
			goto out_bad;
		list_add(&data->pages, &list);
		requests++;
		nbytes -= len;
	} while (nbytes != 0);
	atomic_set(&req->wb_complete, requests);

	ClearPageError(page);
	set_page_writeback(page);
	offset = 0;
	nbytes = req->wb_bytes;
	do {
		data = list_entry(list.next, struct nfs_write_data, pages);
		list_del_init(&data->pages);

		data->pagevec[0] = page;

		if (nbytes > wsize) {
			nfs_write_rpcsetup(req, data, &nfs_write_partial_ops,
					wsize, offset, how);
			offset += wsize;
			nbytes -= wsize;
		} else {
			nfs_write_rpcsetup(req, data, &nfs_write_partial_ops,
					nbytes, offset, how);
			nbytes = 0;
		}
		nfs_execute_write(data);
	} while (nbytes != 0);

	return 0;

out_bad:
	while (!list_empty(&list)) {
		data = list_entry(list.next, struct nfs_write_data, pages);
		list_del(&data->pages);
		nfs_writedata_release(data);
	}
	nfs_mark_request_dirty(req);
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
static int nfs_flush_one(struct inode *inode, struct list_head *head, int how)
{
	struct nfs_page		*req;
	struct page		**pages;
	struct nfs_write_data	*data;
	unsigned int		count;

	data = nfs_writedata_alloc(NFS_SERVER(inode)->wsize);
	if (!data)
		goto out_bad;

	pages = data->pagevec;
	count = 0;
	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_list_add_request(req, &data->pages);
		ClearPageError(req->wb_page);
		set_page_writeback(req->wb_page);
		*pages++ = req->wb_page;
		count += req->wb_bytes;
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
		nfs_mark_request_dirty(req);
		nfs_clear_page_writeback(req);
	}
	return -ENOMEM;
}

static int nfs_flush_list(struct inode *inode, struct list_head *head, int npages, int how)
{
	LIST_HEAD(one_request);
	int (*flush_one)(struct inode *, struct list_head *, int);
	struct nfs_page	*req;
	int wpages = NFS_SERVER(inode)->wpages;
	int wsize = NFS_SERVER(inode)->wsize;
	int error;

	flush_one = nfs_flush_one;
	if (wsize < PAGE_CACHE_SIZE)
		flush_one = nfs_flush_multi;
	/* For single writes, FLUSH_STABLE is more efficient */
	if (npages <= wpages && npages == NFS_I(inode)->npages
			&& nfs_list_entry(head->next)->wb_bytes <= wsize)
		how |= FLUSH_STABLE;

	do {
		nfs_coalesce_requests(head, &one_request, wpages);
		req = nfs_list_entry(one_request.next);
		error = flush_one(inode, &one_request, how);
		if (error < 0)
			goto out_err;
	} while (!list_empty(head));
	return 0;
out_err:
	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_mark_request_dirty(req);
		nfs_clear_page_writeback(req);
	}
	return error;
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
		ClearPageUptodate(page);
		SetPageError(page);
		req->wb_context->error = task->tk_status;
		dprintk(", error = %d\n", task->tk_status);
	} else {
#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
		if (data->verf.committed < NFS_FILE_SYNC) {
			if (!NFS_NEED_COMMIT(req)) {
				nfs_defer_commit(req);
				memcpy(&req->wb_verf, &data->verf, sizeof(req->wb_verf));
				dprintk(" defer commit\n");
			} else if (memcmp(&req->wb_verf, &data->verf, sizeof(req->wb_verf))) {
				nfs_defer_reschedule(req);
				dprintk(" server reboot detected\n");
			}
		} else
#endif
			dprintk(" OK\n");
	}

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
			ClearPageUptodate(page);
			SetPageError(page);
			req->wb_context->error = task->tk_status;
			end_page_writeback(page);
			nfs_inode_remove_request(req);
			dprintk(", error = %d\n", task->tk_status);
			goto next;
		}
		end_page_writeback(page);

#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
		if (data->args.stable != NFS_UNSTABLE || data->verf.committed == NFS_FILE_SYNC) {
			nfs_inode_remove_request(req);
			dprintk(" OK\n");
			goto next;
		}
		memcpy(&req->wb_verf, &data->verf, sizeof(req->wb_verf));
		nfs_mark_request_commit(req);
		dprintk(" marked for commit\n");
#else
		nfs_inode_remove_request(req);
#endif
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

	dprintk("NFS: %4d nfs_writeback_done (status %d)\n",
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
	
	dprintk("NFS: %4d initiated commit call\n", data->task.tk_pid);
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

        dprintk("NFS: %4d nfs_commit_done (status %d)\n",
                                task->tk_pid, task->tk_status);

	/* Call the NFS version-specific code */
	if (NFS_PROTO(data->inode)->commit_done(task, data) != 0)
		return;

	while (!list_empty(&data->pages)) {
		req = nfs_list_entry(data->pages.next);
		nfs_list_remove_request(req);
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
		nfs_mark_request_dirty(req);
	next:
		nfs_clear_page_writeback(req);
	}
}

static const struct rpc_call_ops nfs_commit_ops = {
	.rpc_call_done = nfs_commit_done,
	.rpc_release = nfs_commit_release,
};
#else
static inline int nfs_commit_list(struct inode *inode, struct list_head *head, int how)
{
	return 0;
}
#endif

static int nfs_flush_mapping(struct address_space *mapping, struct writeback_control *wbc, int how)
{
	struct nfs_inode *nfsi = NFS_I(mapping->host);
	LIST_HEAD(head);
	pgoff_t index = wbc->range_start >> PAGE_CACHE_SHIFT;
	unsigned long npages = 1 + (wbc->range_end >> PAGE_CACHE_SHIFT) - index;
	int res;

	spin_lock(&nfsi->req_lock);
	res = nfs_scan_dirty(mapping->host, &head, index, npages);
	spin_unlock(&nfsi->req_lock);
	if (res) {
		int error = nfs_flush_list(mapping->host, &head, res, how);
		if (error < 0)
			return error;
		wbc->nr_to_write -= res;
	}
	return res;
}

#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
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
#endif

int nfs_sync_inode_wait(struct inode *inode, unsigned long idx_start,
		unsigned int npages, int how)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	LIST_HEAD(head);
	int nocommit = how & FLUSH_NOCOMMIT;
	int pages, ret;

	how &= ~FLUSH_NOCOMMIT;
	spin_lock(&nfsi->req_lock);
	do {
		ret = nfs_wait_on_requests_locked(inode, idx_start, npages);
		if (ret != 0)
			continue;
		pages = nfs_scan_dirty(inode, &head, idx_start, npages);
		if (pages != 0) {
			spin_unlock(&nfsi->req_lock);
			if (how & FLUSH_INVALIDATE) {
				nfs_cancel_dirty_list(&head);
				ret = pages;
			} else
				ret = nfs_flush_list(inode, &head, pages, how);
			spin_lock(&nfsi->req_lock);
			continue;
		}
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

	return 0;
}

void nfs_destroy_writepagecache(void)
{
	mempool_destroy(nfs_commit_mempool);
	mempool_destroy(nfs_wdata_mempool);
	kmem_cache_destroy(nfs_wdata_cachep);
}

