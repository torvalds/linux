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

#include "nfstrace.h"

#define NFSDBG_FACILITY		NFSDBG_PAGECACHE

#define MIN_POOL_WRITE		(32)
#define MIN_POOL_COMMIT		(4)

/*
 * Local function declarations
 */
static void nfs_redirty_request(struct nfs_page *req);
static const struct rpc_call_ops nfs_commit_ops;
static const struct nfs_pgio_completion_ops nfs_async_write_completion_ops;
static const struct nfs_commit_completion_ops nfs_commit_completion_ops;
static const struct nfs_rw_ops nfs_rw_write_ops;
static void nfs_clear_request_commit(struct nfs_page *req);
static void nfs_init_cinfo_from_inode(struct nfs_commit_info *cinfo,
				      struct inode *inode);
static struct nfs_page *
nfs_page_search_commits_for_head_request_locked(struct nfs_inode *nfsi,
						struct page *page);

static struct kmem_cache *nfs_wdata_cachep;
static mempool_t *nfs_wdata_mempool;
static struct kmem_cache *nfs_cdata_cachep;
static mempool_t *nfs_commit_mempool;

struct nfs_commit_data *nfs_commitdata_alloc(void)
{
	struct nfs_commit_data *p = mempool_alloc(nfs_commit_mempool, GFP_NOIO);

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

static struct nfs_pgio_header *nfs_writehdr_alloc(void)
{
	struct nfs_pgio_header *p = mempool_alloc(nfs_wdata_mempool, GFP_NOIO);

	if (p)
		memset(p, 0, sizeof(*p));
	return p;
}

static void nfs_writehdr_free(struct nfs_pgio_header *hdr)
{
	mempool_free(hdr, nfs_wdata_mempool);
}

static void nfs_context_set_write_error(struct nfs_open_context *ctx, int error)
{
	ctx->error = error;
	smp_wmb();
	set_bit(NFS_CONTEXT_ERROR_WRITE, &ctx->flags);
}

/*
 * nfs_page_find_head_request_locked - find head request associated with @page
 *
 * must be called while holding the inode lock.
 *
 * returns matching head request with reference held, or NULL if not found.
 */
static struct nfs_page *
nfs_page_find_head_request_locked(struct nfs_inode *nfsi, struct page *page)
{
	struct nfs_page *req = NULL;

	if (PagePrivate(page))
		req = (struct nfs_page *)page_private(page);
	else if (unlikely(PageSwapCache(page)))
		req = nfs_page_search_commits_for_head_request_locked(nfsi,
			page);

	if (req) {
		WARN_ON_ONCE(req->wb_head != req);
		kref_get(&req->wb_kref);
	}

	return req;
}

/*
 * nfs_page_find_head_request - find head request associated with @page
 *
 * returns matching head request with reference held, or NULL if not found.
 */
static struct nfs_page *nfs_page_find_head_request(struct page *page)
{
	struct inode *inode = page_file_mapping(page)->host;
	struct nfs_page *req = NULL;

