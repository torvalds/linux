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

#include <linux/config.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs3.h>
#include <linux/nfs4.h>
#include <linux/nfs_page.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>

#define NFS_PARANOIA 1

static kmem_cache_t *nfs_page_cachep;

static inline struct nfs_page *
nfs_page_alloc(void)
{
	struct nfs_page	*p;
	p = kmem_cache_alloc(nfs_page_cachep, SLAB_KERNEL);
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
 * create two different requests for the same page, and avoids
 * a possible deadlock when we reach the hard limit on the number
 * of dirty pages.
 * User should ensure it is safe to sleep in this function.
 */
struct nfs_page *
nfs_create_request(struct nfs_open_context *ctx, struct inode *inode,
		   struct page *page,
		   unsigned int offset, unsigned int count)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_page		*req;

	/* Deal with hard limits.  */
	for (;;) {
		/* try to allocate the request struct */
		req = nfs_page_alloc();
		if (req != NULL)
			break;

		/* Try to free up at least one request in order to stay
		 * below the hard limit
		 */
		if (signalled() && (server->flags & NFS_MOUNT_INTR))
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
	req->wb_offset  = offset;
	req->wb_pgbase	= offset;
	req->wb_bytes   = count;
	atomic_set(&req->wb_count, 1);
	req->wb_context = get_nfs_open_context(ctx);

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
	wake_up_all(&req->wb_context->waitq);
	nfs_release_request(req);
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
	if (req->wb_page) {
		page_cache_release(req->wb_page);
		req->wb_page = NULL;
	}
}


/**
 * nfs_release_request - Release the count on an NFS read/write request
 * @req: request to release
 *
 * Note: Should never be called with the spinlock held!
 */
void
nfs_release_request(struct nfs_page *req)
{
	if (!atomic_dec_and_test(&req->wb_count))
		return;

#ifdef NFS_PARANOIA
	BUG_ON (!list_empty(&req->wb_list));
	BUG_ON (NFS_WBACK_BUSY(req));
#endif

	/* Release struct file or cached credential */
	nfs_clear_request(req);
	put_nfs_open_context(req->wb_context);
	nfs_page_free(req);
}

/**
 * nfs_list_add_request - Insert a request into a sorted list
 * @req: request
 * @head: head of list into which to insert the request.
 *
 * Note that the wb_list is sorted by page index in order to facilitate
 * coalescing of requests.
 * We use an insertion sort that is optimized for the case of appended
 * writes.
 */
void
nfs_list_add_request(struct nfs_page *req, struct list_head *head)
{
	struct list_head *pos;

#ifdef NFS_PARANOIA
	if (!list_empty(&req->wb_list)) {
		printk(KERN_ERR "NFS: Add to list failed!\n");
		BUG();
	}
#endif
	list_for_each_prev(pos, head) {
		struct nfs_page	*p = nfs_list_entry(pos);
		if (p->wb_index < req->wb_index)
			break;
	}
	list_add(&req->wb_list, pos);
	req->wb_list_head = head;
}

/**
 * nfs_wait_on_request - Wait for a request to complete.
 * @req: request to wait upon.
 *
 * Interruptible by signals only if mounted with intr flag.
 * The user is responsible for holding a count on the request.
 */
int
nfs_wait_on_request(struct nfs_page *req)
{
	struct inode	*inode = req->wb_context->dentry->d_inode;
        struct rpc_clnt	*clnt = NFS_CLIENT(inode);

	if (!NFS_WBACK_BUSY(req))
		return 0;
	return nfs_wait_event(clnt, req->wb_context->waitq, !NFS_WBACK_BUSY(req));
}

/**
 * nfs_coalesce_requests - Split coalesced requests out from a list.
 * @head: source list
 * @dst: destination list
 * @nmax: maximum number of requests to coalesce
 *
 * Moves a maximum of 'nmax' elements from one list to another.
 * The elements are checked to ensure that they form a contiguous set
 * of pages, and that the RPC credentials are the same.
 */
int
nfs_coalesce_requests(struct list_head *head, struct list_head *dst,
		      unsigned int nmax)
{
	struct nfs_page		*req = NULL;
	unsigned int		npages = 0;

	while (!list_empty(head)) {
		struct nfs_page	*prev = req;

		req = nfs_list_entry(head->next);
		if (prev) {
			if (req->wb_context->cred != prev->wb_context->cred)
				break;
			if (req->wb_context->lockowner != prev->wb_context->lockowner)
				break;
			if (req->wb_context->state != prev->wb_context->state)
				break;
			if (req->wb_index != (prev->wb_index + 1))
				break;

			if (req->wb_pgbase != 0)
				break;
		}
		nfs_list_remove_request(req);
		nfs_list_add_request(req, dst);
		npages++;
		if (req->wb_pgbase + req->wb_bytes != PAGE_CACHE_SIZE)
			break;
		if (npages >= nmax)
			break;
	}
	return npages;
}

/**
 * nfs_scan_list - Scan a list for matching requests
 * @head: One of the NFS inode request lists
 * @dst: Destination list
 * @idx_start: lower bound of page->index to scan
 * @npages: idx_start + npages sets the upper bound to scan.
 *
 * Moves elements from one of the inode request lists.
 * If the number of requests is set to 0, the entire address_space
 * starting at index idx_start, is scanned.
 * The requests are *not* checked to ensure that they form a contiguous set.
 * You must be holding the inode's req_lock when calling this function
 */
int
nfs_scan_list(struct list_head *head, struct list_head *dst,
	      unsigned long idx_start, unsigned int npages)
{
	struct list_head	*pos, *tmp;
	struct nfs_page		*req;
	unsigned long		idx_end;
	int			res;

	res = 0;
	if (npages == 0)
		idx_end = ~0;
	else
		idx_end = idx_start + npages - 1;

	list_for_each_safe(pos, tmp, head) {

		req = nfs_list_entry(pos);

		if (req->wb_index < idx_start)
			continue;
		if (req->wb_index > idx_end)
			break;

		if (!nfs_lock_request(req))
			continue;
		nfs_list_remove_request(req);
		nfs_list_add_request(req, dst);
		res++;
	}
	return res;
}

int nfs_init_nfspagecache(void)
{
	nfs_page_cachep = kmem_cache_create("nfs_page",
					    sizeof(struct nfs_page),
					    0, SLAB_HWCACHE_ALIGN,
					    NULL, NULL);
	if (nfs_page_cachep == NULL)
		return -ENOMEM;

	return 0;
}

void nfs_destroy_nfspagecache(void)
{
	if (kmem_cache_destroy(nfs_page_cachep))
		printk(KERN_INFO "nfs_page: not all structures were freed\n");
}

