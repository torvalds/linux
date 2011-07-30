/*
 * linux/fs/nfs/pagelist.c
 *
 * A set of helper functions for managing NFS read and write requests.
 * The main purpose of these routines is to provide support for the
 * coalescing of several requests into a single RPC call.
 *
 * Copyright 2000, 2001 (c) Trond Myklebust <trond.myklebust@fys.uio.no>
 *
 */

#include <linux/slab.h>
#include <linux/file.h>
#include <linux/sched.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs3.h>
#include <linux/nfs4.h>
#include <linux/nfs_page.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>

#include "internal.h"

static struct kmem_cache *nfs_page_cachep;

static inline struct nfs_page *
nfs_page_alloc(void)
{
	struct nfs_page	*p;
	p = kmem_cache_alloc(nfs_page_cachep, GFP_KERNEL);
	if (p) {
		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&p->wb_list);
	}
	return p;
}

static inline void
nfs_page_free(struct nfs_page *p)
{
	kmem_cache_free(nfs_page_cachep, p);
}

/**
 * nfs_create_request - Create an NFS read/write request.
 * @file: file descriptor to use
 * @inode: inode to which the request is attached
 * @page: page to write
 * @offset: starting offset within the page for the write
 * @count: number of bytes to read/write
 *
 * The page must be locked by the caller. This makes sure we never
 * create two different requests for the same page.
 * User should ensure it is safe to sleep in this function.
 */
struct nfs_page *
nfs_create_request(struct nfs_open_context *ctx, struct inode *inode,
		   struct page *page,
		   unsigned int offset, unsigned int count)
{
	struct nfs_page		*req;

	for (;;) {
		/* try to allocate the request struct */
		req = nfs_page_alloc();
		if (req != NULL)
			break;

		if (fatal_signal_pending(current))
			return ERR_PTR(-ERESTARTSYS);
		yield();
	}

	/* Initialize the request struct. Initially, we assume a
	 * long write-back delay. This will be adjusted in
	 * update_nfs_request below if the region is not locked. */
	req->wb_page    = page;
	atomic_set(&req->wb_complete, 0);
	req->wb_index	= page->index;
	page_cache_get(page);
	BUG_ON(PagePrivate(page));
	BUG_ON(!PageLocked(page));
	BUG_ON(page->mapping->host != inode);
	req->wb_offset  = offset;
	req->wb_pgbase	= offset;
	req->wb_bytes   = count;
	req->wb_context = get_nfs_open_context(ctx);
	kref_init(&req->wb_kref);
	return req;
}

/**
 * nfs_unlock_request - Unlock request and wake up sleepers.
 * @req:
 */
void nfs_unlock_request(struct nfs_page *req)
{
	if (!NFS_WBACK_BUSY(req)) {
		printk(KERN_ERR "NFS: Invalid unlock attempted\n");
		BUG();
	}
	smp_mb__before_clear_bit();
	clear_bit(PG_BUSY, &req->wb_flags);
	smp_mb__after_clear_bit();
	wake_up_bit(&req->wb_flags, PG_BUSY);
	nfs_release_request(req);
}

/**
 * nfs_set_page_tag_locked - Tag a request as locked
 * @req:
 */
int nfs_set_page_tag_locked(struct nfs_page *req)
{
	struct nfs_inode *nfsi = NFS_I(req->wb_context->path.dentry->d_inode);

	if (!nfs_lock_request_dontget(req))
		return 0;
	if (req->wb_page != NULL)
		radix_tree_tag_set(&nfsi->nfs_page_tree, req->wb_index, NFS_PAGE_TAG_LOCKED);
	return 1;
}

/**
 * nfs_clear_page_tag_locked - Clear request tag and wake up sleepers
 */
void nfs_clear_page_tag_locked(struct nfs_page *req)
{
	struct inode *inode = req->wb_context->path.dentry->d_inode;
	struct nfs_inode *nfsi = NFS_I(inode);

	if (req->wb_page != NULL) {
		spin_lock(&inode->i_lock);
		radix_tree_tag_clear(&nfsi->nfs_page_tree, req->wb_index, NFS_PAGE_TAG_LOCKED);
		nfs_unlock_request(req);
		spin_unlock(&inode->i_lock);
	} else
		nfs_unlock_request(req);
}

/**
 * nfs_clear_request - Free up all resources allocated to the request
 * @req:
 *
 * Release page resources associated with a write request after it
 * has completed.
 */
void nfs_clear_request(struct nfs_page *req)
{
	struct page *page = req->wb_page;
	if (page != NULL) {
		page_cache_release(page);
		req->wb_page = NULL;
	}
}