	spin_lock(&inode->i_lock);
	req = nfs_page_find_head_request_locked(NFS_I(inode), page);
	spin_unlock(&inode->i_lock);
	return req;
}

/* Adjust the file length if we're writing beyond the end */
static void nfs_grow_file(struct page *page, unsigned int offset, unsigned int count)
{
	struct inode *inode = page_file_mapping(page)->host;
	loff_t end, i_size;
	pgoff_t end_index;

	spin_lock(&inode->i_lock);
	i_size = i_size_read(inode);
	end_index = (i_size - 1) >> PAGE_CACHE_SHIFT;
	if (i_size > 0 && page_file_index(page) < end_index)
		goto out;
	end = page_file_offset(page) + ((loff_t)offset+count);
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
	nfs_zap_mapping(page_file_mapping(page)->host, page_file_mapping(page));
}

/*
 * nfs_page_group_search_locked
 * @head - head request of page group
 * @page_offset - offset into page
 *
 * Search page group with head @head to find a request that contains the
 * page offset @page_offset.
 *
 * Returns a pointer to the first matching nfs request, or NULL if no
 * match is found.
 *
 * Must be called with the page group lock held
 */
static struct nfs_page *
nfs_page_group_search_locked(struct nfs_page *head, unsigned int page_offset)
{
	struct nfs_page *req;

	WARN_ON_ONCE(head != head->wb_head);
	WARN_ON_ONCE(!test_bit(PG_HEADLOCK, &head->wb_head->wb_flags));

	req = head;
	do {
		if (page_offset >= req->wb_pgbase &&
		    page_offset < (req->wb_pgbase + req->wb_bytes))
			return req;

		req = req->wb_this_page;
	} while (req != head);

	return NULL;
}

/*
 * nfs_page_group_covers_page
 * @head - head request of page group
 *
 * Return true if the page group with head @head covers the whole page,
 * returns false otherwise
 */
static bool nfs_page_group_covers_page(struct nfs_page *req)
{
	struct nfs_page *tmp;
	unsigned int pos = 0;
	unsigned int len = nfs_page_length(req->wb_page);

	nfs_page_group_lock(req, false);

	do {
		tmp = nfs_page_group_search_locked(req->wb_head, pos);
		if (tmp) {
			/* no way this should happen */
			WARN_ON_ONCE(tmp->wb_pgbase != pos);
			pos += tmp->wb_bytes - (pos - tmp->wb_pgbase);
		}
	} while (tmp && pos < len);

	nfs_page_group_unlock(req);
	WARN_ON_ONCE(pos > len);
	return pos == len;
}

/* We can set the PG_uptodate flag if we see that a write request
 * covers the full page.
 */
static void nfs_mark_uptodate(struct nfs_page *req)
{
	if (PageUptodate(req->wb_page))
		return;
	if (!nfs_page_group_covers_page(req))
		return;
	SetPageUptodate(req->wb_page);
}

static int wb_priority(struct writeback_control *wbc)
{
	int ret = 0;
	if (wbc->for_reclaim)
		return FLUSH_HIGHPRI | FLUSH_STABLE;
	if (wbc->sync_mode == WB_SYNC_ALL)
		ret = FLUSH_COND_STABLE;
	if (wbc->for_kupdate || wbc->for_background)
		ret |= FLUSH_LOWPRI;
	return ret;
}

/*
 * NFS congestion control
 */

int nfs_congestion_kb;

#define NFS_CONGESTION_ON_THRESH 	(nfs_congestion_kb >> (PAGE_SHIFT-10))
#define NFS_CONGESTION_OFF_THRESH	\
	(NFS_CONGESTION_ON_THRESH - (NFS_CONGESTION_ON_THRESH >> 2))

static void nfs_set_page_writeback(struct page *page)
{
	struct nfs_server *nfss = NFS_SERVER(page_file_mapping(page)->host);
	int ret = test_set_page_writeback(page);

	WARN_ON_ONCE(ret != 0);

	if (atomic_long_inc_return(&nfss->writeback) >
			NFS_CONGESTION_ON_THRESH) {
		set_bdi_congested(&nfss->backing_dev_info,
					BLK_RW_ASYNC);
	}
}

static void nfs_end_page_writeback(struct nfs_page *req)
{
	struct inode *inode = page_file_mapping(req->wb_page)->host;
	struct nfs_server *nfss = NFS_SERVER(inode);

	if (!nfs_page_group_sync_on_bit(req, PG_WB_END))
		return;

	end_page_writeback(req->wb_page);
	if (atomic_long_dec_return(&nfss->writeback) < NFS_CONGESTION_OFF_THRESH)
		clear_bdi_congested(&nfss->backing_dev_info, BLK_RW_ASYNC);
}


/* nfs_page_group_clear_bits
 *   @req - an nfs request
 * clears all page group related bits from @req
 */
static void
nfs_page_group_clear_bits(struct nfs_page *req)
{
	clear_bit(PG_TEARDOWN, &req->wb_flags);
	clear_bit(PG_UNLOCKPAGE, &req->wb_flags);
	clear_bit(PG_UPTODATE, &req->wb_flags);
	clear_bit(PG_WB_END, &req->wb_flags);
	clear_bit(PG_REMOVE, &req->wb_flags);
}


/*
 * nfs_unroll_locks_and_wait -  unlock all newly locked reqs and wait on @req
 *
 * this is a helper function for nfs_lock_and_join_requests
 *
 * @inode - inode associated with request page group, must be holding inode lock
 * @head  - head request of page group, must be holding head lock
 * @req   - request that couldn't lock and needs to wait on the req bit lock
 * @nonblock - if true, don't actually wait
 *
 * NOTE: this must be called holding page_group bit lock and inode spin lock
 *       and BOTH will be released before returning.
 *
 * returns 0 on success, < 0 on error.
 */
static int
nfs_unroll_locks_and_wait(struct inode *inode, struct nfs_page *head,
			  struct nfs_page *req, bool nonblock)
	__releases(&inode->i_lock)
{
	struct nfs_page *tmp;
	int ret;

	/* relinquish all the locks successfully grabbed this run */
	for (tmp = head ; tmp != req; tmp = tmp->wb_this_page)
		nfs_unlock_request(tmp);

	WARN_ON_ONCE(test_bit(PG_TEARDOWN, &req->wb_flags));

	/* grab a ref on the request that will be waited on */
	kref_get(&req->wb_kref);

	nfs_page_group_unlock(head);
	spin_unlock(&inode->i_lock);

	/* release ref from nfs_page_find_head_request_locked */
	nfs_release_request(head);

	if (!nonblock)
		ret = nfs_wait_on_request(req);
	else
		ret = -EAGAIN;
	nfs_release_request(req);

	return ret;
}

/*
 * nfs_destroy_unlinked_subrequests - destroy recently unlinked subrequests
 *
 * @destroy_list - request list (using wb_this_page) terminated by @old_head
 * @old_head - the old head of the list
 *
 * All subrequests must be locked and removed from all lists, so at this point
 * they are only "active" in this function, and possibly in nfs_wait_on_request
 * with a reference held by some other context.
 */
static void
nfs_destroy_unlinked_subrequests(struct nfs_page *destroy_list,
				 struct nfs_page *old_head)
{
	while (destroy_list) {
		struct nfs_page *subreq = destroy_list;

		destroy_list = (subreq->wb_this_page == old_head) ?
				   NULL : subreq->wb_this_page;

		WARN_ON_ONCE(old_head != subreq->wb_head);

		/* make sure old group is not used */
		subreq->wb_head = subreq;
		subreq->wb_this_page = subreq;

		/* subreq is now totally disconnected from page group or any
		 * write / commit lists. last chance to wake any waiters */
		nfs_unlock_request(subreq);

		if (!test_bit(PG_TEARDOWN, &subreq->wb_flags)) {
			/* release ref on old head request */
			nfs_release_request(old_head);

			nfs_page_group_clear_bits(subreq);

			/* release the PG_INODE_REF reference */
			if (test_and_clear_bit(PG_INODE_REF, &subreq->wb_flags))
				nfs_release_request(subreq);
			else
				WARN_ON_ONCE(1);
		} else {
			WARN_ON_ONCE(test_bit(PG_CLEAN, &subreq->wb_flags));
			/* zombie requests have already released the last
			 * reference and were waiting on the rest of the
			 * group to complete. Since it's no longer part of a
			 * group, simply free the request */
			nfs_page_group_clear_bits(subreq);
			nfs_free_request(subreq);
		}
	}
}

/*
 * nfs_lock_and_join_requests - join all subreqs to the head req and return
 *                              a locked reference, cancelling any pending
 *                              operations for this page.
 *
 * @page - the page used to lookup the "page group" of nfs_page structures
 * @nonblock - if true, don't block waiting for request locks
 *
 * This function joins all sub requests to the head request by first
 * locking all requests in the group, cancelling any pending operations
 * and finally updating the head request to cover the whole range covered by
 * the (former) group.  All subrequests are removed from any write or commit
 * lists, unlinked from the group and destroyed.
 *
 * Returns a locked, referenced pointer to the head request - which after
 * this call is guaranteed to be the only request associated with the page.
 * Returns NULL if no requests are found for @page, or a ERR_PTR if an
 * error was encountered.
 */
static struct nfs_page *
nfs_lock_and_join_requests(struct page *page, bool nonblock)
{
	struct inode *inode = page_file_mapping(page)->host;
	struct nfs_page *head, *subreq;
	struct nfs_page *destroy_list = NULL;
	unsigned int total_bytes;
	int ret;

try_again:
	total_bytes = 0;

	WARN_ON_ONCE(destroy_list);

	spin_lock(&inode->i_lock);

	/*
	 * A reference is taken only on the head request which acts as a
	 * reference to the whole page group - the group will not be destroyed
	 * until the head reference is released.
	 */
	head = nfs_page_find_head_request_locked(NFS_I(inode), page);

	if (!head) {
		spin_unlock(&inode->i_lock);
		return NULL;
	}

	/* holding inode lock, so always make a non-blocking call to try the
	 * page group lock */
	ret = nfs_page_group_lock(head, true);
	if (ret < 0) {
		spin_unlock(&inode->i_lock);

		if (!nonblock && ret == -EAGAIN) {
			nfs_page_group_lock_wait(head);
			nfs_release_request(head);
			goto try_again;
		}

		nfs_release_request(head);
		return ERR_PTR(ret);
	}

	/* lock each request in the page group */
	subreq = head;
	do {
		/*
		 * Subrequests are always contiguous, non overlapping
		 * and in order. If not, it's a programming error.
		 */
		WARN_ON_ONCE(subreq->wb_offset !=
		     (head->wb_offset + total_bytes));

		/* keep track of how many bytes this group covers */
		total_bytes += subreq->wb_bytes;

		if (!nfs_lock_request(subreq)) {
			/* releases page group bit lock and
			 * inode spin lock and all references */
			ret = nfs_unroll_locks_and_wait(inode, head,
				subreq, nonblock);

			if (ret == 0)
				goto try_again;

			return ERR_PTR(ret);
		}

		subreq = subreq->wb_this_page;
	} while (subreq != head);

	/* Now that all requests are locked, make sure they aren't on any list.
	 * Commit list removal accounting is done after locks are dropped */
	subreq = head;
	do {
		nfs_clear_request_commit(subreq);
		subreq = subreq->wb_this_page;
	} while (subreq != head);

	/* unlink subrequests from head, destroy them later */
	if (head->wb_this_page != head) {
		/* destroy list will be terminated by head */
		destroy_list = head->wb_this_page;
		head->wb_this_page = head;

		/* change head request to cover whole range that
		 * the former page group covered */
		head->wb_bytes = total_bytes;
	}

	/*
	 * prepare head request to be added to new pgio descriptor
	 */
	nfs_page_group_clear_bits(head);

	/*
	 * some part of the group was still on the inode list - otherwise
	 * the group wouldn't be involved in async write.
	 * grab a reference for the head request, iff it needs one.
	 */
	if (!test_and_set_bit(PG_INODE_REF, &head->wb_flags))
		kref_get(&head->wb_kref);

	nfs_page_group_unlock(head);

	/* drop lock to clean uprequests on destroy list */
	spin_unlock(&inode->i_lock);

	nfs_destroy_unlinked_subrequests(destroy_list, head);

	/* still holds ref on head from nfs_page_find_head_request_locked
	 * and still has lock on head from lock loop */
	return head;
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

	req = nfs_lock_and_join_requests(page, nonblock);
	if (!req)
		goto out;
	ret = PTR_ERR(req);
	if (IS_ERR(req))
		goto out;

	nfs_set_page_writeback(page);
	WARN_ON_ONCE(test_bit(PG_CLEAN, &req->wb_flags));

	ret = 0;
	if (!nfs_pageio_add_request(pgio, req)) {
		nfs_redirty_request(req);
		ret = pgio->pg_error;
	}
out:
	return ret;
}

static int nfs_do_writepage(struct page *page, struct writeback_control *wbc, struct nfs_pageio_descriptor *pgio)
{
	struct inode *inode = page_file_mapping(page)->host;
	int ret;

	nfs_inc_stats(inode, NFSIOS_VFSWRITEPAGE);
	nfs_add_stats(inode, NFSIOS_WRITEPAGES, 1);

	nfs_pageio_cond_complete(pgio, page_file_index(page));
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
				false, &nfs_async_write_completion_ops);
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
	err = wait_on_bit_lock_action(bitlock, NFS_INO_FLUSHING,
			nfs_wait_bit_killable, TASK_KILLABLE);
	if (err)
		goto out_err;

