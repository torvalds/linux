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
#include <linux/nfs.h>
#include <linux/nfs3.h>
#include <linux/nfs4.h>
#include <linux/nfs_page.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/export.h>

#include "internal.h"
#include "pnfs.h"

static struct kmem_cache *nfs_page_cachep;

bool nfs_pgarray_set(struct nfs_page_array *p, unsigned int pagecount)
{
	p->npages = pagecount;
	if (pagecount <= ARRAY_SIZE(p->page_array))
		p->pagevec = p->page_array;
	else {
		p->pagevec = kcalloc(pagecount, sizeof(struct page *), GFP_KERNEL);
		if (!p->pagevec)
			p->npages = 0;
	}
	return p->pagevec != NULL;
}

void nfs_pgheader_init(struct nfs_pageio_descriptor *desc,
		       struct nfs_pgio_header *hdr,
		       void (*release)(struct nfs_pgio_header *hdr))
{
	hdr->req = nfs_list_entry(desc->pg_list.next);
	hdr->inode = desc->pg_inode;
	hdr->cred = hdr->req->wb_context->cred;
	hdr->io_start = req_offset(hdr->req);
	hdr->good_bytes = desc->pg_count;
	hdr->dreq = desc->pg_dreq;
	hdr->release = release;
	hdr->completion_ops = desc->pg_completion_ops;
	if (hdr->completion_ops->init_hdr)
		hdr->completion_ops->init_hdr(hdr);
}
EXPORT_SYMBOL_GPL(nfs_pgheader_init);

void nfs_set_pgio_error(struct nfs_pgio_header *hdr, int error, loff_t pos)
{
	spin_lock(&hdr->lock);
	if (pos < hdr->io_start + hdr->good_bytes) {
		set_bit(NFS_IOHDR_ERROR, &hdr->flags);
		clear_bit(NFS_IOHDR_EOF, &hdr->flags);
		hdr->good_bytes = pos - hdr->io_start;
		hdr->error = error;
	}
	spin_unlock(&hdr->lock);
}

static inline struct nfs_page *
nfs_page_alloc(void)
{
	struct nfs_page	*p = kmem_cache_zalloc(nfs_page_cachep, GFP_NOIO);
	if (p)
		INIT_LIST_HEAD(&p->wb_list);
	return p;
}

static inline void
nfs_page_free(struct nfs_page *p)
{
	kmem_cache_free(nfs_page_cachep, p);
}

/**
 * nfs_create_request - Create an NFS read/write request.
 * @ctx: open context to use
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

	/* try to allocate the request struct */
	req = nfs_page_alloc();
	if (req == NULL)
		return ERR_PTR(-ENOMEM);

	/* get lock context early so we can deal with alloc failures */
	req->wb_lock_context = nfs_get_lock_context(ctx);
	if (req->wb_lock_context == NULL) {
		nfs_page_free(req);
		return ERR_PTR(-ENOMEM);
	}

	/* Initialize the request struct. Initially, we assume a
	 * long write-back delay. This will be adjusted in
	 * update_nfs_request below if the region is not locked. */
	req->wb_page    = page;
	req->wb_index	= page_file_index(page);
	page_cache_get(page);
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
}

/**
 * nfs_unlock_and_release_request - Unlock request and release the nfs_page
 * @req:
 */
void nfs_unlock_and_release_request(struct nfs_page *req)
{
	nfs_unlock_request(req);
	nfs_release_request(req);
}

/*
 * nfs_clear_request - Free up all resources allocated to the request
 * @req:
 *
 * Release page and open context resources associated with a read/write
 * request after it has completed.
 */