/**
 * nfs_release_request - Release the count on an NFS read/write request
 * @req: request to release
 *
 * Note: Should never be called with the spinlock held!
 */
static void nfs_free_request(struct kref *kref)
{
	struct nfs_page *req = container_of(kref, struct nfs_page, wb_kref);

	/* Release struct file or cached credential */
	nfs_clear_request(req);
	put_nfs_open_context(req->wb_context);
	nfs_page_free(req);
}

void nfs_release_request(struct nfs_page *req)
{
	kref_put(&req->wb_kref, nfs_free_request);
}

/**
 * nfs_wait_on_request - Wait for a request to complete.
 * @req: request to wait upon.
 *
 * Interruptible by fatal signals only.
 * The user is responsible for holding a count on the request.
 */
int
nfs_wait_on_request(struct nfs_page *req)
{
	int ret = 0;

	if (!test_bit(PG_BUSY, &req->wb_flags))
		goto out;
	ret = out_of_line_wait_on_bit(&req->wb_flags, PG_BUSY,
			nfs_wait_bit_killable, TASK_KILLABLE);
out:
	return ret;
}

/**
 * nfs_pageio_init - initialise a page io descriptor
 * @desc: pointer to descriptor
 * @inode: pointer to inode
 * @doio: pointer to io function
 * @bsize: io block size
 * @io_flags: extra parameters for the io function
 */
void nfs_pageio_init(struct nfs_pageio_descriptor *desc,
		     struct inode *inode,
		     int (*doio)(struct inode *, struct list_head *, unsigned int, size_t, int),
		     size_t bsize,
		     int io_flags)
{
	INIT_LIST_HEAD(&desc->pg_list);
	desc->pg_bytes_written = 0;
	desc->pg_count = 0;
	desc->pg_bsize = bsize;
	desc->pg_base = 0;
	desc->pg_inode = inode;
	desc->pg_doio = doio;
	desc->pg_ioflags = io_flags;
	desc->pg_error = 0;
}

/**
 * nfs_can_coalesce_requests - test two requests for compatibility
 * @prev: pointer to nfs_page
 * @req: pointer to nfs_page
 *
 * The nfs_page structures 'prev' and 'req' are compared to ensure that the
 * page data area they describe is contiguous, and that their RPC
 * credentials, NFSv4 open state, and lockowners are the same.
 *
 * Return 'true' if this is the case, else return 'false'.
 */
static int nfs_can_coalesce_requests(struct nfs_page *prev,
				     struct nfs_page *req)
{
	if (req->wb_context->cred != prev->wb_context->cred)
		return 0;
	if (req->wb_context->lockowner != prev->wb_context->lockowner)
		return 0;
	if (req->wb_context->state != prev->wb_context->state)
		return 0;
	if (req->wb_index != (prev->wb_index + 1))
		return 0;
	if (req->wb_pgbase != 0)
		return 0;
	if (prev->wb_pgbase + prev->wb_bytes != PAGE_CACHE_SIZE)
		return 0;
	return 1;
}

/**
 * nfs_pageio_do_add_request - Attempt to coalesce a request into a page list.
 * @desc: destination io descriptor
 * @req: request
 *
 * Returns true if the request 'req' was successfully coalesced into the
 * existing list of pages 'desc'.
 */
static int nfs_pageio_do_add_request(struct nfs_pageio_descriptor *desc,
				     struct nfs_page *req)
{
	size_t newlen = req->wb_bytes;

	if (desc->pg_count != 0) {
		struct nfs_page *prev;

		/*
		 * FIXME: ideally we should be able to coalesce all requests
		 * that are not block boundary aligned, but currently this
		 * is problematic for the case of bsize < PAGE_CACHE_SIZE,
		 * since nfs_flush_multi and nfs_pagein_multi assume you
		 * can have only one struct nfs_page.
		 */
		if (desc->pg_bsize < PAGE_SIZE)
			return 0;
		newlen += desc->pg_count;
		if (newlen > desc->pg_bsize)
			return 0;
		prev = nfs_list_entry(desc->pg_list.prev);
		if (!nfs_can_coalesce_requests(prev, req))
			return 0;
	} else
		desc->pg_base = req->wb_pgbase;
	nfs_list_remove_request(req);
	nfs_list_add_request(req, &desc->pg_list);
	desc->pg_count = newlen;
	return 1;
}

/*
 * Helper for nfs_pageio_add_request and nfs_pageio_complete
 */