	nfs_inc_stats(inode, NFSIOS_VFSWRITEPAGES);

	nfs_pageio_init_write(&pgio, inode, wb_priority(wbc), false,
				&nfs_async_write_completion_ops);
	err = write_cache_pages(mapping, wbc, nfs_writepages_callback, &pgio);
	nfs_pageio_complete(&pgio);

	clear_bit_unlock(NFS_INO_FLUSHING, bitlock);
	smp_mb__after_atomic();
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

	WARN_ON_ONCE(req->wb_this_page != req);

	/* Lock the request! */
	nfs_lock_request(req);

	spin_lock(&inode->i_lock);
	if (!nfsi->npages && NFS_PROTO(inode)->have_delegation(inode, FMODE_WRITE))
		inode->i_version++;
	/*
	 * Swap-space should not get truncated. Hence no need to plug the race
	 * with invalidate/truncate.
	 */
	if (likely(!PageSwapCache(req->wb_page))) {
		set_bit(PG_MAPPED, &req->wb_flags);
		SetPagePrivate(req->wb_page);
		set_page_private(req->wb_page, (unsigned long)req);
	}
	nfsi->npages++;
	/* this a head request for a page group - mark it as having an
	 * extra reference so sub groups can follow suit */
	WARN_ON(test_and_set_bit(PG_INODE_REF, &req->wb_flags));
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
	struct nfs_page *head;

