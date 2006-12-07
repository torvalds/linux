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
#include <linux/sunrpc/clnt.h>
#include <linux/nfs3.h>
#include <linux/nfs4.h>
#include <linux/nfs_page.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>

#define NFS_PARANOIA 1

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
	BUG_ON(PagePrivate(page));
	BUG_ON(!PageLocked(page));
	BUG_ON(page->mapping->host != inode);
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
	wake_up_bit(&req->wb_flags, PG_BUSY);
	nfs_release_request(req);
}

/**
 * nfs_set_page_writeback_locked - Lock a request for writeback
 * @req:
 */
int nfs_set_page_writeback_locked(struct nfs_page *req)
{
	struct nfs_inode *nfsi = NFS_I(req->wb_context->dentry->d_inode);

	if (!nfs_lock_request(req))
		return 0;
	radix_tree_tag_set(&nfsi->nfs_page_tree, req->wb_index, NFS_PAGE_TAG_WRITEBACK);
	return 1;
}

/**
 * nfs_clear_page_writeback - Unlock request and wake up sleepers
 */
void nfs_clear_page_writeback(struct nfs_page *req)
{
	struct nfs_inode *nfsi = NFS_I(req->wb_context->dentry->d_inode);

	if (req->wb_page != NULL) {
		spin_lock(&nfsi->req_lock);
		radix_tree_tag_clear(&nfsi->nfs_page_tree, req->wb_index, NFS_PAGE_TAG_WRITEBACK);
		spin_unlock(&nfsi->req_lock);
	}
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

static int nfs_wait_bit_interruptible(void *word)
{
	int ret = 0;

	if (signal_pending(current))
		ret = -ERESTARTSYS;
	else
		schedule();
	return ret;
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
        struct rpc_clnt	*clnt = NFS_CLIENT(req->wb_context->dentry->d_inode);
	sigset_t oldmask;
	int ret = 0;

	if (!test_bit(PG_BUSY, &req->wb_flags))
		goto out;
	/*
	 * Note: the call to rpc_clnt_sigmask() suffices to ensure that we
	 *	 are not interrupted if intr flag is not set
	 */
	rpc_clnt_sigmask(clnt, &oldmask);
	ret = out_of_line_wait_on_bit(&req->wb_flags, PG_BUSY,
			nfs_wait_bit_interruptible, TASK_INTERRUPTIBLE);
	rpc_clnt_sigunmask(clnt, &oldmask);
out:
	return ret;
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

#define NFS_SCAN_MAXENTRIES 16
/**
 * nfs_scan_lock_dirty - Scan the radix tree for dirty requests
 * @nfsi: NFS inode
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
nfs_scan_lock_dirty(struct nfs_inode *nfsi, struct list_head *dst,
	      unsigned long idx_start, unsigned int npages)
{
	struct nfs_page *pgvec[NFS_SCAN_MAXENTRIES];
	struct nfs_page *req;
	unsigned long idx_end;
	int found, i;
	int res;

	res = 0;
	if (npages == 0)
		idx_end = ~0;
	else
		idx_end = idx_start + npages - 1;

	for (;;) {
		found = radix_tree_gang_lookup_tag(&nfsi->nfs_page_tree,
				(void **)&pgvec[0], idx_start, NFS_SCAN_MAXENTRIES,
				NFS_PAGE_TAG_DIRTY);
		if (found <= 0)
			break;
		for (i = 0; i < found; i++) {
			req = pgvec[i];
			if (req->wb_index > idx_end)
				goto out;

			idx_start = req->wb_index + 1;

			if (nfs_set_page_writeback_locked(req)) {
				radix_tree_tag_clear(&nfsi->nfs_page_tree,
						req->wb_index, NFS_PAGE_TAG_DIRTY);
				nfs_list_remove_request(req);
				nfs_list_add_request(req, dst);
				dec_zone_page_state(req->wb_page, NR_FILE_DIRTY);
				res++;
			}
		}
	}
out:
	return res;
}

/**
 * nfs_scan_list - Scan a list for matching requests
 * @nfsi: NFS inode
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
int nfs_scan_list(struct nfs_inode *nfsi, struct list_head *head,
		struct list_head *dst, unsigned long idx_start,
		unsigned int npages)
{
	struct nfs_page *pgvec[NFS_SCAN_MAXENTRIES];
	struct nfs_page *req;
	unsigned long idx_end;
	int found, i;
	int res;

	res = 0;
	if (npages == 0)
		idx_end = ~0;
	else
		idx_end = idx_start + npages - 1;

	for (;;) {
		found = radix_tree_gang_lookup(&nfsi->nfs_page_tree,
				(void **)&pgvec[0], idx_start,
				NFS_SCAN_MAXENTRIES);
		if (found <= 0)
			break;
		for (i = 0; i < found; i++) {
			req = pgvec[i];
			if (req->wb_index > idx_end)
				goto out;
			idx_start = req->wb_index + 1;
			if (req->wb_list_head != head)
				continue;
			if (nfs_set_page_writeback_locked(req)) {
				nfs_list_remove_request(req);
				nfs_list_add_request(req, dst);
				res++;
			}
		}

	}
out:
	return res;
}

int __init nfs_init_nfspagecache(void)
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
	kmem_cache_destroy(nfs_page_cachep);
}