static void nfs_clear_request(struct nfs_page *req)
{
	struct page *page = req->wb_page;
	struct nfs_open_context *ctx = req->wb_context;
	struct nfs_lock_context *l_ctx = req->wb_lock_context;

	if (page != NULL) {
		page_cache_release(page);
		req->wb_page = NULL;
	}
	if (l_ctx != NULL) {
		nfs_put_lock_context(l_ctx);
		req->wb_lock_context = NULL;
	}
	if (ctx != NULL) {
		put_nfs_open_context(ctx);
		req->wb_context = NULL;
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

	/* Release struct file and open context */
	nfs_clear_request(req);
	nfs_page_free(req);
}

void nfs_release_request(struct nfs_page *req)
{
	kref_put(&req->wb_kref, nfs_free_request);
}

static int nfs_wait_bit_uninterruptible(void *word)
{
	io_schedule();
	return 0;
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
	return wait_on_bit(&req->wb_flags, PG_BUSY,
			nfs_wait_bit_uninterruptible,
			TASK_UNINTERRUPTIBLE);
}

bool nfs_generic_pg_test(struct nfs_pageio_descriptor *desc, struct nfs_page *prev, struct nfs_page *req)
{
	/*
	 * FIXME: ideally we should be able to coalesce all requests
	 * that are not block boundary aligned, but currently this
	 * is problematic for the case of bsize < PAGE_CACHE_SIZE,
	 * since nfs_flush_multi and nfs_pagein_multi assume you
	 * can have only one struct nfs_page.
	 */
	if (desc->pg_bsize < PAGE_SIZE)
		return 0;

	return desc->pg_count + req->wb_bytes <= desc->pg_bsize;
}
EXPORT_SYMBOL_GPL(nfs_generic_pg_test);

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
		     const struct nfs_pageio_ops *pg_ops,
		     const struct nfs_pgio_completion_ops *compl_ops,
		     size_t bsize,
		     int io_flags)
{
	INIT_LIST_HEAD(&desc->pg_list);
	desc->pg_bytes_written = 0;
	desc->pg_count = 0;
	desc->pg_bsize = bsize;
	desc->pg_base = 0;
	desc->pg_moreio = 0;
	desc->pg_recoalesce = 0;
	desc->pg_inode = inode;
	desc->pg_ops = pg_ops;
	desc->pg_completion_ops = compl_ops;
	desc->pg_ioflags = io_flags;
	desc->pg_error = 0;
	desc->pg_lseg = NULL;
	desc->pg_dreq = NULL;
}
EXPORT_SYMBOL_GPL(nfs_pageio_init);

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
static bool nfs_can_coalesce_requests(struct nfs_page *prev,
				      struct nfs_page *req,
				      struct nfs_pageio_descriptor *pgio)
{
	if (req->wb_context->cred != prev->wb_context->cred)
		return false;
	if (req->wb_lock_context->lockowner != prev->wb_lock_context->lockowner)
		return false;
	if (req->wb_context->state != prev->wb_context->state)
		return false;
	if (req->wb_pgbase != 0)
		return false;
	if (prev->wb_pgbase + prev->wb_bytes != PAGE_CACHE_SIZE)
		return false;
	if (req_offset(req) != req_offset(prev) + prev->wb_bytes)
		return false;
	return pgio->pg_ops->pg_test(pgio, prev, req);
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
	if (desc->pg_count != 0) {
		struct nfs_page *prev;

		prev = nfs_list_entry(desc->pg_list.prev);
		if (!nfs_can_coalesce_requests(prev, req, desc))
			return 0;
	} else {
		if (desc->pg_ops->pg_init)
			desc->pg_ops->pg_init(desc, req);
		desc->pg_base = req->wb_pgbase;
	}
	nfs_list_remove_request(req);
	nfs_list_add_request(req, &desc->pg_list);
	desc->pg_count += req->wb_bytes;
	return 1;
}

/*
 * Helper for nfs_pageio_add_request and nfs_pageio_complete
 */
static void nfs_pageio_doio(struct nfs_pageio_descriptor *desc)
{
	if (!list_empty(&desc->pg_list)) {
		int error = desc->pg_ops->pg_doio(desc);
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
static int __nfs_pageio_add_request(struct nfs_pageio_descriptor *desc,
			   struct nfs_page *req)
{
	while (!nfs_pageio_do_add_request(desc, req)) {
		desc->pg_moreio = 1;
		nfs_pageio_doio(desc);
		if (desc->pg_error < 0)
			return 0;
		desc->pg_moreio = 0;
		if (desc->pg_recoalesce)
			return 0;
	}
	return 1;
}

static int nfs_do_recoalesce(struct nfs_pageio_descriptor *desc)
{
	LIST_HEAD(head);

	do {
		list_splice_init(&desc->pg_list, &head);
		desc->pg_bytes_written -= desc->pg_count;
		desc->pg_count = 0;
		desc->pg_base = 0;
		desc->pg_recoalesce = 0;

		while (!list_empty(&head)) {
			struct nfs_page *req;

			req = list_first_entry(&head, struct nfs_page, wb_list);
			nfs_list_remove_request(req);
			if (__nfs_pageio_add_request(desc, req))
				continue;
			if (desc->pg_error < 0)
				return 0;
			break;
		}
	} while (desc->pg_recoalesce);
	return 1;
}

int nfs_pageio_add_request(struct nfs_pageio_descriptor *desc,
		struct nfs_page *req)
{
	int ret;

	do {
		ret = __nfs_pageio_add_request(desc, req);
		if (ret)
			break;
		if (desc->pg_error < 0)
			break;
		ret = nfs_do_recoalesce(desc);
	} while (ret);
	return ret;
}
EXPORT_SYMBOL_GPL(nfs_pageio_add_request);

/**
 * nfs_pageio_complete - Complete I/O on an nfs_pageio_descriptor
 * @desc: pointer to io descriptor
 */
void nfs_pageio_complete(struct nfs_pageio_descriptor *desc)
{
	for (;;) {
		nfs_pageio_doio(desc);
		if (!desc->pg_recoalesce)
			break;
		if (!nfs_do_recoalesce(desc))
			break;
	}
}
EXPORT_SYMBOL_GPL(nfs_pageio_complete);

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
			nfs_pageio_complete(desc);
	}
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