	if (nfs_page_group_sync_on_bit(req, PG_REMOVE)) {
		head = req->wb_head;

		spin_lock(&inode->i_lock);
		if (likely(!PageSwapCache(head->wb_page))) {
			set_page_private(head->wb_page, 0);
			ClearPagePrivate(head->wb_page);
			smp_mb__after_atomic();
			wake_up_page(head->wb_page, PG_private);
			clear_bit(PG_MAPPED, &head->wb_flags);
		}
		nfsi->npages--;
		spin_unlock(&inode->i_lock);
	}

	if (test_and_clear_bit(PG_INODE_REF, &req->wb_flags))
		nfs_release_request(req);
	else
		WARN_ON_ONCE(1);
}

static void
nfs_mark_request_dirty(struct nfs_page *req)
{
	__set_page_dirty_nobuffers(req->wb_page);
}

/*
 * nfs_page_search_commits_for_head_request_locked
 *
 * Search through commit lists on @inode for the head request for @page.
 * Must be called while holding the inode (which is cinfo) lock.
 *
 * Returns the head request if found, or NULL if not found.
 */
static struct nfs_page *
nfs_page_search_commits_for_head_request_locked(struct nfs_inode *nfsi,
						struct page *page)
{
	struct nfs_page *freq, *t;
	struct nfs_commit_info cinfo;
	struct inode *inode = &nfsi->vfs_inode;