static void nfs_pageio_doio(struct nfs_pageio_descriptor *desc)
{
	if (!list_empty(&desc->pg_list)) {
		int error = desc->pg_doio(desc->pg_inode,
					  &desc->pg_list,
					  nfs_page_array_len(desc->pg_base,
							     desc->pg_count),
					  desc->pg_count,
					  desc->pg_ioflags);
		if (error < 0)
			desc->pg_error = error;
		else
			desc->pg_bytes_written += desc->pg_count;
	}
	if (list_empty(&desc->pg_list)) {
		desc->pg_count = 0;
		desc->pg_base = 0;
	}
}

/**
 * nfs_pageio_add_request - Attempt to coalesce a request into a page list.
 * @desc: destination io descriptor
 * @req: request
 *
 * Returns true if the request 'req' was successfully coalesced into the
 * existing list of pages 'desc'.
 */
int nfs_pageio_add_request(struct nfs_pageio_descriptor *desc,
			   struct nfs_page *req)
{
	while (!nfs_pageio_do_add_request(desc, req)) {
		nfs_pageio_doio(desc);
		if (desc->pg_error < 0)
			return 0;
	}
	return 1;
}

/**
 * nfs_pageio_complete - Complete I/O on an nfs_pageio_descriptor
 * @desc: pointer to io descriptor
 */
void nfs_pageio_complete(struct nfs_pageio_descriptor *desc)
{
	nfs_pageio_doio(desc);
}

/**
 * nfs_pageio_cond_complete - Conditional I/O completion
 * @desc: pointer to io descriptor
 * @index: page index
 *
 * It is important to ensure that processes don't try to take locks
 * on non-contiguous ranges of pages as that might deadlock. This
 * function should be called before attempting to wait on a locked
 * nfs_page. It will complete the I/O if the page index 'index'
 * is not contiguous with the existing list of pages in 'desc'.
 */
void nfs_pageio_cond_complete(struct nfs_pageio_descriptor *desc, pgoff_t index)
{
	if (!list_empty(&desc->pg_list)) {
		struct nfs_page *prev = nfs_list_entry(desc->pg_list.prev);
		if (index != prev->wb_index + 1)
			nfs_pageio_doio(desc);
	}
}

#define NFS_SCAN_MAXENTRIES 16
/**
 * nfs_scan_list - Scan a list for matching requests
 * @nfsi: NFS inode
 * @dst: Destination list
 * @idx_start: lower bound of page->index to scan
 * @npages: idx_start + npages sets the upper bound to scan.
 * @tag: tag to scan for
 *
 * Moves elements from one of the inode request lists.
 * If the number of requests is set to 0, the entire address_space
 * starting at index idx_start, is scanned.
 * The requests are *not* checked to ensure that they form a contiguous set.
 * You must be holding the inode's i_lock when calling this function
 */
int nfs_scan_list(struct nfs_inode *nfsi,
		struct list_head *dst, pgoff_t idx_start,
		unsigned int npages, int tag)
{
	struct nfs_page *pgvec[NFS_SCAN_MAXENTRIES];
	struct nfs_page *req;
	pgoff_t idx_end;
	int found, i;
	int res;

	res = 0;
	if (npages == 0)
		idx_end = ~0;
	else
		idx_end = idx_start + npages - 1;

	for (;;) {
		found = radix_tree_gang_lookup_tag(&nfsi->nfs_page_tree,
				(void **)&pgvec[0], idx_start,
				NFS_SCAN_MAXENTRIES, tag);
		if (found <= 0)
			break;
		for (i = 0; i < found; i++) {
			req = pgvec[i];
			if (req->wb_index > idx_end)
				goto out;
			idx_start = req->wb_index + 1;
			if (nfs_set_page_tag_locked(req)) {
				kref_get(&req->wb_kref);
				nfs_list_remove_request(req);
				radix_tree_tag_clear(&nfsi->nfs_page_tree,
						req->wb_index, tag);
				nfs_list_add_request(req, dst);
				res++;
				if (res == INT_MAX)
					goto out;
			}
		}
		/* for latency reduction */
		cond_resched_lock(&nfsi->vfs_inode.i_lock);
	}
out:
	return res;
}

int __init nfs_init_nfspagecache(void)
{
	nfs_page_cachep = kmem_cache_create("nfs_page",
					    sizeof(struct nfs_page),
					    0, SLAB_HWCACHE_ALIGN,
					    NULL);
	if (nfs_page_cachep == NULL)
		return -ENOMEM;

	return 0;
}

void nfs_destroy_nfspagecache(void)
{
	kmem_cache_destroy(nfs_page_cachep);
}