	nfs_init_cinfo_from_inode(&cinfo, inode);

	/* search through pnfs commit lists */
	freq = pnfs_search_commit_reqs(inode, &cinfo, page);
	if (freq)
		return freq->wb_head;

	/* Linearly search the commit list for the correct request */
	list_for_each_entry_safe(freq, t, &cinfo.mds->list, wb_list) {
		if (freq->wb_page == page)
			return freq->wb_head;
	}

	return NULL;
}

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
		inc_bdi_stat(page_file_mapping(req->wb_page)->backing_dev_info,
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
	dec_bdi_stat(page_file_mapping(page)->backing_dev_info, BDI_RECLAIMABLE);
}

/* Called holding inode (/cinfo) lock */
static void
nfs_clear_request_commit(struct nfs_page *req)
{
	if (test_bit(PG_CLEAN, &req->wb_flags)) {
		struct inode *inode = req->wb_context->dentry->d_inode;
		struct nfs_commit_info cinfo;

		nfs_init_cinfo_from_inode(&cinfo, inode);
		if (!pnfs_clear_request_commit(req, &cinfo)) {
			nfs_request_remove_commit_list(req, &cinfo);
		}
		nfs_clear_page_commit(req->wb_page);
	}
}

int nfs_write_need_commit(struct nfs_pgio_header *hdr)
{
	if (hdr->verf.committed == NFS_DATA_SYNC)
		return hdr->lseg == NULL;
	return hdr->verf.committed != NFS_FILE_SYNC;
}

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
		if (nfs_write_need_commit(hdr)) {
			memcpy(&req->wb_verf, &hdr->verf.verifier, sizeof(req->wb_verf));
			nfs_mark_request_commit(req, hdr->lseg, &cinfo);
			goto next;
		}
remove_req:
		nfs_inode_remove_request(req);
next:
		nfs_unlock_request(req);
		nfs_end_page_writeback(req);
		nfs_release_request(req);
	}
out:
	hdr->release(hdr);
}

unsigned long
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
		req = nfs_page_find_head_request_locked(NFS_I(inode), page);
		if (req == NULL)
			goto out_unlock;

		/* should be handled by nfs_flush_incompatible */
		WARN_ON_ONCE(req->wb_head != req);
		WARN_ON_ONCE(req->wb_this_page != req);

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
	if (req)
		nfs_clear_request_commit(req);
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
	struct inode *inode = page_file_mapping(page)->host;
	struct nfs_page	*req;

	req = nfs_try_to_update_request(inode, page, offset, bytes);
	if (req != NULL)
		goto out;
	req = nfs_create_request(ctx, page, NULL, offset, bytes);
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
	nfs_mark_uptodate(req);
	nfs_mark_request_dirty(req);
	nfs_unlock_and_release_request(req);
	return 0;
}

int nfs_flush_incompatible(struct file *file, struct page *page)
{
	struct nfs_open_context *ctx = nfs_file_open_context(file);
	struct nfs_lock_context *l_ctx;
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
		req = nfs_page_find_head_request(page);
		if (req == NULL)
			return 0;
		l_ctx = req->wb_lock_context;
		do_flush = req->wb_page != page || req->wb_context != ctx;
		/* for now, flush if more than 1 request in page_group */
		do_flush |= req->wb_this_page != req;
		if (l_ctx && ctx->dentry->d_inode->i_flock != NULL) {
			do_flush |= l_ctx->lockowner.l_owner != current->files
				|| l_ctx->lockowner.l_pid != current->tgid;
		}
		nfs_release_request(req);
		if (!do_flush)
			return 0;
		status = nfs_wb_page(page_file_mapping(page)->host, page);
	} while (status == 0);
	return status;
}

/*
 * Avoid buffered writes when a open context credential's key would
 * expire soon.
 *
 * Returns -EACCES if the key will expire within RPC_KEY_EXPIRE_FAIL.
 *
 * Return 0 and set a credential flag which triggers the inode to flush
 * and performs  NFS_FILE_SYNC writes if the key will expired within
 * RPC_KEY_EXPIRE_TIMEO.
 */
int
nfs_key_timeout_notify(struct file *filp, struct inode *inode)
{
	struct nfs_open_context *ctx = nfs_file_open_context(filp);
	struct rpc_auth *auth = NFS_SERVER(inode)->client->cl_auth;

	return rpcauth_key_timeout_notify(auth, ctx->cred);
}

/*
 * Test if the open context credential key is marked to expire soon.
 */
bool nfs_ctx_key_to_expire(struct nfs_open_context *ctx)
{
	return rpcauth_cred_key_to_expire(ctx->cred);
}

/*
 * If the page cache is marked as unsafe or invalid, then we can't rely on
 * the PageUptodate() flag. In this case, we will need to turn off
 * write optimisations that depend on the page contents being correct.
 */
static bool nfs_write_pageuptodate(struct page *page, struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	if (nfs_have_delegated_attributes(inode))
		goto out;
	if (nfsi->cache_validity & NFS_INO_REVAL_PAGECACHE)
		return false;
	smp_rmb();
	if (test_bit(NFS_INO_INVALIDATING, &nfsi->flags))
		return false;
out:
	if (nfsi->cache_validity & NFS_INO_INVALID_DATA)
		return false;
	return PageUptodate(page) != 0;
}

/* If we know the page is up to date, and we're not using byte range locks (or
 * if we have the whole file locked for writing), it may be more efficient to
 * extend the write to cover the entire page in order to avoid fragmentation
 * inefficiencies.
 *
 * If the file is opened for synchronous writes then we can just skip the rest
 * of the checks.
 */
static int nfs_can_extend_write(struct file *file, struct page *page, struct inode *inode)
{
	if (file->f_flags & O_DSYNC)
		return 0;
	if (!nfs_write_pageuptodate(page, inode))
		return 0;
	if (NFS_PROTO(inode)->have_delegation(inode, FMODE_WRITE))
		return 1;
	if (inode->i_flock == NULL || (inode->i_flock->fl_start == 0 &&
			inode->i_flock->fl_end == OFFSET_MAX &&
			inode->i_flock->fl_type != F_RDLCK))
		return 1;
	return 0;
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
	struct inode	*inode = page_file_mapping(page)->host;
	int		status = 0;

	nfs_inc_stats(inode, NFSIOS_VFSUPDATEPAGE);

	dprintk("NFS:       nfs_updatepage(%pD2 %d@%lld)\n",
		file, count, (long long)(page_file_offset(page) + offset));

	if (nfs_can_extend_write(file, page, inode)) {
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

static void nfs_initiate_write(struct nfs_pgio_header *hdr,
			       struct rpc_message *msg,
			       struct rpc_task_setup *task_setup_data, int how)
{
	struct inode *inode = hdr->inode;
	int priority = flush_task_priority(how);

	task_setup_data->priority = priority;
	NFS_PROTO(inode)->write_setup(hdr, msg);

	nfs4_state_protect_write(NFS_SERVER(inode)->nfs_client,
				 &task_setup_data->rpc_client, msg, hdr);
}

/* If a nfs_flush_* function fails, it should remove reqs from @head and
 * call this on each, which will prepare them to be retried on next
 * writeback using standard nfs.
 */
static void nfs_redirty_request(struct nfs_page *req)
{
	nfs_mark_request_dirty(req);
	nfs_unlock_request(req);
	nfs_end_page_writeback(req);
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

void nfs_pageio_init_write(struct nfs_pageio_descriptor *pgio,
			       struct inode *inode, int ioflags, bool force_mds,
			       const struct nfs_pgio_completion_ops *compl_ops)
{
	struct nfs_server *server = NFS_SERVER(inode);
	const struct nfs_pageio_ops *pg_ops = &nfs_pgio_rw_ops;

#ifdef CONFIG_NFS_V4_1
	if (server->pnfs_curr_ld && !force_mds)
		pg_ops = server->pnfs_curr_ld->pg_write_ops;
#endif
	nfs_pageio_init(pgio, inode, pg_ops, compl_ops, &nfs_rw_write_ops,
			server->wsize, ioflags);
}
EXPORT_SYMBOL_GPL(nfs_pageio_init_write);

void nfs_pageio_reset_write_mds(struct nfs_pageio_descriptor *pgio)
{
	pgio->pg_ops = &nfs_pgio_rw_ops;
	pgio->pg_bsize = NFS_SERVER(pgio->pg_inode)->wsize;
}
EXPORT_SYMBOL_GPL(nfs_pageio_reset_write_mds);


void nfs_commit_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs_commit_data *data = calldata;

	NFS_PROTO(data->inode)->commit_rpc_prepare(task, data);
}

static void nfs_writeback_release_common(struct nfs_pgio_header *hdr)
{
	/* do nothing! */
}

/*
 * Special version of should_remove_suid() that ignores capabilities.
 */
static int nfs_should_remove_suid(const struct inode *inode)
{
	umode_t mode = inode->i_mode;
	int kill = 0;

	/* suid always must be killed */
	if (unlikely(mode & S_ISUID))
		kill = ATTR_KILL_SUID;

	/*
	 * sgid without any exec bits is just a mandatory locking mark; leave
	 * it alone.  If some exec bits are set, it's a real sgid; kill it.
	 */
	if (unlikely((mode & S_ISGID) && (mode & S_IXGRP)))
		kill |= ATTR_KILL_SGID;

	if (unlikely(kill && S_ISREG(mode)))
		return kill;

	return 0;
}

/*
 * This function is called when the WRITE call is complete.
 */
static int nfs_writeback_done(struct rpc_task *task,
			      struct nfs_pgio_header *hdr,
			      struct inode *inode)
{
	int status;

	/*
	 * ->write_done will attempt to use post-op attributes to detect
	 * conflicting writes by other clients.  A strict interpretation
	 * of close-to-open would allow us to continue caching even if
	 * another writer had changed the file, but some applications
	 * depend on tighter cache coherency when writing.
	 */
	status = NFS_PROTO(inode)->write_done(task, hdr);
	if (status != 0)
		return status;
	nfs_add_stats(inode, NFSIOS_SERVERWRITTENBYTES, hdr->res.count);

	if (hdr->res.verf->committed < hdr->args.stable &&
	    task->tk_status >= 0) {
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
				hdr->res.verf->committed, hdr->args.stable);
			complain = jiffies + 300 * HZ;
		}
	}

	/* Deal with the suid/sgid bit corner case */
	if (nfs_should_remove_suid(inode))
		nfs_mark_for_revalidate(inode);
	return 0;
}

/*
 * This function is called when the WRITE call is complete.
 */
static void nfs_writeback_result(struct rpc_task *task,
				 struct nfs_pgio_header *hdr)
{
	struct nfs_pgio_args	*argp = &hdr->args;
	struct nfs_pgio_res	*resp = &hdr->res;

	if (resp->count < argp->count) {
		static unsigned long    complain;

		/* This a short write! */
		nfs_inc_stats(hdr->inode, NFSIOS_SHORTWRITE);

		/* Has the server at least made some progress? */
		if (resp->count == 0) {
			if (time_before(complain, jiffies)) {
				printk(KERN_WARNING
				       "NFS: Server wrote zero bytes, expected %u.\n",
				       argp->count);
				complain = jiffies + 300 * HZ;
			}
			nfs_set_pgio_error(hdr, -EIO, argp->offset);
			task->tk_status = -EIO;
			return;
		}
		/* Was this an NFSv2 write or an NFSv3 stable write? */
		if (resp->verf->committed != NFS_UNSTABLE) {
			/* Resend from where the server left off */
			hdr->mds_offset += resp->count;
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
	smp_mb__after_atomic();
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

	nfs4_state_protect(NFS_SERVER(data->inode)->nfs_client,
		NFS_SP4_MACH_CRED_COMMIT, &task_setup_data.rpc_client, &msg);

	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	if (how & FLUSH_SYNC)
		rpc_wait_for_completion_task(task);
	rpc_put_task(task);
	return 0;
}
EXPORT_SYMBOL_GPL(nfs_initiate_commit);

static loff_t nfs_get_lwb(struct list_head *head)
{
	loff_t lwb = 0;
	struct nfs_page *req;

	list_for_each_entry(req, head, wb_list)
		if (lwb < (req_offset(req) + req->wb_bytes))
			lwb = req_offset(req) + req->wb_bytes;

	return lwb;
}

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
	/* only set lwb for pnfs commit */
	if (lseg)
		data->lwb = nfs_get_lwb(&data->pages);
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
			dec_bdi_stat(page_file_mapping(req->wb_page)->backing_dev_info,
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
	struct nfs_server *nfss;

	while (!list_empty(&data->pages)) {
		req = nfs_list_entry(data->pages.next);
		nfs_list_remove_request(req);
		nfs_clear_page_commit(req->wb_page);

		dprintk("NFS:       commit (%s/%llu %d@%lld)",
			req->wb_context->dentry->d_sb->s_id,
			(unsigned long long)NFS_FILEID(req->wb_context->dentry->d_inode),
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
		if (!memcmp(&req->wb_verf, &data->verf.verifier, sizeof(req->wb_verf))) {
			/* We have a match */
			nfs_inode_remove_request(req);
			dprintk(" OK\n");
			goto next;
		}
		/* We have a mismatch. Write the page again */
		dprintk(" mismatch\n");
		nfs_mark_request_dirty(req);
		set_bit(NFS_CONTEXT_RESEND_WRITES, &req->wb_context->flags);
	next:
		nfs_unlock_and_release_request(req);
	}
	nfss = NFS_SERVER(data->inode);
	if (atomic_long_read(&nfss->writeback) < NFS_CONGESTION_OFF_THRESH)
		clear_bdi_congested(&nfss->backing_dev_info, BLK_RW_ASYNC);

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
		error = wait_on_bit_action(&NFS_I(inode)->flags,
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

int nfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	return nfs_commit_unstable_pages(inode, wbc);
}
EXPORT_SYMBOL_GPL(nfs_write_inode);

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
	int ret;

	trace_nfs_writeback_inode_enter(inode);

	ret = sync_inode(inode, &wbc);

	trace_nfs_writeback_inode_exit(inode, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(nfs_wb_all);

int nfs_wb_page_cancel(struct inode *inode, struct page *page)
{
	struct nfs_page *req;
	int ret = 0;

	wait_on_page_writeback(page);

	/* blocking call to cancel all requests and join to a single (head)
	 * request */
	req = nfs_lock_and_join_requests(page, false);

	if (IS_ERR(req)) {
		ret = PTR_ERR(req);
	} else if (req) {
		/* all requests from this page have been cancelled by
		 * nfs_lock_and_join_requests, so just remove the head
		 * request from the inode / page_private pointer and
		 * release it */
		nfs_inode_remove_request(req);
		/*
		 * In case nfs_inode_remove_request has marked the
		 * page as being dirty
		 */
		cancel_dirty_page(page, PAGE_CACHE_SIZE);
		nfs_unlock_and_release_request(req);
	}

	return ret;
}

/*
 * Write back all requests on one page - we do this before reading it.
 */
int nfs_wb_page(struct inode *inode, struct page *page)
{
	loff_t range_start = page_file_offset(page);
	loff_t range_end = range_start + (loff_t)(PAGE_CACHE_SIZE - 1);
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = 0,
		.range_start = range_start,
		.range_end = range_end,
	};
	int ret;

	trace_nfs_writeback_page_enter(inode);

	for (;;) {
		wait_on_page_writeback(page);
		if (clear_page_dirty_for_io(page)) {
			ret = nfs_writepage_locked(page, &wbc);
			if (ret < 0)
				goto out_error;
			continue;
		}
		ret = 0;
		if (!PagePrivate(page))
			break;
		ret = nfs_commit_inode(inode, FLUSH_SYNC);
		if (ret < 0)
			goto out_error;
	}
out_error:
	trace_nfs_writeback_page_exit(inode, ret);
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

	if (!nfs_fscache_release_page(page, GFP_KERNEL))
		return -EBUSY;

	return migrate_page(mapping, newpage, page, mode);
}
#endif

int __init nfs_init_writepagecache(void)
{
	nfs_wdata_cachep = kmem_cache_create("nfs_write_data",
					     sizeof(struct nfs_pgio_header),
					     0, SLAB_HWCACHE_ALIGN,
					     NULL);
	if (nfs_wdata_cachep == NULL)
		return -ENOMEM;

	nfs_wdata_mempool = mempool_create_slab_pool(MIN_POOL_WRITE,
						     nfs_wdata_cachep);
	if (nfs_wdata_mempool == NULL)
		goto out_destroy_write_cache;

	nfs_cdata_cachep = kmem_cache_create("nfs_commit_data",
					     sizeof(struct nfs_commit_data),
					     0, SLAB_HWCACHE_ALIGN,
					     NULL);
	if (nfs_cdata_cachep == NULL)
		goto out_destroy_write_mempool;

	nfs_commit_mempool = mempool_create_slab_pool(MIN_POOL_COMMIT,
						      nfs_cdata_cachep);
	if (nfs_commit_mempool == NULL)
		goto out_destroy_commit_cache;

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

out_destroy_commit_cache:
	kmem_cache_destroy(nfs_cdata_cachep);
out_destroy_write_mempool:
	mempool_destroy(nfs_wdata_mempool);
out_destroy_write_cache:
	kmem_cache_destroy(nfs_wdata_cachep);
	return -ENOMEM;
}

void nfs_destroy_writepagecache(void)
{
	mempool_destroy(nfs_commit_mempool);
	kmem_cache_destroy(nfs_cdata_cachep);
	mempool_destroy(nfs_wdata_mempool);
	kmem_cache_destroy(nfs_wdata_cachep);
}

static const struct nfs_rw_ops nfs_rw_write_ops = {
	.rw_mode		= FMODE_WRITE,
	.rw_alloc_header	= nfs_writehdr_alloc,
	.rw_free_header		= nfs_writehdr_free,
	.rw_release		= nfs_writeback_release_common,
	.rw_done		= nfs_writeback_done,
	.rw_result		= nfs_writeback_result,
	.rw_initiate		= nfs_initiate_write,
};
